//===-- SBReproducerPrivate.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBREPRODUCER_PRIVATE_H
#define LLDB_API_SBREPRODUCER_PRIVATE_H

#include "lldb/API/SBReproducer.h"

#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Reproducer.h"

#include "llvm/ADT/DenseMap.h"

#include <mutex>

namespace lldb_private {
namespace repro {

/// Mapping between serialized indices and their corresponding objects.
///
/// This class is used during replay to map indices back to in-memory objects.
///
/// When objects are constructed, they are added to this mapping using
/// AddObjectForIndex.
///
/// When an object is passed to a function, its index is deserialized and
/// AddObjectForIndex returns the corresponding object. If there is no object
/// for the given index, a nullptr is returend. The latter is valid when custom
/// replay code is in place and the actual object is ignored.
class SBIndexToObject {
public:
  /// Returns an object as a pointer for the given index or nullptr if not
  /// present in the map.
  template <typename T> T *GetObjectForIndex(int idx) {
    assert(idx != 0 && "Cannot get object for sentinel");
    void *object = GetObjectForIndexImpl(idx);
    return static_cast<typename std::remove_const<T>::type *>(object);
  }

  /// Adds a pointer to an object to the mapping for the given index.
  template <typename T> void AddObjectForIndex(int idx, T *object) {
    AddObjectForIndexImpl(
        idx, static_cast<void *>(
                 const_cast<typename std::remove_const<T>::type *>(object)));
  }

  /// Adds a reference to an object to the mapping for the given index.
  template <typename T> void AddObjectForIndex(int idx, T &object) {
    AddObjectForIndexImpl(
        idx, static_cast<void *>(
                 const_cast<typename std::remove_const<T>::type *>(&object)));
  }

private:
  /// Helper method that does the actual lookup. The void* result is later cast
  /// by the caller.
  void *GetObjectForIndexImpl(int idx) {
    auto it = m_mapping.find(idx);
    if (it == m_mapping.end()) {
      return nullptr;
    }
    return m_mapping[idx];
  }

  /// Helper method that does the actual insertion.
  void AddObjectForIndexImpl(int idx, void *object) {
    assert(idx != 0 && "Cannot add object for sentinel");
    m_mapping[idx] = object;
  }

  /// Keeps a mapping between indices and their corresponding object.
  llvm::DenseMap<unsigned, void *> m_mapping;
};

/// Base class for tag dispatch used in the SBDeserializer. Different tags are
/// instantiated with different values.
template <unsigned> struct SBTag {};

/// We need to differentiate between pointers to fundamental and
/// non-fundamental types. See the corresponding SBDeserializer::Read method
/// for the reason why.
typedef SBTag<0> PointerTag;
typedef SBTag<1> ReferenceTag;
typedef SBTag<2> ValueTag;
typedef SBTag<3> FundamentalPointerTag;
typedef SBTag<4> FundamentalReferenceTag;

/// Return the deserialization tag for the given type T.
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

/// Deserializes data from a buffer. It is used to deserialize function indices
/// to replay, their arguments and return values.
///
/// Fundamental types and strings are read by value. Objects are read by their
/// index, which get translated by the SBIndexToObject mapping maintained in
/// this class.
///
/// Additional bookkeeping with regards to the SBIndexToObject is required to
/// deserialize objects. When a constructor is run or an object is returned by
/// value, we need to capture the object and add it to the index together with
/// its index. This is the job of HandleReplayResult(Void).
class SBDeserializer {
public:
  SBDeserializer(llvm::StringRef buffer = {}) : m_buffer(buffer), m_offset(0) {}

  /// Returns true when the buffer has unread data.
  bool HasData(int offset = 0) { return m_offset + offset < m_buffer.size(); }

  /// Deserialize and interpret value as T.
  template <typename T> T Deserialize() {
    return Read<T>(typename serializer_tag<T>::type());
  }

  /// Store the returned value in the index-to-object mapping.
  template <typename T> void HandleReplayResult(const T &t) {
    unsigned result = Deserialize<unsigned>();
    if (std::is_fundamental<T>::value)
      return;
    // We need to make a copy as the original object might go out of scope.
    m_index_to_object.AddObjectForIndex(result, new T(t));
  }

  /// Store the returned value in the index-to-object mapping.
  template <typename T> void HandleReplayResult(T *t) {
    unsigned result = Deserialize<unsigned>();
    if (std::is_fundamental<T>::value)
      return;
    m_index_to_object.AddObjectForIndex(result, t);
  }

  /// All returned types are recorded, even when the function returns a void.
  /// The latter requires special handling.
  void HandleReplayResultVoid() {
    unsigned result = Deserialize<unsigned>();
    assert(result == 0);
  }

  // FIXME: We have references to this instance stored in replayer instance. We
  // should find a better way to swap out the buffer after this instance has
  // been created, but his will have to do for now.
  void LoadBuffer(llvm::StringRef buffer) {
    m_buffer = buffer;
    m_offset = 0;
  }

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

  /// This method is used to parse references to fundamental types. Because
  /// they're not recorded in the object table we have serialized their value.
  /// We read its value, allocate a copy on the heap, and return a pointer to
  /// the copy.
  template <typename T> T Read(FundamentalPointerTag) {
    typedef typename std::remove_pointer<T>::type UnderlyingT;
    return new UnderlyingT(Deserialize<UnderlyingT>());
  }

  /// This method is used to parse references to fundamental types. Because
  /// they're not recorded in the object table we have serialized their value.
  /// We read its value, allocate a copy on the heap, and return a reference to
  /// the copy.
  template <typename T> T Read(FundamentalReferenceTag) {
    // If this is a reference to a fundamental type we just read its value.
    typedef typename std::remove_reference<T>::type UnderlyingT;
    return *(new UnderlyingT(Deserialize<UnderlyingT>()));
  }

  /// Mapping of indices to objects.
  SBIndexToObject m_index_to_object;

  /// Buffer containing the serialized data.
  llvm::StringRef m_buffer;

  /// Current offset in the buffer.
  uint32_t m_offset;
};

/// Partial specialization for C-style strings. We read the string value
/// instead of treating it as pointer.
template <> const char *SBDeserializer::Deserialize<const char *>();

/// Helpers to auto-synthesize function replay code. It deserializes the replay
/// function's arguments one by one and finally calls the corresponding
/// function.
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

/// The replayer interface.
struct SBReplayer {
  SBReplayer(SBDeserializer &deserializer) : m_deserializer(deserializer) {}
  virtual ~SBReplayer() {}
  virtual void operator()() const = 0;

protected:
  SBDeserializer &m_deserializer;
};

/// The default replayer deserializes the arguments and calls the function.
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

/// Partial specialization for function returning a void type. It ignores the
/// (absent) return value.
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

/// The custom replayer is similar to the default replayer but doesn't invoke
/// the original function directly, but a different function with the same
/// interface. The allows us to intercept replay calls and have custom
/// implementation, for example to ignore a particular argument.
template <typename Signature> struct CustomReplayer;
template <typename Result, typename... Args>
struct CustomReplayer<Result(Args...)> : public SBReplayer {
  CustomReplayer(SBDeserializer &deserializer, Result (*f)(Args...),
                 Result (*g)(Args...))
      : SBReplayer(deserializer), f(f), g(g) {}

  void operator()() const override {
    m_deserializer.HandleReplayResult(
        DeserializationHelper<Args...>::template deserialized<Result>::doit(
            m_deserializer, f));
  }

  /// The replayed function.
  Result (*f)(Args...);
  /// A custom function.
  Result (*g)(Args...);
};

/// Partial specialization for function returning a void type.
template <typename... Args>
struct CustomReplayer<void(Args...)> : public SBReplayer {
  CustomReplayer(SBDeserializer &deserializer, void (*f)(Args...),
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

/// The registry contains a unique mapping between functions and their ID. The
/// IDs can be serialized and deserialized to replay a function. Functions need
/// to be registered with the registry for this to work.
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

  /// Register a replayer that invokes a custom function with the same
  /// signature as the replayed function.
  template <typename Signature>
  void Register(Signature *f, unsigned ID, Signature *g) {
    DoRegister(uintptr_t(f),
               new CustomReplayer<Signature>(m_deserializer, f, g), ID);
  }

  /// Replay functions from a file.
  bool Replay(const FileSpec &file);

  /// Returns the ID for a given function address.
  unsigned GetID(uintptr_t addr) {
    unsigned id = m_sbreplayers[addr].second;
    assert(id != 0);
    return id;
  }

private:
  /// Initialize the registry by registering function.
  void Init();

  /// Register the given replayer for a function (and the ID mapping).
  void DoRegister(uintptr_t RunID, SBReplayer *replayer, unsigned id) {
    m_sbreplayers[RunID] = std::make_pair(replayer, id);
    m_ids[id] = replayer;
  }

  // The deserializer is tightly coupled with the registry.
  SBDeserializer m_deserializer;

  /// Mapping of function addresses to replayers and their ID.
  std::map<uintptr_t, std::pair<SBReplayer *, unsigned>> m_sbreplayers;

  /// Mapping of IDs to replayer instances.
  std::map<unsigned, SBReplayer *> m_ids;

  /// Unique ID for every function registered with the registry.
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

/// Maps an object to an index for serialization. Indices are unique and
/// incremented for every new object.
///
/// Indices start at 1 in order to differentiate with an invalid index (0) in
/// the serialized buffer.
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

/// Serializes functions, their arguments and their return type to a stream.
class SBSerializer {
public:
  SBSerializer(llvm::raw_ostream &stream = llvm::outs()) : m_stream(stream) {}

  /// Serialize a function ID.
  void SerializeID(uintptr_t addr) {
    SerializeAll(SBRegistry::Instance().GetID(addr));
  }

  /// Recursively serialize all the given arguments.
  template <typename Head, typename... Tail>
  void SerializeAll(const Head &head, const Tail &... tail) {
    Serialize(head);
    SerializeAll(tail...);
  }

  void SerializeAll() {}

private:
  /// Serialize pointers. We need to differentiate between pointers to
  /// fundamental types (in which case we serialize its value) and pointer to
  /// objects (in which case we serialize their index).
  template <typename T> void Serialize(T *t) {
    if (std::is_fundamental<T>::value) {
      Serialize(*t);
    } else {
      int idx = m_tracker.GetIndexForObject(t);
      Serialize(idx);
    }
  }

  /// Serialize references. We need to differentiate between references to
  /// fundamental types (in which case we serialize its value) and references
  /// to objects (in which case we serialize their index).
  template <typename T> void Serialize(T &t) {
    if (std::is_fundamental<T>::value) {
      Serialize(t);
    } else {
      int idx = m_tracker.GetIndexForObject(&t);
      Serialize(idx);
    }
  }

  void Serialize(void *v) {
    // Do nothing.
  }

  void Serialize(const char *t) {
    m_stream << t;
    m_stream.write(0x0);
  }

#define SB_SERIALIZER_POD(Type)                                                \
  void Serialize(Type t) {                                                     \
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

  /// Serialization stream.
  llvm::raw_ostream &m_stream;

  /// Mapping of objects to indices.
  SBObjectToIndex m_tracker;
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

/// RAII object that tracks the function invocations and their return value.
///
/// API calls are only captured when the API boundary is crossed. Once we're in
/// the API layer, and another API function is called, it doesn't need to be
/// recorded.
///
/// When a call is recored, its result is always recorded as well, even if the
/// function returns a void. For functions that return by value, RecordResult
/// should be used. Otherwise a sentinel value (0) will be serialized.
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
    m_serializer->SerializeID(uintptr_t(f));
    m_serializer->SerializeAll(args...);
  }

  /// Records a single function call.
  template <typename... Args>
  void Record(void (*f)(Args...), const Args &... args) {
    if (!ShouldCapture()) {
      return;
    }
    m_serializer->SerializeID(uintptr_t(f));
    m_serializer->SerializeAll(args...);
  }

  /// Record the result of a function call.
  template <typename Result> Result RecordResult(const Result &r) {
    UpdateBoundary();
    if (ShouldCapture()) {
      m_serializer->SerializeAll(r);
      m_result_recorded = true;
    }
    return r;
  }

  /// Serialize an omitted return value.
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

  /// Pretty function for logging.
  llvm::StringRef m_pretty_func;

  /// The serializer is set from the reproducer framework. If the serializer is
  /// not set, we're not in recording mode.
  SBSerializer *m_serializer;

  /// Whether this function call was the one crossing the API boundary.
  bool m_local_boundary;

  /// Whether the return value was recorded explicitly.
  bool m_result_recorded;

  /// Whether we're currently across the API boundary.
  static std::atomic<bool> g_global_boundary;
};

} // namespace repro
} // namespace lldb_private

#define SB_RECORD_CONSTRUCTOR(Class, Signature, ...)                           \
  if (auto *g = lldb_private::repro::Reproducer::Instance().GetGenerator()) {  \
    lldb_private::repro::SBRecorder sb_recorder(__PRETTY_FUNCTION__);          \
    sb_recorder.SetSerializer(                                                 \
        g->GetOrCreate<repro::SBProvider>().GetSerializer());                  \
    sb_recorder.Record(&lldb_private::repro::construct<Class Signature>::doit, \
                       __VA_ARGS__);                                           \
    sb_recorder.RecordResult(this);                                            \
  }

#define SB_RECORD_CONSTRUCTOR_NO_ARGS(Class)                                   \
  if (auto *g = lldb_private::repro::Reproducer::Instance().GetGenerator()) {  \
    lldb_private::repro::SBRecorder sb_recorder(__PRETTY_FUNCTION__);          \
    sb_recorder.SetSerializer(                                                 \
        g->GetOrCreate<repro::SBProvider>().GetSerializer());                  \
    sb_recorder.Record(&lldb_private::repro::construct<Class()>::doit);        \
    sb_recorder.RecordResult(this);                                            \
  }

#define SB_RECORD_METHOD(Result, Class, Method, Signature, ...)                \
  lldb_private::repro::SBRecorder sb_recorder(__PRETTY_FUNCTION__);            \
  if (auto *g = lldb_private::repro::Reproducer::Instance().GetGenerator()) {  \
    sb_recorder.SetSerializer(                                                 \
        g->GetOrCreate<repro::SBProvider>().GetSerializer());                  \
    sb_recorder.Record(&lldb_private::repro::invoke<Result(                    \
                           Class::*) Signature>::method<&Class::Method>::doit, \
                       this, __VA_ARGS__);                                     \
  }

#define SB_RECORD_METHOD_CONST(Result, Class, Method, Signature, ...)          \
  lldb_private::repro::SBRecorder sb_recorder(__PRETTY_FUNCTION__);            \
  if (auto *g = lldb_private::repro::Reproducer::Instance().GetGenerator()) {  \
    sb_recorder.SetSerializer(                                                 \
        g->GetOrCreate<repro::SBProvider>().GetSerializer());                  \
    sb_recorder.Record(                                                        \
        &lldb_private::repro::invoke<Result(                                   \
            Class::*) Signature const>::method_const<&Class::Method>::doit,    \
        this, __VA_ARGS__);                                                    \
  }

#define SB_RECORD_METHOD_NO_ARGS(Result, Class, Method)                        \
  lldb_private::repro::SBRecorder sb_recorder(__PRETTY_FUNCTION__);            \
  if (auto *g = lldb_private::repro::Reproducer::Instance().GetGenerator()) {  \
    sb_recorder.SetSerializer(                                                 \
        g->GetOrCreate<repro::SBProvider>().GetSerializer());                  \
    sb_recorder.Record(&lldb_private::repro::invoke<Result (                   \
                           Class::*)()>::method<&Class::Method>::doit,         \
                       this);                                                  \
  }

#define SB_RECORD_METHOD_CONST_NO_ARGS(Result, Class, Method)                  \
  lldb_private::repro::SBRecorder sb_recorder(__PRETTY_FUNCTION__);            \
  if (auto *g = lldb_private::repro::Reproducer::Instance().GetGenerator()) {  \
    sb_recorder.SetSerializer(                                                 \
        g->GetOrCreate<repro::SBProvider>().GetSerializer());                  \
    sb_recorder.Record(                                                        \
        &lldb_private::repro::invoke<Result (                                  \
            Class::*)() const>::method_const<&Class::Method>::doit,            \
        this);                                                                 \
  }

#define SB_RECORD_STATIC_METHOD(Result, Class, Method, Signature, ...)         \
  lldb_private::repro::SBRecorder sb_recorder(__PRETTY_FUNCTION__);            \
  if (auto *g = lldb_private::repro::Reproducer::Instance().GetGenerator()) {  \
    sb_recorder.SetSerializer(                                                 \
        g->GetOrCreate<repro::SBProvider>().GetSerializer());                  \
    sb_recorder.Record(static_cast<Result(*) Signature>(&Class::Method),       \
                       __VA_ARGS__);                                           \
  }

#define SB_RECORD_STATIC_METHOD_NO_ARGS(Result, Class, Method)                 \
  lldb_private::repro::SBRecorder sb_recorder(__PRETTY_FUNCTION__);            \
  if (auto *g = lldb_private::repro::Reproducer::Instance().GetGenerator()) {  \
    sb_recorder.SetSerializer(                                                 \
        g->GetOrCreate<repro::SBProvider>().GetSerializer());                  \
    sb_recorder.Record(static_cast<Result (*)()>(&Class::Method));             \
  }

#define SB_RECORD_RESULT(Result) sb_recorder.RecordResult(Result);

#endif
