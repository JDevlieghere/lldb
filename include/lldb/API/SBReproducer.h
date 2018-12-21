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

#define RECORD_STATIC                                                          \
  repro::SBRecorder sb_recorder;                                               \
  if (auto *g = repro::Reproducer::Instance().GetGenerator()) {                \
    repro::SBProvider &p = g->GetOrCreate<repro::SBProvider>();                \
    sb_recorder.SetSerializer(&p.GetSerializer());                             \
    sb_recorder.RecordCall(__PRETTY_FUNCTION__);                               \
  }

#define RECORD(...)                                                            \
  repro::SBRecorder sb_recorder;                                               \
  if (auto *g = repro::Reproducer::Instance().GetGenerator()) {                \
    repro::SBProvider &p = g->GetOrCreate<repro::SBProvider>();                \
    sb_recorder.SetSerializer(&p.GetSerializer());                             \
    sb_recorder.RecordCall(__PRETTY_FUNCTION__, __VA_ARGS__);                  \
  }

#define RECORD_RETURN(t) sb_recorder.RecordReturn(t)

namespace lldb {
void GenerateReproducer();
bool ReplayReproducer();
} // namespace lldb

namespace lldb_private {
namespace repro {

class SBObjectToIndex {
public:
  SBObjectToIndex() : m_index(0) {}

  template <typename T> unsigned GetIndexForObject(T *t) {
    return GetIndexForObjectImpl((void *)t);
  }

private:
  unsigned GetIndexForObjectImpl(void *object) {
    auto it = m_mapping.find(object);
    if (it == m_mapping.end())
      m_mapping[object] = Increment();
    llvm::outs() << object << " -> " << m_mapping[object] << '\n';
    return m_mapping[object];
  }

  unsigned Increment() {
    std::lock_guard<std::mutex> guard(m_mutex);
    return ++m_index;
  }

  unsigned m_index;
  std::mutex m_mutex;
  llvm::DenseMap<void *, unsigned> m_mapping;
};

class SBIndexToObject {
public:
  template <typename T> T *GetObjectForIndex(int index) {
    void *object = GetObjectForIndexImpl(index);
    return static_cast<T *>(object);
  }

  template <typename T> void AddObjectForIndex(int index, T *object) {
    AddObjectForIndexImpl(index, static_cast<void *>(object));
  }

  template <typename T> void AddObjectForIndex(int index, T &object) {
    AddObjectForIndexImpl(index, static_cast<void *>(&object));
  }

private:
  void *GetObjectForIndexImpl(int index) {
    auto it = m_mapping.find(index);
    if (it == m_mapping.end()) {
      llvm::outs() << index << " -> nullptr" << '\n';
      return nullptr;
    }
    llvm::outs() << index << " -> " << m_mapping[index] << '\n';
    return m_mapping[index];
  }

  void AddObjectForIndexImpl(int index, void *object) {
    if (index == -1)
      return;
    llvm::outs() << index << " -> " << object << '\n';
    m_mapping[index] = object;
  }

  llvm::DenseMap<unsigned, void *> m_mapping;
};

class SBSerializer {
public:
  SBSerializer(llvm::raw_ostream &stream) : m_stream(stream) {}

  void Serialize() {}

  template <typename T, typename... Ts>
  void Serialize(const T &t, const Ts &... ts) {
    Write(t);
    Serialize(ts...);
  }

private:
  template <typename T> void Write(T *t) {
    int idx = m_tracker.GetIndexForObject(t);
    Write(idx);
  }

  template <typename T> void Write(T &t) {
    int idx = m_tracker.GetIndexForObject(&t);
    Write(idx);
  }

  void Write(std::string t) { Write(t.c_str()); }

  void Write(const char *t) {
    m_stream << t;
    m_stream.write(0x0);
  }

#define SB_SERIALIZER_POD(Type)                                                \
  void Write(Type t) {                                                         \
    m_stream.write(reinterpret_cast<const char *>(&t), sizeof(Type));          \
  }

  SB_SERIALIZER_POD(bool);
  SB_SERIALIZER_POD(char);
  SB_SERIALIZER_POD(double);
  SB_SERIALIZER_POD(float);
  SB_SERIALIZER_POD(int);
  SB_SERIALIZER_POD(long long);
  SB_SERIALIZER_POD(long);
  SB_SERIALIZER_POD(short);
  SB_SERIALIZER_POD(unsigned char);
  SB_SERIALIZER_POD(unsigned int);
  SB_SERIALIZER_POD(unsigned long long);
  SB_SERIALIZER_POD(unsigned long);
  SB_SERIALIZER_POD(unsigned short);

private:
  llvm::raw_ostream &m_stream;
  SBObjectToIndex m_tracker;
};

class SBDeserializer {
public:
  SBDeserializer(llvm::StringRef buffer) : m_buffer(buffer), m_offset(0) {}

  template <typename T> T Read() {
    T t;
    std::memcpy((char *)&t, &m_buffer.data()[m_offset], sizeof(t));
    m_offset += sizeof(t);
    return t;
  }

  bool HasData(int offset = 0) { return m_offset + offset <= m_buffer.size(); }

private:
  llvm::StringRef m_buffer;
  uint32_t m_offset;
};

template <> const char *SBDeserializer::Read<const char *>();

class SBRecorder {
public:
  SBRecorder()
      : m_serializer(nullptr), m_local_boundary(false),
        m_return_recorded(false) {
    if (!g_global_boundary) {
      g_global_boundary = true;
      m_local_boundary = true;
    }
  }

  ~SBRecorder() {
    UpdateBoundary();
    if (!m_return_recorded) {
      RecordNoReturn();
    }
  }

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
    llvm::outs() << "Capturing: " << name << '\n';
    m_serializer->Serialize(name);
    m_serializer->Serialize(args...);
  }

  template <typename T> const T &RecordReturn(const T &t) {
    UpdateBoundary();
    if (!ShouldCapture() || !ShouldSerialize()) {
      return t;
    }
    llvm::outs() << "Capturing return: " << &t << '\n';
    m_serializer->Serialize(t);
    m_return_recorded = true;
    return t;
  }

  void SetSerializer(SBSerializer *serializer) { m_serializer = serializer; }

private:
  bool ShouldCapture() { return m_local_boundary; }
  bool ShouldSerialize() { return m_serializer != nullptr; }

  void RecordNoReturn() {
    if (!ShouldCapture() || !ShouldSerialize())
      return;
    m_serializer->Serialize(-1);
  }

  SBSerializer *m_serializer;
  bool m_local_boundary;
  bool m_return_recorded;

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
      } else {
        llvm::outs() << "Replaying function call '" << f << "'\n";
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
