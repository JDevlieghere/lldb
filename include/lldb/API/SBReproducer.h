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
  { llvm::outs() << __PRETTY_FUNCTION__ << '\n'; }

#define TRACE_VALUE(value)                                                     \
  { llvm::outs() << __PRETTY_FUNCTION__ << ' ' << value << '\n'; }

namespace lldb {
class SBIndexToObject {
public:
  template <typename T> T *GetObjectForIndex(int idx) {
    void *object = GetObjectForIndexImpl(idx);
    return static_cast<T *>(object);
  }

  template <typename T> void AddObjectForIndex(int idx, T *object) {
    AddObjectForIndexImpl(idx, static_cast<void *>(object));
  }

  template <typename T> void AddObjectForIndex(int idx, T &object) {
    AddObjectForIndexImpl(idx, static_cast<void *>(&object));
  }

private:
  void *GetObjectForIndexImpl(int idx) {
    auto it = m_mapping.find(idx);
    if (it == m_mapping.end()) {
      return nullptr;
    }
    return m_mapping[idx];
  }

  void AddObjectForIndexImpl(int idx, void *object) { m_mapping[idx] = object; }

  llvm::DenseMap<unsigned, void *> m_mapping;
};

class SBDeserializer {
public:
  SBDeserializer(llvm::StringRef buffer = {}) : m_buffer(buffer), m_offset(0) {}

  bool HasData(int offset = 0) { return m_offset + offset < m_buffer.size(); }

  template <typename T> T Deserialize() {
    // FIXME: This is bogus for pointers or references to fundamental types.
    return Read<T>(std::is_fundamental<T>(), std::is_pointer<T>(),
                   std::is_reference<T>());
  }

  // FIXME: We have references to this instance stored all over the place. We
  //        should find a better way to set the buffer but his will have to do
  //        for now.
  void LoadBuffer(llvm::StringRef buffer) { m_buffer = buffer; }

private:
  template <typename T>
  T Read(std::true_type, std::false_type, std::false_type) {
    T t;
    std::memcpy((char *)&t, &m_buffer.data()[m_offset], sizeof(t));
    m_offset += sizeof(t);
    return t;
  }

  template <typename T>
  T Read(std::false_type, std::true_type, std::false_type) {
    return m_index_to_object
        .template GetObjectForIndex<typename std::remove_pointer<T>::type>(
            Deserialize<unsigned>());
  }

  template <typename T>
  T Read(std::false_type, std::false_type, std::true_type) {
    return *m_index_to_object.template GetObjectForIndex<
        typename std::remove_reference<T>::type>(Deserialize<unsigned>());
  }

  llvm::StringRef m_buffer;
  SBIndexToObject m_index_to_object;
  uint32_t m_offset;
};

template <>
const char *SBDeserializer::Read<const char *>(std::true_type, std::false_type,
                                               std::false_type);

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
    DeserializationHelper<Args...>::template deserialized<Result>::doit(
        m_deserializer, f);
  }

  Result (*f)(Args...);
};

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

class SBSerializer {
public:
  SBSerializer(llvm::raw_ostream &stream = llvm::outs()) : m_stream(stream) {}

  void SerializeAll() {}

  template <typename Head, typename... Tail>
  void SerializeAll(const Head &head, const Tail &... tail) {
    Serialize(head);
    SerializeAll(tail...);
  }

private:
  template <typename T> void Serialize(T *t) {
    int idx = m_tracker.GetIndexForObject(t);
    Serialize(idx);
  }

  template <typename T> void Serialize(T &t) {
    int idx = m_tracker.GetIndexForObject(&t);
    Serialize(idx);
  }

  void Serialize(std::string t) { Serialize(t.c_str()); }

  void Serialize(const char *t) {
    TRACE_VALUE(t);
    m_stream << t;
    m_stream.write(0x0);
  }

#define SB_SERIALIZER_POD(Type)                                                \
  void Serialize(Type t) {                                                     \
    TRACE_VALUE(t);                                                            \
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

class SBRegistry {
public:
  static SBRegistry &Instance() {
    static SBRegistry g_registry;
    return g_registry;
  }

  SBRegistry() : m_id(0) { Init(); }

  /// Register a default replayer for a function.
  template <typename Signature> void Register(Signature *f, unsigned ID) {
    DoRegister(uintptr_t(f), new DefaultReplayer<Signature>(m_deserializer, f),
               ID);
  }

  bool Capture();
  bool Replay();

  SBDeserializer &GetDeserializer() { return m_deserializer; }
  SBSerializer &GetSerializer() { return m_serializer; }

  unsigned GetID(uintptr_t addr) { return m_sbreplayers[addr].second; }

private:
  void Init();

  /// Register the given replayer for a function (and the ID mapping).
  void DoRegister(uintptr_t RunID, SBReplayer *replayer, unsigned id) {
    m_sbreplayers[RunID] = std::make_pair(replayer, id);
    m_ids[id] = replayer;
  }

  SBDeserializer m_deserializer;
  SBSerializer m_serializer;

  std::map<uintptr_t, std::pair<SBReplayer *, unsigned>> m_sbreplayers;
  std::map<unsigned, SBReplayer *> m_ids;

  unsigned m_id;
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
  SBRecorder()
      : m_capture(lldb_private::repro::Reproducer::Instance().GetGenerator() !=
                  nullptr),
        m_local_boundary(false) {
    if (!g_global_boundary) {
      g_global_boundary = true;
      m_local_boundary = true;
    }
  }

  ~SBRecorder() { UpdateBoundary(); }

  /// Records a single function call.
  template <typename Result, typename... FArgs, typename... RArgs>
  void Record(Result (*f)(FArgs...), const RArgs &... args) {
    if (!ShouldCapture()) {
      return;
    }
    TRACE;

    SBRegistry &registry = SBRegistry::Instance();
    registry.GetSerializer().SerializeAll(registry.GetID(uintptr_t(f)));
    registry.GetSerializer().SerializeAll(args...);
  }

  /// Records a single function call.
  template <typename... Args>
  void Record(void (*f)(Args...), const Args &... args) {
    if (!ShouldCapture()) {
      return;
    }
    TRACE;

    SBRegistry &registry = SBRegistry::Instance();
    registry.GetSerializer().SerializeAll(registry.GetID(uintptr_t(f)));
    registry.GetSerializer().SerializeAll(args...);
  }

  /// Record the result of a function call.
  template <typename Result> Result RecordResult(const Result &r) {
    UpdateBoundary();
    if (ShouldCapture()) {
      TRACE;
      SBRegistry::Instance().GetSerializer().SerializeAll(r);
    }
    return r;
  }

private:
  void UpdateBoundary() {
    if (m_local_boundary) {
      g_global_boundary = false;
    }
  }

  bool ShouldCapture() { return m_capture && m_local_boundary; }

  bool m_capture;
  bool m_local_boundary;

  static thread_local std::atomic<bool> g_global_boundary;
};
} // namespace lldb

#define SB_RECORD_CONSTRUCTOR(Class, Signature, ...)                           \
  {                                                                            \
    SBRecorder sb_recorder;                                                    \
    sb_recorder.Record(&construct<Class Signature>::doit, __VA_ARGS__);        \
    sb_recorder.RecordResult(this);                                            \
  }

#define SB_RECORD_CONSTRUCTOR_NO_ARGS(Class)                                   \
  {                                                                            \
    SBRecorder sb_recorder;                                                    \
    sb_recorder.Record(&construct<Class()>::doit);                             \
    sb_recorder.RecordResult(this);                                            \
  }

#define SB_RECORD_METHOD(Result, Class, Method, Signature, ...)                \
  SBRecorder sb_recorder;                                                      \
  {                                                                            \
    sb_recorder.Record(                                                        \
        &invoke<Result(Class::*) Signature>::method<&Class::Method>::doit,     \
        this, __VA_ARGS__);                                                    \
  }

#define SB_RECORD_METHOD_CONST(Result, Class, Method, Signature, ...)          \
  SBRecorder sb_recorder;                                                      \
  {                                                                            \
    sb_recorder.Record(                                                        \
        &invoke<Result(Class::*)                                               \
                    Signature const>::method_const<&Class::Method>::doit,      \
        this, __VA_ARGS__);                                                    \
  }

#define SB_RECORD_METHOD_NO_ARGS(Result, Class, Method)                        \
  SBRecorder sb_recorder;                                                      \
  {                                                                            \
    sb_recorder.Record(                                                        \
        &invoke<Result (Class::*)()>::method<&Class::Method>::doit, this);     \
  }

#define SB_RECORD_METHOD_CONST_NO_ARGS(Result, Class, Method)                  \
  SBRecorder sb_recorder;                                                      \
  {                                                                            \
    sb_recorder.Record(&invoke<Result (Class::*)()                             \
                                   const>::method_const<&Class::Method>::doit, \
                       this);                                                  \
  }

#define SB_RECORD_STATIC_METHOD(Result, Class, Method, Signature, ...)         \
  SBRecorder sb_recorder;                                                      \
  {                                                                            \
    sb_recorder.Record(static_cast<Result(*) Signature>(&Class::Method),       \
                       __VA_ARGS__);                                           \
  }

#define SB_RECORD_STATIC_METHOD_NO_ARGS(Result, Class, Method)                 \
  SBRecorder sb_recorder;                                                      \
  { sb_recorder.Record(static_cast<Result (*)()>(&Class::Method)); }

#define SB_RECORD_RESULT(Result) sb_recorder.RecordResult(Result);

#endif
