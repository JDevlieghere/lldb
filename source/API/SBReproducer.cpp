//===-- SBReproducer.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBReproducer.h"
#include "lldb/API/LLDB.h"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBHostOS.h"

using namespace lldb;
using namespace lldb_private;

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
    REGISTER_METHOD(void, SBDebugger, SetErrorFileHandle, (FILE *, bool));
    REGISTER_METHOD(void, SBDebugger, SetOutputFileHandle, (FILE *, bool));
    REGISTER_METHOD(void, SBDebugger, SetInputFileHandle, (FILE *, bool));

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
}

bool SBRegistry::Replay() {
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

  auto error_or_file = llvm::MemoryBuffer::getFile(file.GetPath());
  if (auto err = error_or_file.getError())
    return false;

  m_deserializer.LoadBuffer((*error_or_file)->getBuffer());

  while (m_deserializer.HasData(1)) {
    unsigned id = m_deserializer.Deserialize<unsigned>();
    llvm::outs() << "Replaying #" << id << "\n";
    m_ids[id]->operator()();
  }

  return true;
}

template <> const char *SBDeserializer::Deserialize<const char *>() {
  TRACE;
  auto pos = m_buffer.find('\0', m_offset);
  if (pos == llvm::StringRef::npos)
    return nullptr;
  size_t begin = m_offset;
  m_offset = pos + 1;
  return m_buffer.data() + begin;
}

thread_local std::atomic<bool> SBRecorder::g_global_boundary;
char lldb_private::repro::SBProvider::ID = 0;
