//===-- SBReproducer.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBREPRODUCER_H
#define LLDB_API_SBREPRODUCER_H

#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Reproducer.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/YAMLTraits.h"

#include <mutex>
#include <string>
#include <vector>

#define SB_RECORD_NO_ARGS                                                      \
  repro::SBRecorder sb_recorder;                                               \
  if (auto *g = repro::Reproducer::Instance().GetGenerator()) {                \
    repro::SBProvider &p = g->GetOrCreate<repro::SBProvider>();                \
    sb_recorder.SetSerializer(&p.GetSerializer());                             \
    sb_recorder.RecordCall(__PRETTY_FUNCTION__);                               \
  }

#define SB_RECORD(...)                                                         \
  repro::SBRecorder sb_recorder;                                               \
  if (auto *g = repro::Reproducer::Instance().GetGenerator()) {                \
    repro::SBProvider &p = g->GetOrCreate<repro::SBProvider>();                \
    sb_recorder.SetSerializer(&p.GetSerializer());                             \
    sb_recorder.RecordCall(__PRETTY_FUNCTION__, __VA_ARGS__);                  \
  }

#define SB_RECORD_RETURN(t) sb_recorder.RecordReturn(t)

namespace lldb {
void GenerateReproducer();
bool ReplayReproducer();
} // namespace lldb

namespace lldb_private {
namespace repro {

template <> const char *SBDeserializer::Read<const char *>();

class SBRecorder {
public:
  SBRecorder() : m_serializer(nullptr), m_local_boundary(false) {
    if (!g_global_boundary) {
      g_global_boundary = true;
      m_local_boundary = true;
    }
  }

  ~SBRecorder() { UpdateBoundary(); }

  void UpdateBoundary() {
    if (m_local_boundary) {
      g_global_boundary = false;
    }
  }

  template <typename... Ts>
  void RecordCall(const char *name, const Ts &... args) {
    if (!ShouldCapture() || !ShouldSerialize()) {
      return;
    }
    m_serializer->Serialize(name);
    m_serializer->Serialize(args...);
  }

  template <typename T> const T &RecordReturn(const T &t) {
    UpdateBoundary();
    if (!ShouldCapture() || !ShouldSerialize()) {
      return t;
    }
    m_serializer->Serialize(t);
    return t;
  }

  void SetSerializer(SBSerializer *serializer) { m_serializer = serializer; }

private:
  bool ShouldCapture() { return m_local_boundary; }
  bool ShouldSerialize() { return m_serializer != nullptr; }

  SBSerializer *m_serializer;
  bool m_local_boundary;

  static thread_local std::atomic<bool> g_global_boundary;
};

class SBReplayer {
public:
  SBReplayer() {}

  void Init();

  llvm::Error Replay() {
    repro::Loader *loader = repro::Reproducer::Instance().GetLoader();
    if (!loader) {
      return llvm::make_error<llvm::StringError>(
          "Cannot replay when not in replay mode.",
          llvm::inconvertibleErrorCode());
    }

    llvm::Optional<ProviderInfo> info = loader->GetProviderInfo("sbapi");
    if (!info) {
      return llvm::make_error<llvm::StringError>(
          "No SB API provider info available to replay.",
          llvm::inconvertibleErrorCode());
    }

    FileSpec file(loader->GetRoot());
    file.AppendPathComponent(info->files.front());

    auto error_or_file = llvm::MemoryBuffer::getFile(file.GetPath());
    if (auto err = error_or_file.getError())
      return llvm::errorCodeToError(err);

    SBDeserializer deserializer((*error_or_file)->getBuffer());
    while (deserializer.HasData()) {
      llvm::StringRef f(deserializer.Read<const char *>());
      if (f.empty())
        break;
      if (m_functions.find(f) == m_functions.end()) {
        llvm::outs() << "Could not find function for '" << f << "' ("
                     << f.size() << ")\n";
        break;
      }
      m_functions[f](deserializer);
    }

    return llvm::Error::success();
  }

private:
  SBIndexToObject m_index_to_object;
  llvm::StringMap<std::function<void(SBDeserializer &)>> m_functions;
};

class SBProvider : public Provider<SBProvider> {
public:
  SBProvider(const FileSpec &directory)
      : Provider(directory),
        m_stream(directory.CopyByAppendingPathComponent("sbapi.bin").GetPath(),
                 m_ec, llvm::sys::fs::OpenFlags::F_None),
        m_serializer(m_stream) {
    m_info.name = "sbapi";
    m_info.files.push_back("sbapi.bin");
  }

  SBSerializer &GetSerializer() { return m_serializer; }

  static char ID;

private:
  std::error_code m_ec;
  llvm::raw_fd_ostream m_stream;
  SBSerializer m_serializer;
};

} // namespace repro
} // namespace lldb_private

#endif
