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

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/YAMLTraits.h"

#include <mutex>
#include <string>
#include <vector>

namespace lldb_private {
namespace repro {

class Reproducer;

enum class ReproducerMode {
  Capture,
  Replay,
  Off,
};

/// Abstraction for information associated with a provider. This information
/// is serialized into an index which is used by the loader.
struct ProviderInfo {
  std::string name;
  std::vector<std::string> files;
};

/// The provider defines an interface for generating files needed for
/// reproducing. The provider must populate its ProviderInfo to communicate
/// its name and files to the index, before registering with the generator,
/// i.e. in the constructor.
///
/// Different components will implement different providers.
class ProviderBase {
public:
  virtual ~ProviderBase() = default;

  const ProviderInfo &GetInfo() const { return m_info; }
  const FileSpec &GetRoot() const { return m_root; }

  /// The Keep method is called when it is decided that we need to keep the
  /// data in order to provide a reproducer.
  virtual void Keep(){};

  /// The Discard method is called when it is decided that we do not need to
  /// keep any information and will not generate a reproducer.
  virtual void Discard(){};

  // Returns the class ID for this type.
  static const void *ClassID() { return &ID; }

  // Returns the class ID for the dynamic type of this Provider instance.
  virtual const void *DynamicClassID() const = 0;

protected:
  ProviderBase(const FileSpec &root) : m_root(root) {}

  /// Every provider keeps track of its own files.
  ProviderInfo m_info;

private:
  /// Every provider knows where to dump its potential files.
  FileSpec m_root;

  virtual void anchor();
  static char ID;
};

template <typename ThisProviderT> class Provider : public ProviderBase {
public:
  static const void *ClassID() { return &ThisProviderT::ID; }

  const void *DynamicClassID() const override { return &ThisProviderT::ID; }

protected:
  using ProviderBase::ProviderBase; // Inherit constructor.
};

/// The generator is responsible for the logic needed to generate a
/// reproducer. For doing so it relies on providers, who serialize data that
/// is necessary for reproducing  a failure.
class Generator final {
public:
  Generator(const FileSpec &root);
  ~Generator();

  /// Method to indicate we want to keep the reproducer. If reproducer
  /// generation is disabled, this does nothing.
  void Keep();

  /// Method to indicate we do not want to keep the reproducer. This is
  /// unaffected by whether or not generation reproduction is enabled, as we
  /// might need to clean up files already written to disk.
  void Discard();

  /// Create and register a new provider.
  template <typename T> T *Create() {
    std::unique_ptr<ProviderBase> provider = llvm::make_unique<T>(m_root);
    return static_cast<T *>(Register(std::move(provider)));
  }

  /// Get an existing provider.
  template <typename T> T *Get() {
    auto it = m_providers.find(T::ClassID());
    if (it == m_providers.end())
      return nullptr;
    return static_cast<T *>(it->second.get());
  }

  /// Get a provider if it exists, otherwise create it.
  template <typename T> T &GetOrCreate() {
    auto *provider = Get<T>();
    if (provider)
      return *provider;
    return *Create<T>();
  }

  const FileSpec &GetRoot() const;

private:
  friend Reproducer;

  ProviderBase *Register(std::unique_ptr<ProviderBase> provider);

  /// Builds and index with provider info.
  void AddProvidersToIndex();

  /// Map of provider IDs to provider instances.
  llvm::DenseMap<const void *, std::unique_ptr<ProviderBase>> m_providers;
  std::mutex m_providers_mutex;

  /// The reproducer root directory.
  FileSpec m_root;

  /// Flag to ensure that we never call both keep and discard.
  bool m_done;
};

class Loader final {
public:
  Loader(const FileSpec &root);

  llvm::Optional<ProviderInfo> GetProviderInfo(llvm::StringRef name);
  llvm::Error LoadIndex();

  const FileSpec &GetRoot() const { return m_root; }

private:
  llvm::StringMap<ProviderInfo> m_provider_info;
  FileSpec m_root;
  bool m_loaded;
};

/// The reproducer enables clients to obtain access to the Generator and
/// Loader.
class Reproducer {
public:
  static Reproducer &Instance();
  static llvm::Error Initialize(ReproducerMode mode,
                                llvm::Optional<FileSpec> root);
  static void Terminate();

  Reproducer() = default;

  Generator *GetGenerator();
  Loader *GetLoader();

  const Generator *GetGenerator() const;
  const Loader *GetLoader() const;

  FileSpec GetReproducerPath() const;

protected:
  llvm::Error SetCapture(llvm::Optional<FileSpec> root);
  llvm::Error SetReplay(llvm::Optional<FileSpec> root);

private:
  static llvm::Optional<Reproducer> &InstanceImpl();

  llvm::Optional<Generator> m_generator;
  llvm::Optional<Loader> m_loader;

  mutable std::mutex m_mutex;
};

class SBObjectToIndex {
public:
  SBObjectToIndex() : m_index(0) {}

  template <typename T> unsigned GetIndexForObject(T *t) {
    return GetIndexForObjectImpl((void *)t);
  }

private:
  unsigned GetIndexForObjectImpl(void *object);
  unsigned Increment();

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
  void *GetObjectForIndexImpl(int index);
  void AddObjectForIndexImpl(int index, void *object);

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

  void Write(std::string t);
  void Write(const char *t);

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

  bool HasData(int offset = 0);

private:
  llvm::StringRef m_buffer;
  uint32_t m_offset;
};

template <> const char *SBDeserializer::Read<const char *>();

} // namespace repro
} // namespace lldb_private

LLVM_YAML_IS_DOCUMENT_LIST_VECTOR(lldb_private::repro::ProviderInfo)

namespace llvm {
namespace yaml {

template <> struct MappingTraits<lldb_private::repro::ProviderInfo> {
  static void mapping(IO &io, lldb_private::repro::ProviderInfo &info) {
    io.mapRequired("name", info.name);
    io.mapOptional("files", info.files);
  }
};
} // namespace yaml
} // namespace llvm

#endif // LLDB_UTILITY_REPRODUCER_H
