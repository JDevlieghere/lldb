//===-- SBReproducer.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SBReproducerPrivate.h"

#include "lldb/API/LLDB.h"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBHostOS.h"
#include "lldb/API/SBReproducer.h"

#include "lldb/Host/FileSystem.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::repro;

#define REGISTER_CONSTRUCTOR(Class, Signature)                                 \
  Register<Class * Signature>(&construct<Class Signature>::doit, m_id++)

#define REGISTER_METHOD(Result, Class, Method, Signature)                      \
  Register(&invoke<Result(Class::*) Signature>::method<&Class::Method>::doit,  \
           m_id++)
#define REGISTER_METHOD_CONST(Result, Class, Method, Signature)                \
  Register(&invoke<Result(Class::*)                                            \
                       Signature const>::method_const<&Class::Method>::doit,   \
           m_id++)
#define REGISTER_STATIC_METHOD(Result, Class, Method, Signature)               \
  Register<Result Signature>(static_cast<Result(*) Signature>(&Class::Method), \
                             m_id++)

static FileSpec GetCommandFile() {
  repro::Loader *loader = repro::Reproducer::Instance().GetLoader();
  if (!loader) {
    return {};
  }

  auto provider_info = loader->GetProviderInfo("command-interpreter");
  if (!provider_info) {
    return {};
  }

  if (provider_info->files.empty()) {
    return {};
  }

  return loader->GetRoot().CopyByAppendingPathComponent(
      provider_info->files.front());
}

static void SetInputFileHandleRedirect(SBDebugger *t, FILE *, bool) {
  FileSpec fs = GetCommandFile();
  if (!fs)
    return;

  FILE *f = FileSystem::Instance().Fopen(fs.GetPath().c_str(), "r");
  if (f == nullptr)
    return;
  t->SetInputFileHandle(f, true);
}

static void SetFileHandleRedirect(SBDebugger *, FILE *, bool) {
  // Do nothing.
}

void SBRegistry::Init() {
  // SBFileSpec
  {
    REGISTER_CONSTRUCTOR(SBFileSpec, ());
    REGISTER_CONSTRUCTOR(SBFileSpec, (const lldb::SBFileSpec &));
    REGISTER_CONSTRUCTOR(SBFileSpec, (const char *));
    REGISTER_CONSTRUCTOR(SBFileSpec, (const char *, bool));

    REGISTER_METHOD_CONST(bool, SBFileSpec, IsValid, ());
    REGISTER_METHOD_CONST(bool, SBFileSpec, Exists, ());
    REGISTER_METHOD_CONST(const char *, SBFileSpec, GetFilename, ());
    REGISTER_METHOD_CONST(const char *, SBFileSpec, GetDirectory, ());
    REGISTER_METHOD_CONST(uint32_t, SBFileSpec, GetPath, (char *, size_t));
    REGISTER_METHOD_CONST(bool, SBFileSpec, GetDescription, (lldb::SBStream &));

    REGISTER_METHOD(const SBFileSpec &,
                    SBFileSpec, operator=,(const lldb::SBFileSpec &));
    REGISTER_METHOD(bool, SBFileSpec, ResolveExecutableLocation, ());
    REGISTER_METHOD(void, SBFileSpec, SetFilename, (const char *));
    REGISTER_METHOD(void, SBFileSpec, SetDirectory, (const char *));
    REGISTER_METHOD(void, SBFileSpec, AppendPathComponent, (const char *));

    REGISTER_STATIC_METHOD(int, SBFileSpec, ResolvePath,
                           (const char *, char *, size_t));
  }

  // SBHostOS
  { REGISTER_STATIC_METHOD(SBFileSpec, SBHostOS, GetUserHomeDirectory, ()); }

  // SBDebugger
  {
    REGISTER_CONSTRUCTOR(SBDebugger, ());
    REGISTER_CONSTRUCTOR(SBDebugger, (const DebuggerSP &));
    REGISTER_CONSTRUCTOR(SBDebugger, (const SBDebugger &));

    REGISTER_METHOD(SBCommandInterpreter, SBDebugger, GetCommandInterpreter,
                    ());
    REGISTER_METHOD(SBDebugger &, SBDebugger, operator=,(const SBDebugger &));
    REGISTER_METHOD(void, SBDebugger, SkipLLDBInitFiles, (bool));
    REGISTER_METHOD(void, SBDebugger, SkipAppInitFiles, (bool));
    REGISTER_METHOD(bool, SBDebugger, GetAsync, ());
    REGISTER_METHOD(void, SBDebugger, SetAsync, (bool));
    REGISTER_METHOD(void, SBDebugger, RunCommandInterpreter,
                    (bool, bool, lldb::SBCommandInterpreterRunOptions &, int &,
                     bool &, bool &));

    // Custom implementation.
    Register(&invoke<void (SBDebugger::*)(
                 FILE *, bool)>::method<&SBDebugger::SetInputFileHandle>::doit,
             m_id++, &SetInputFileHandleRedirect);
    Register(&invoke<void (SBDebugger::*)(
                 FILE *, bool)>::method<&SBDebugger::SetErrorFileHandle>::doit,
             m_id++, &SetFileHandleRedirect);
    Register(&invoke<void (SBDebugger::*)(
                 FILE *, bool)>::method<&SBDebugger::SetOutputFileHandle>::doit,
             m_id++, &SetFileHandleRedirect);

    REGISTER_STATIC_METHOD(SBDebugger, SBDebugger, Create, ());
    REGISTER_STATIC_METHOD(SBDebugger, SBDebugger, Create, (bool));

    // FIXME: Why is this an issue?
#if 0
    REGISTER_STATIC_METHOD(SBDebugger, SBDebugger, Create,
                           (bool, lldb::LogOutputCallback, void *));
#endif
  }

  // SBCommandInterpreter
  {
    REGISTER_CONSTRUCTOR(SBCommandInterpreter,
                         (lldb_private::CommandInterpreter *));
    REGISTER_CONSTRUCTOR(SBCommandInterpreter, (SBCommandInterpreter &));
    REGISTER_CONSTRUCTOR(SBCommandInterpreter, (const SBCommandInterpreter &));

    REGISTER_METHOD_CONST(bool, SBCommandInterpreter, IsValid, ());

    REGISTER_METHOD(void, SBCommandInterpreter, SourceInitFileInHomeDirectory,
                    (SBCommandReturnObject &));
    REGISTER_METHOD(void, SBCommandInterpreter, AllowExitCodeOnQuit, (bool));
    REGISTER_METHOD(int, SBCommandInterpreter, GetQuitStatus, ());
  }

  // SBCommandInterpreterRunOptions
  {
    REGISTER_CONSTRUCTOR(SBCommandInterpreterRunOptions, ());
    REGISTER_METHOD(void, SBCommandInterpreterRunOptions, SetStopOnError,
                    (bool));
    REGISTER_METHOD(void, SBCommandInterpreterRunOptions, SetStopOnCrash,
                    (bool));
  }

  // SBCommandReturnObject
  { REGISTER_CONSTRUCTOR(SBCommandReturnObject, ()); }
}

bool SBReproducer::Replay() const {
  repro::Loader *loader = repro::Reproducer::Instance().GetLoader();
  if (!loader) {
    return false;
  }

  llvm::Optional<repro::ProviderInfo> info = loader->GetProviderInfo("sbapi");
  if (!info) {
    return false;
  }

  FileSpec file(loader->GetRoot());
  file.AppendPathComponent(info->files.front());

  SBRegistry::Instance().Replay(file);

  return true;
}

bool SBRegistry::Replay(const FileSpec &file) {
  auto error_or_file = llvm::MemoryBuffer::getFile(file.GetPath());
  if (auto err = error_or_file.getError())
    return false;

  m_deserializer.LoadBuffer((*error_or_file)->getBuffer());
  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_API);

  while (m_deserializer.HasData(1)) {
    unsigned id = m_deserializer.Deserialize<unsigned>();
    if (log)
      log->Printf("Replaying function #%u", id);
    m_ids[id]->operator()();
  }

  return true;
}

template <> const char *SBDeserializer::Deserialize<const char *>() {
  auto pos = m_buffer.find('\0', m_offset);
  if (pos == llvm::StringRef::npos)
    return nullptr;
  size_t begin = m_offset;
  m_offset = pos + 1;
  return m_buffer.data() + begin;
}

std::atomic<bool> lldb_private::repro::SBRecorder::g_global_boundary;
char lldb_private::repro::SBProvider::ID = 0;
