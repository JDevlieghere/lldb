//===-- Reproducer.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_REPRODUCER_H
#define LLDB_UTILITY_REPRODUCER_H

#include "lldb/Utility/FileSpec.h"
#include "llvm/Support/YAMLTraits.h"

#include <mutex>
#include <string>
#include <vector>

namespace lldb_private {

class Reproducer final {
public:
  Reproducer();
  ~Reproducer();

  static Reproducer *Instance();

  struct ProviderInfo {
    std::string name;
    std::vector<std::string> files;
  };

  /// A provider is responsible for generating the files necessary for
  /// reproduction. The implementation is free to decide how to do this as long
  /// as it communicates its files via the Provider info.
  class Provider {
  public:
    Provider(FileSpec directory) : m_directory(directory) {}
    virtual ~Provider() = default;

    ProviderInfo GetInfo() { return m_info; }
    const FileSpec &GetDirectory() { return m_directory; }

    virtual void Keep(){};
    virtual void Discard(){};

  protected:
    /// Every provider keeps track of its own files.
    ProviderInfo m_info;

  private:
    /// Every provider knows where to dump its potential files.
    FileSpec m_directory;
  };

  void SetEnabled(bool enabled);

  void Keep();
  void Discard();

  Provider &Register(std::unique_ptr<Provider> &&provider);

  /// Convenience helper to create and register a new provider.
  template <typename T> T &CreateProvider() {
    std::unique_ptr<T> provider = llvm::make_unique<T>(m_directory);
    return static_cast<T &>(Register(std::move(provider)));
  }

private:
  void AddProviderToIndex(const ProviderInfo &provider_info);

  std::mutex m_providers_mutex;
  std::vector<std::unique_ptr<Provider>> m_providers;

  FileSpec m_directory;
  bool m_enabled;
  bool m_done;

  static std::atomic<Reproducer *> g_instance;
  static std::mutex g_instance_mutex;
};

} // namespace lldb_private

LLVM_YAML_IS_DOCUMENT_LIST_VECTOR(lldb_private::Reproducer::ProviderInfo)

namespace llvm {
namespace yaml {

template <> struct MappingTraits<lldb_private::Reproducer::ProviderInfo> {
  static void mapping(IO &io, lldb_private::Reproducer::ProviderInfo &info) {
    io.mapRequired("name", info.name);
    io.mapOptional("files", info.files);
  }
};
} // namespace yaml
} // namespace llvm

#endif // LLDB_UTILITY_REPRODUCER_H
