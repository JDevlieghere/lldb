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

#include "lldb/Host/FileSystem.h"
#include "lldb/Utility/FileSpec.h"

using namespace lldb;
using namespace lldb_private::repro;
using namespace lldb_private;
using namespace llvm;

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

template <typename T>
T *ReadObject(SBDeserializer &s, SBIndexToObject &index_to_object) {
  return index_to_object.GetObjectForIndex<T>(s.Read<int>());
}

#define SB_REGISTER(function, implementation)                                  \
  {                                                                            \
    m_functions[function] = [&](SBDeserializer &s) { implementation };         \
  }

void SBReplayer::Init() {
  SB_REGISTER("lldb::SBFileSpec::SBFileSpec(const char *, bool)", {
    auto t = s.Read<int>(); // this
    auto a = s.Read<const char *>();
    auto b = s.Read<bool>();
    auto x = new SBFileSpec(a, b);
    m_index_to_object.AddObjectForIndex(t, x);
  });

  SB_REGISTER("lldb::SBFileSpec::SBFileSpec(const char *)", {
    auto t = s.Read<int>(); // this
    auto a = s.Read<const char *>();
    auto x = new SBFileSpec(a);
    m_index_to_object.AddObjectForIndex(t, x);
  });

  SB_REGISTER("static lldb::SBFileSpec lldb::SBHostOS::GetUserHomeDirectory()",
              {
                auto r = s.Read<int>(); // return
                auto x = new SBFileSpec(SBHostOS::GetUserHomeDirectory());
                m_index_to_object.AddObjectForIndex(r, x);
              });

  SB_REGISTER("void lldb::SBFileSpec::AppendPathComponent(const char *)", {
    auto a = ReadObject<SBFileSpec>(s, m_index_to_object);
    auto b = s.Read<const char *>();
    a->AppendPathComponent(b);
  });

  SB_REGISTER("lldb::SBFileSpec::SBFileSpec(const lldb::SBFileSpec &)", {
    auto t = s.Read<int>(); // this
    auto a = m_index_to_object.GetObjectForIndex<SBFileSpec>(s.Read<int>());
    auto x = new SBFileSpec(*a);
    m_index_to_object.AddObjectForIndex(t, x);
  });

  SB_REGISTER("bool lldb::SBFileSpec::Exists() const", {
    auto a = m_index_to_object.GetObjectForIndex<SBFileSpec>(s.Read<int>());
    a->Exists();
  });

  SB_REGISTER("lldb::SBCommandInterpreterRunOptions::"
              "SBCommandInterpreterRunOptions()",
              {
                auto t = s.Read<int>(); // this
                auto x = new SBCommandInterpreterRunOptions();
                m_index_to_object.AddObjectForIndex(t, x);
              });

  SB_REGISTER(
      "void lldb::SBCommandInterpreterRunOptions::SetStopOnError(bool)", {
        auto t =
            m_index_to_object.GetObjectForIndex<SBCommandInterpreterRunOptions>(
                s.Read<int>());
        auto a = s.Read<bool>();
        t->SetStopOnError(a);
      });

  SB_REGISTER(
      "void lldb::SBCommandInterpreterRunOptions::SetStopOnCrash(bool)", {
        auto t =
            m_index_to_object.GetObjectForIndex<SBCommandInterpreterRunOptions>(
                s.Read<int>());
        auto a = s.Read<bool>();
        t->SetStopOnCrash(a);
      });

  SB_REGISTER("lldb::SBCommandInterpreter::SBCommandInterpreter(lldb_private::"
              "CommandInterpreter *)",
              {
                auto t = s.Read<int>();
                auto a =
                    m_index_to_object.GetObjectForIndex<CommandInterpreter>(
                        s.Read<int>());
                auto x = new SBCommandInterpreter(a);
                m_index_to_object.AddObjectForIndex(t, x);
              });

  SB_REGISTER("bool lldb::SBCommandInterpreter::IsValid() const", {
    auto t = m_index_to_object.GetObjectForIndex<SBCommandInterpreter>(
        s.Read<int>()); // this
    t->IsValid();
  });

  SB_REGISTER("lldb::SBDebugger::SBDebugger()", {
    auto t = s.Read<int>();
    auto x = new SBDebugger();
    m_index_to_object.AddObjectForIndex(t, x);
  });

  SB_REGISTER("static lldb::SBDebugger lldb::SBDebugger::Create(bool)", {
    auto a = s.Read<bool>();
    auto r = s.Read<int>();
    auto x = new SBDebugger(SBDebugger::Create(a));
    m_index_to_object.AddObjectForIndex(r, x);
  });

  SB_REGISTER("lldb::SBDebugger::SBDebugger(const lldb::SBDebugger &)", {
    auto t = s.Read<int>(); // this
    auto a = m_index_to_object.GetObjectForIndex<SBDebugger>(s.Read<int>());
    auto x = new SBDebugger(*a);
    m_index_to_object.AddObjectForIndex(t, x);
  });

  SB_REGISTER(
      "lldb::SBCommandInterpreter lldb::SBDebugger::GetCommandInterpreter()", {
        auto t = m_index_to_object.GetObjectForIndex<SBDebugger>(
            s.Read<int>()); // this
        auto r = s.Read<int>();
        auto x = new SBCommandInterpreter(t->GetCommandInterpreter());
        m_index_to_object.AddObjectForIndex(r, x);
      });

  SB_REGISTER("void "
              "lldb::SBCommandInterpreter::SourceInitFileInHomeDirectory(lldb::"
              "SBCommandReturnObject &)",
              {
                auto t =
                    m_index_to_object.GetObjectForIndex<SBCommandInterpreter>(
                        s.Read<int>()); // this
                auto a =
                    m_index_to_object.GetObjectForIndex<SBCommandReturnObject>(
                        s.Read<int>());
                t->SourceInitFileInHomeDirectory(*a);
              });

  SB_REGISTER("lldb::SBCommandReturnObject::SBCommandReturnObject()", {
    auto t = s.Read<int>(); // this
    auto x = new SBCommandReturnObject();
    m_index_to_object.AddObjectForIndex(t, x);
  });

  SB_REGISTER("lldb::SBCommandInterpreter::SBCommandInterpreter(const "
              "lldb::SBCommandInterpreter &)",
              {
                auto t = s.Read<int>(); // this
                auto a =
                    m_index_to_object.GetObjectForIndex<SBCommandInterpreter>(
                        s.Read<int>());
                auto x = new SBCommandInterpreter(*a);
                m_index_to_object.AddObjectForIndex(t, x);
              });

  SB_REGISTER("void lldb::SBCommandInterpreter::AllowExitCodeOnQuit(bool)", {
    auto t = m_index_to_object.GetObjectForIndex<SBCommandInterpreter>(
        s.Read<int>()); // this
    auto a = s.Read<bool>();
    t->AllowExitCodeOnQuit(a);
  });

  SB_REGISTER(
      "lldb::SBDebugger &lldb::SBDebugger::operator=(const lldb::SBDebugger &)",
      {
        auto t = s.Read<int>(); // this
        auto a = m_index_to_object.GetObjectForIndex<SBDebugger>(s.Read<int>());
        auto x = new SBDebugger(*a);
        m_index_to_object.AddObjectForIndex(t, x);
      });

  SB_REGISTER("int lldb::SBCommandInterpreter::GetQuitStatus()", {
    auto t = m_index_to_object.GetObjectForIndex<SBCommandInterpreter>(
        s.Read<int>()); // this
    t->GetQuitStatus();
  });

  SB_REGISTER("void lldb::SBDebugger::SkipLLDBInitFiles(bool)", {
    auto t =
        m_index_to_object.GetObjectForIndex<SBDebugger>(s.Read<int>()); // this
    auto a = s.Read<bool>();
    t->SkipLLDBInitFiles(a);
  });

  SB_REGISTER("void lldb::SBDebugger::SkipAppInitFiles(bool)", {
    auto t =
        m_index_to_object.GetObjectForIndex<SBDebugger>(s.Read<int>()); // this
    auto a = s.Read<bool>();
    t->SkipAppInitFiles(a);
  });

  SB_REGISTER("bool lldb::SBDebugger::GetAsync()", {
    auto t =
        m_index_to_object.GetObjectForIndex<SBDebugger>(s.Read<int>()); // this
    t->GetAsync();
  });

  SB_REGISTER("void lldb::SBDebugger::SetAsync(bool)", {
    auto t =
        m_index_to_object.GetObjectForIndex<SBDebugger>(s.Read<int>()); // this
    auto a = s.Read<bool>();
    t->SetAsync(a);
  });

  SB_REGISTER(
      "void lldb::SBDebugger::RunCommandInterpreter(bool, bool, "
      "lldb::SBCommandInterpreterRunOptions &, int &, bool &, bool &)",
      {
        auto t = m_index_to_object.GetObjectForIndex<SBDebugger>(
            s.Read<int>()); // this
        auto a = s.Read<bool>();
        auto b = s.Read<bool>();
        auto c =
            m_index_to_object.GetObjectForIndex<SBCommandInterpreterRunOptions>(
                s.Read<int>());
        auto d = s.Read<int>();
        auto e = s.Read<bool>();
        auto f = s.Read<bool>();
        t->RunCommandInterpreter(a, b, *c, d, e, f);
      });

  SB_REGISTER("void lldb::SBDebugger::SetErrorFileHandle(FILE *, bool)", {
    s.Read<int>(); // this
    s.Read<int>();
    s.Read<bool>();

    // Do nothing.
  });

  SB_REGISTER("void lldb::SBDebugger::SetOutputFileHandle(FILE *, bool)", {
    s.Read<int>(); // this
    s.Read<int>();
    s.Read<bool>();

    // Do nothing.
  });

  SB_REGISTER("void lldb::SBDebugger::SetInputFileHandle(FILE *, bool)", {
    auto t =
        m_index_to_object.GetObjectForIndex<SBDebugger>(s.Read<int>()); // this
    s.Read<int>();
    s.Read<bool>();

    FileSpec fs = GetCommandFile();
    if (!fs)
      return;

    FILE *f = FileSystem::Instance().Fopen(fs.GetPath().c_str(), "r");
    if (f == nullptr)
      return;
    t->SetInputFileHandle(f, true);
  });

  llvm::outs() << "Registered " << m_functions.size() << " functions\n";
}

namespace lldb {
void GenerateReproducer() {
  if (auto *g = Reproducer::Instance().GetGenerator()) {
    g->Keep();
  }
}

bool ReplayReproducer() {
  if (auto *g = Reproducer::Instance().GetLoader()) {
    SBReplayer replayer;
    replayer.Init();
    if (auto e = replayer.Replay()) {
      llvm::outs() << toString(std::move(e)) << '\n';
    }
    return true;
  }
  return false;
}
} // namespace lldb

char SBProvider::ID = 0;
thread_local std::atomic<bool> SBRecorder::g_global_boundary;
