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

// FIXME: Cannot include private LLDB headers in API.
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Reproducer.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/YAMLTraits.h"

#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <type_traits>

#define TRACE                                                                  \
  llvm::outs() << __PRETTY_FUNCTION__ << " (line " << __LINE__ << ')' << '\n'

template <class T> struct remove_const { typedef T type; };
template <class T> struct remove_const<const T> { typedef T type; };

namespace lldb {
class SBIndexToObject {
public:
  template <typename T> T *GetObjectForIndex(int idx) {
    assert(idx != 0 && "Cannot get object for sentinel");
    void *object = GetObjectForIndexImpl(idx);
    return static_cast<typename remove_const<T>::type *>(object);
  }

  template <typename T> void AddObjectForIndex(int idx, T *object) {
    AddObjectForIndexImpl(
        idx, static_cast<void *>(
                 const_cast<typename remove_const<T>::type *>(object)));
  }

  template <typename T> void AddObjectForIndex(int idx, T &object) {
    AddObjectForIndexImpl(
        idx, static_cast<void *>(
                 const_cast<typename remove_const<T>::type *>(&object)));
  }

private:
  void *GetObjectForIndexImpl(int idx) {
    auto it = m_mapping.find(idx);
    if (it == m_mapping.end()) {
      llvm::outs() << "Mapping object to index: " << idx << " -> nullptr\n";
      return nullptr;
    }
    llvm::outs() << "Mapping object to index: " << idx << " -> "
                 << m_mapping[idx] << '\n';
    return m_mapping[idx];
  }

  void AddObjectForIndexImpl(int idx, void *object) {
    assert(idx != 0 && "Cannot add object for sentinel");
    llvm::outs() << "Adding object to index: " << object << " -> " << idx
                 << '\n';
    m_mapping[idx] = object;
  }

  llvm::DenseMap<unsigned, void *> m_mapping;
};

template <unsigned> struct SBTag {};
typedef SBTag<0> PointerTag;
typedef SBTag<1> ReferenceTag;
typedef SBTag<2> ValueTag;
typedef SBTag<3> FundamentalPointerTag;
typedef SBTag<4> FundamentalReferenceTag;

template <class T> struct serializer_tag { typedef ValueTag type; };
template <class T> struct serializer_tag<T *> {
  typedef
      typename std::conditional<std::is_fundamental<T>::value,
                                FundamentalPointerTag, PointerTag>::type type;
};
template <class T> struct serializer_tag<T &> {
  typedef typename std::conditional<std::is_fundamental<T>::value,
                                    FundamentalReferenceTag, ReferenceTag>::type
      type;
};

class SBDeserializer {
public:
  SBDeserializer(llvm::StringRef buffer = {}) : m_buffer(buffer), m_offset(0) {}

  bool HasData(int offset = 0) { return m_offset + offset < m_buffer.size(); }

  template <typename T> T Deserialize() {
    TRACE;
    // FIXME: This is bogus for pointers or references to fundamental types.
    return Read<T>(typename serializer_tag<T>::type());
  }

  template <typename T> void HandleReplayResult(const T &t) {
    unsigned result = Deserialize<unsigned>();
    if (std::is_fundamental<T>::value)
      return;
    // We need to make a copy as the original object might go out of scope.
    m_index_to_object.AddObjectForIndex(result, new T(t));
  }

  template <typename T> void HandleReplayResult(T *t) {
    unsigned result = Deserialize<unsigned>();
    if (std::is_fundamental<T>::value)
      return;
    m_index_to_object.AddObjectForIndex(result, t);
  }

  void HandleReplayResultVoid() {
    unsigned result = Deserialize<unsigned>();
    assert(result == 0);
  }

  // FIXME: We have references to this instance stored all over the place. We
  //        should find a better way to set the buffer but his will have to do
  //        for now.
  void LoadBuffer(llvm::StringRef buffer) { m_buffer = buffer; }

private:
  template <typename T> T Read(ValueTag) {
    T t;
    std::memcpy((char *)&t, &m_buffer.data()[m_offset], sizeof(t));
    m_offset += sizeof(t);
    return t;
  }

  template <typename T> T Read(PointerTag) {
    typedef typename std::remove_pointer<T>::type UnderlyingT;
    return m_index_to_object.template GetObjectForIndex<UnderlyingT>(
        Deserialize<unsigned>());
  }

  template <typename T> T Read(ReferenceTag) {
    typedef typename std::remove_reference<T>::type UnderlyingT;
    // If this is a reference to a fundamental type we just read its value.
    return *m_index_to_object.template GetObjectForIndex<UnderlyingT>(
        Deserialize<unsigned>());
  }

  // This method is used to parse references to fundamental types. Because
  // they're not recorded in the object table we have serialized their value.
  // We read its value, allocate a copy on the heap, and return a pointer to
  // the copy.
  template <typename T> T Read(FundamentalPointerTag) {
    typedef typename std::remove_pointer<T>::type UnderlyingT;
    return new UnderlyingT(Deserialize<UnderlyingT>());
  }

  // This method is used to parse references to fundamental types. Because
  // they're not recorded in the object table we have serialized their value.
  // We read its value, allocate a copy on the heap, and return a reference to
  // the copy.
  template <typename T> T Read(FundamentalReferenceTag) {
    // If this is a reference to a fundamental type we just read its value.
    typedef typename std::remove_reference<T>::type UnderlyingT;
    return *(new UnderlyingT(Deserialize<UnderlyingT>()));
  }

  llvm::StringRef m_buffer;
  SBIndexToObject m_index_to_object;
  uint32_t m_offset;
};

template <> const char *SBDeserializer::Deserialize<const char *>();

/// Helpers to auto-synthesize function replay code.
template <typename... Remaining> struct DeserializationHelper;

template <typename Head, typename... Tail>
struct DeserializationHelper<Head, Tail...> {
  template <typename Result, typename... Deserialized> struct deserialized {
    static Result doit(SBDeserializer &deserializer,
                       Result (*f)(Deserialized..., Head, Tail...),
                       Deserialized... d) {
      return DeserializationHelper<Tail...>::
          template deserialized<Result, Deserialized..., Head>::doit(
              deserializer, f, d..., deserializer.Deserialize<Head>());
    }
  };
};

template <> struct DeserializationHelper<> {
  template <typename Result, typename... Deserialized> struct deserialized {
    static Result doit(SBDeserializer &deserializer,
                       Result (*f)(Deserialized...), Deserialized... d) {
      return f(d...);
    }
  };
};

/// An abstract replayer interface. If you need custom replay handling, inherit
/// from this. Otherwise, just use the default replayer.
struct SBReplayer {
  SBReplayer(SBDeserializer &deserializer) : m_deserializer(deserializer) {}
  virtual ~SBReplayer() {}
  virtual void operator()() const = 0;

protected:
  SBDeserializer &m_deserializer;
};

template <typename Signature> struct DefaultReplayer;
template <typename Result, typename... Args>
struct DefaultReplayer<Result(Args...)> : public SBReplayer {
  DefaultReplayer(SBDeserializer &deserializer, Result (*f)(Args...))
      : SBReplayer(deserializer), f(f) {}

  void operator()() const override {
    m_deserializer.HandleReplayResult(
        DeserializationHelper<Args...>::template deserialized<Result>::doit(
            m_deserializer, f));
  }

  Result (*f)(Args...);
};

template <typename... Args>
struct DefaultReplayer<void(Args...)> : public SBReplayer {
  DefaultReplayer(SBDeserializer &deserializer, void (*f)(Args...))
      : SBReplayer(deserializer), f(f) {}

  void operator()() const override {
    DeserializationHelper<Args...>::template deserialized<void>::doit(
        m_deserializer, f);
    m_deserializer.HandleReplayResultVoid();
  }

  void (*f)(Args...);
};

template <typename Signature> struct ForwardingReplayer;
template <typename Result, typename... Args>
struct ForwardingReplayer<Result(Args...)> : public SBReplayer {
  ForwardingReplayer(SBDeserializer &deserializer, Result (*f)(Args...),
                     Result (*g)(Args...))
      : SBReplayer(deserializer), f(f), g(g) {}

  void operator()() const override {
    m_deserializer.HandleReplayResult(
        DeserializationHelper<Args...>::template deserialized<Result>::doit(
            m_deserializer, f));
  }

  Result (*f)(Args...);
  Result (*g)(Args...);
};

template <typename... Args>
struct ForwardingReplayer<void(Args...)> : public SBReplayer {
  ForwardingReplayer(SBDeserializer &deserializer, void (*f)(Args...),
                     void (*g)(Args...))
      : SBReplayer(deserializer), f(f), g(g) {}

  void operator()() const override {
    DeserializationHelper<Args...>::template deserialized<void>::doit(
        m_deserializer, f);
    m_deserializer.HandleReplayResultVoid();
  }

  void (*f)(Args...);
  void (*g)(Args...);
};

class SBObjectToIndex {
public:
  SBObjectToIndex() : m_index(1) {}

  template <typename T> unsigned GetIndexForObject(T *t) {
    return GetIndexForObjectImpl((void *)t);
  }

private:
  unsigned GetIndexForObjectImpl(void *object) {
    auto it = m_mapping.find(object);
    if (it == m_mapping.end())
      m_mapping[object] = Increment();
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

class SBRegistry {
public:
  static SBRegistry &Instance() {
    static SBRegistry g_registry;
    return g_registry;
  }

  SBRegistry() : m_id(1) { Init(); }

  /// Register a default replayer for a function.
  template <typename Signature> void Register(Signature *f, unsigned ID) {
    DoRegister(uintptr_t(f), new DefaultReplayer<Signature>(m_deserializer, f),
               ID);
  }

  /// Register a replayer that forwards to another function with the same
  /// signature.
  template <typename Signature>
  void Register(Signature *f, unsigned ID, Signature *g) {
    DoRegister(uintptr_t(f),
               new ForwardingReplayer<Signature>(m_deserializer, f, g), ID);
  }

  /// Register a custom replayer for a function.
  template <typename Signature, template <typename> class Replayer>
  void RegisterCustom(Signature *f, unsigned ID) {
    DoRegister(uintptr_t(f), new Replayer<Signature>(m_deserializer, f), ID);
  }

  bool Replay();

  unsigned GetID(uintptr_t addr) {
    unsigned id = m_sbreplayers[addr].second;
    assert(id != 0);
    return id;
  }

private:
  void Init();

  /// Register the given replayer for a function (and the ID mapping).
  void DoRegister(uintptr_t RunID, SBReplayer *replayer, unsigned id) {
    m_sbreplayers[RunID] = std::make_pair(replayer, id);
    m_ids[id] = replayer;
  }

  // The deserializer is inherently coupled with the registry.
  SBDeserializer m_deserializer;

  std::map<uintptr_t, std::pair<SBReplayer *, unsigned>> m_sbreplayers;
  std::map<unsigned, SBReplayer *> m_ids;

  unsigned m_id;
};

class SBSerializer {
public:
  SBSerializer(llvm::raw_ostream &stream = llvm::outs()) : m_stream(stream) {}

  void SerializeID(uintptr_t addr) {
    SerializeAll(SBRegistry::Instance().GetID(addr));
  }

  void SerializeAll() {}

  template <typename Head, typename... Tail>
  void SerializeAll(const Head &head, const Tail &... tail) {
    Serialize(head);
    SerializeAll(tail...);
  }

private:
  template <typename T> void Serialize(T *t) {
    if (std::is_fundamental<T>::value) {
      Serialize(*t);
    } else {
      int idx = m_tracker.GetIndexForObject(t);
      Serialize(idx);
    }
  }

  template <typename T> void Serialize(T &t) {
    if (std::is_fundamental<T>::value) {
      Serialize(t);
    } else {
      int idx = m_tracker.GetIndexForObject(&t);
      Serialize(idx);
    }
  }

  void Serialize(std::string t) { Serialize(t.c_str()); }

  void Serialize(void *v) {
    // Do nothing.
  }

  void Serialize(const char *t) {
    TRACE << t << '\n';
    m_stream << t;
    m_stream.write(0x0);
  }

#define SB_SERIALIZER_POD(Type)                                                \
  void Serialize(Type t) {                                                     \
    TRACE << t << '\n';                                                        \
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

  llvm::raw_ostream &m_stream;
  SBObjectToIndex m_tracker;
};

/// To be used as the "Runtime ID" of a constructor. It also invokes the
/// constructor when called.
template <typename Signature> struct construct;
template <typename Class, typename... Args> struct construct<Class(Args...)> {
  static Class *doit(Args... args) { return new Class(args...); }
};

/// To be used as the "Runtime ID" of a member function. It also invokes the
/// member function when called.
template <typename Signature> struct invoke;
template <typename Result, typename Class, typename... Args>
struct invoke<Result (Class::*)(Args...)> {
  template <Result (Class::*m)(Args...)> struct method {
    static Result doit(Class *c, Args... args) { return (c->*m)(args...); }
  };
};

template <typename Result, typename Class, typename... Args>
struct invoke<Result (Class::*)(Args...) const> {
  template <Result (Class::*m)(Args...) const> struct method_const {
    static Result doit(Class *c, Args... args) { return (c->*m)(args...); }
  };
};

template <typename Class, typename... Args>
struct invoke<void (Class::*)(Args...)> {
  template <void (Class::*m)(Args...)> struct method {
    static void doit(Class *c, Args... args) { (c->*m)(args...); }
  };
};

class SBRecorder {
public:
  SBRecorder(llvm::StringRef pretty_func = "")
      : m_pretty_func(pretty_func), m_serializer(nullptr),
        m_local_boundary(false), m_result_recorded(false) {
    if (!g_global_boundary) {
      g_global_boundary = true;
      m_local_boundary = true;
    }
  }

  ~SBRecorder() {
    UpdateBoundary();
    RecordOmittedResult();
  }

  void SetSerializer(SBSerializer &serializer) { m_serializer = &serializer; }

  /// Records a single function call.
  template <typename Result, typename... FArgs, typename... RArgs>
  void Record(Result (*f)(FArgs...), const RArgs &... args) {
    if (!ShouldCapture()) {
      return;
    }
    if (!m_pretty_func.empty())
      llvm::outs() << "Recording '" << m_pretty_func << "' \n";
    m_serializer->SerializeID(uintptr_t(f));
    m_serializer->SerializeAll(args...);
  }

  /// Records a single function call.
  template <typename... Args>
  void Record(void (*f)(Args...), const Args &... args) {
    if (!ShouldCapture()) {
      return;
    }
    if (!m_pretty_func.empty())
      llvm::outs() << "Recording '" << m_pretty_func << "' \n";
    m_serializer->SerializeID(uintptr_t(f));
    m_serializer->SerializeAll(args...);
  }

  /// Record the result of a function call.
  template <typename Result> Result RecordResult(const Result &r) {
    UpdateBoundary();
    if (ShouldCapture()) {
      TRACE;
      m_serializer->SerializeAll(r);
      m_result_recorded = true;
    }
    return r;
  }

  void RecordOmittedResult() {
    if (m_result_recorded)
      return;
    if (!ShouldCapture())
      return;

    m_serializer->SerializeAll(0);
    m_result_recorded = true;
  }

private:
  void UpdateBoundary() {
    if (m_local_boundary) {
      g_global_boundary = false;
    }
  }

  bool ShouldCapture() { return m_serializer && m_local_boundary; }

  // FIXME: For debugging only.
  llvm::StringRef m_pretty_func;

  SBSerializer *m_serializer;
  bool m_local_boundary;
  bool m_result_recorded;

  static std::atomic<bool> g_global_boundary;
};
} // namespace lldb

#define SB_RECORD_CONSTRUCTOR(Class, Signature, ...)                           \
  if (auto *g = lldb_private::repro::Reproducer::Instance().GetGenerator()) {  \
    SBRecorder sb_recorder(__PRETTY_FUNCTION__);                               \
    sb_recorder.SetSerializer(                                                 \
        g->GetOrCreate<repro::SBProvider>().GetSerializer());                  \
    sb_recorder.Record(&construct<Class Signature>::doit, __VA_ARGS__);        \
    sb_recorder.RecordResult(this);                                            \
  }

#define SB_RECORD_CONSTRUCTOR_NO_ARGS(Class)                                   \
  if (auto *g = lldb_private::repro::Reproducer::Instance().GetGenerator()) {  \
    SBRecorder sb_recorder(__PRETTY_FUNCTION__);                               \
    sb_recorder.SetSerializer(                                                 \
        g->GetOrCreate<repro::SBProvider>().GetSerializer());                  \
    sb_recorder.Record(&construct<Class()>::doit);                             \
    sb_recorder.RecordResult(this);                                            \
  }

#define SB_RECORD_METHOD(Result, Class, Method, Signature, ...)                \
  SBRecorder sb_recorder(__PRETTY_FUNCTION__);                                 \
  if (auto *g = lldb_private::repro::Reproducer::Instance().GetGenerator()) {  \
    sb_recorder.SetSerializer(                                                 \
        g->GetOrCreate<repro::SBProvider>().GetSerializer());                  \
    sb_recorder.Record(                                                        \
        &invoke<Result(Class::*) Signature>::method<&Class::Method>::doit,     \
        this, __VA_ARGS__);                                                    \
  }

#define SB_RECORD_METHOD_CONST(Result, Class, Method, Signature, ...)          \
  SBRecorder sb_recorder(__PRETTY_FUNCTION__);                                 \
  if (auto *g = lldb_private::repro::Reproducer::Instance().GetGenerator()) {  \
    sb_recorder.SetSerializer(                                                 \
        g->GetOrCreate<repro::SBProvider>().GetSerializer());                  \
    sb_recorder.Record(                                                        \
        &invoke<Result(Class::*)                                               \
                    Signature const>::method_const<&Class::Method>::doit,      \
        this, __VA_ARGS__);                                                    \
  }

#define SB_RECORD_METHOD_NO_ARGS(Result, Class, Method)                        \
  SBRecorder sb_recorder(__PRETTY_FUNCTION__);                                 \
  if (auto *g = lldb_private::repro::Reproducer::Instance().GetGenerator()) {  \
    sb_recorder.SetSerializer(                                                 \
        g->GetOrCreate<repro::SBProvider>().GetSerializer());                  \
    sb_recorder.Record(                                                        \
        &invoke<Result (Class::*)()>::method<&Class::Method>::doit, this);     \
  }

#define SB_RECORD_METHOD_CONST_NO_ARGS(Result, Class, Method)                  \
  SBRecorder sb_recorder(__PRETTY_FUNCTION__);                                 \
  if (auto *g = lldb_private::repro::Reproducer::Instance().GetGenerator()) {  \
    sb_recorder.SetSerializer(                                                 \
        g->GetOrCreate<repro::SBProvider>().GetSerializer());                  \
    sb_recorder.Record(&invoke<Result (Class::*)()                             \
                                   const>::method_const<&Class::Method>::doit, \
                       this);                                                  \
  }

#define SB_RECORD_STATIC_METHOD(Result, Class, Method, Signature, ...)         \
  SBRecorder sb_recorder(__PRETTY_FUNCTION__);                                 \
  if (auto *g = lldb_private::repro::Reproducer::Instance().GetGenerator()) {  \
    sb_recorder.SetSerializer(                                                 \
        g->GetOrCreate<repro::SBProvider>().GetSerializer());                  \
    sb_recorder.Record(static_cast<Result(*) Signature>(&Class::Method),       \
                       __VA_ARGS__);                                           \
  }

#define SB_RECORD_STATIC_METHOD_NO_ARGS(Result, Class, Method)                 \
  SBRecorder sb_recorder(__PRETTY_FUNCTION__);                                 \
  if (auto *g = lldb_private::repro::Reproducer::Instance().GetGenerator()) {  \
    sb_recorder.SetSerializer(                                                 \
        g->GetOrCreate<repro::SBProvider>().GetSerializer());                  \
    sb_recorder.Record(static_cast<Result (*)()>(&Class::Method));             \
  }

#define SB_RECORD_RESULT(Result) sb_recorder.RecordResult(Result);

namespace lldb_private {
namespace repro {

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

  lldb::SBSerializer &GetSerializer() { return m_serializer; }
  static char ID;

private:
  std::error_code m_ec;
  llvm::raw_fd_ostream m_stream;
  lldb::SBSerializer m_serializer;
};

} // namespace repro
} // namespace lldb_private

#endif
