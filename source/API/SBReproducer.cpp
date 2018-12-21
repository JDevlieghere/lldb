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

using namespace lldb;
using namespace lldb_private::repro;
using namespace lldb_private;
using namespace llvm;

#define REGISTER(function, implementation)                                     \
  {                                                                            \
    m_functions[function] = [&](SBDeserializer &s) { implementation };         \
  }

void SBReplayer::Init() {
  m_functions["lldb::SBFileSpec::SBFileSpec(const char *, bool)"] =
      [&](SBDeserializer &s) {
        auto t = s.Read<int>(); // this
        auto a = s.Read<const char *>();
        auto b = s.Read<bool>();
        s.Read<int>(); // return
        auto x = new SBFileSpec(a, b);
        m_index_to_object.AddObjectForIndex(t, x);
      };

  m_functions["lldb::SBFileSpec::SBFileSpec(const char *)"] =
      [&](SBDeserializer &s) {
        auto t = s.Read<int>(); // this
        auto a = s.Read<const char *>();
        s.Read<int>(); // return
        auto x = new SBFileSpec(a);
        m_index_to_object.AddObjectForIndex(t, x);
      };

  m_functions
      ["static lldb::SBFileSpec lldb::SBHostOS::GetUserHomeDirectory()"] =
          [&](SBDeserializer &s) {
            auto r = s.Read<int>(); // return
            auto x = new SBFileSpec(SBHostOS::GetUserHomeDirectory());
            m_index_to_object.AddObjectForIndex(r, x);
          };

  m_functions["void lldb::SBFileSpec::AppendPathComponent(const char *)"] =
      [&](SBDeserializer &s) {
        auto a = m_index_to_object.GetObjectForIndex<SBFileSpec>(s.Read<int>());
        auto b = s.Read<const char *>();
        llvm::outs() << __LINE__ << ':' << b << '\n';
        s.Read<int>();
        a->AppendPathComponent(b);
      };

  m_functions["lldb::SBFileSpec::SBFileSpec(const lldb::SBFileSpec &)"] =
      [&](SBDeserializer &s) {
        auto t = s.Read<int>(); // this
        auto a = m_index_to_object.GetObjectForIndex<SBFileSpec>(s.Read<int>());
        s.Read<int>();
        auto x = new SBFileSpec(*a);
        m_index_to_object.AddObjectForIndex(t, x);
      };

  m_functions["bool lldb::SBFileSpec::Exists() const"] =
      [&](SBDeserializer &s) {
        auto a = m_index_to_object.GetObjectForIndex<SBFileSpec>(s.Read<int>());
        s.Read<int>();
        a->Exists();
      };

  m_functions["lldb::SBCommandInterpreterRunOptions::"
              "SBCommandInterpreterRunOptions()"] = [&](SBDeserializer &s) {
    auto t = s.Read<int>(); // this
    s.Read<int>();          // return
    auto x = new SBCommandInterpreterRunOptions();
    m_index_to_object.AddObjectForIndex(t, x);
  };

  m_functions
      ["void lldb::SBCommandInterpreterRunOptions::SetStopOnError(bool)"] =
          [&](SBDeserializer &s) {
            auto t = m_index_to_object
                         .GetObjectForIndex<SBCommandInterpreterRunOptions>(
                             s.Read<int>());
            auto a = s.Read<bool>();
            s.Read<int>(); // return
            t->SetStopOnError(a);
          };

  m_functions["lldb::SBCommandInterpreter::SBCommandInterpreter(lldb_private::"
              "CommandInterpreter *)"] = [&](SBDeserializer &s) {
    auto t = s.Read<int>();
    auto a =
        m_index_to_object.GetObjectForIndex<CommandInterpreter>(s.Read<int>());
    s.Read<int>(); // return
    auto x = new SBCommandInterpreter(a);
    m_index_to_object.AddObjectForIndex(t, x);
  };

  m_functions["bool lldb::SBCommandInterpreter::IsValid() const"] =
      [&](SBDeserializer &s) {
        auto t = m_index_to_object.GetObjectForIndex<SBCommandInterpreter>(
            s.Read<int>()); // this
        s.Read<int>();      // return
        t->IsValid();
      };
  m_functions["lldb::SBDebugger::SBDebugger()"] = [&](SBDeserializer &s) {
    auto t = s.Read<int>();
    s.Read<int>(); // return
    auto x = new SBDebugger();
    m_index_to_object.AddObjectForIndex(t, x);
  };

  m_functions["static lldb::SBDebugger lldb::SBDebugger::Create(bool)"] =
      [&](SBDeserializer &s) {
        auto a = s.Read<bool>();
        auto r = s.Read<int>(); // return
        auto x = new SBDebugger(SBDebugger::Create(a));
        m_index_to_object.AddObjectForIndex(r, x);
      };

  m_functions["lldb::SBDebugger::SBDebugger(const lldb::SBDebugger &)"] =
      [&](SBDeserializer &s) {
        auto t = s.Read<int>(); // this
        auto a = m_index_to_object.GetObjectForIndex<SBDebugger>(s.Read<int>());
        s.Read<int>(); // return
        auto x = new SBDebugger(*a);
        m_index_to_object.AddObjectForIndex(t, x);
      };

  m_functions
      ["lldb::SBCommandInterpreter lldb::SBDebugger::GetCommandInterpreter()"] =
          [&](SBDeserializer &s) {
            auto t = m_index_to_object.GetObjectForIndex<SBDebugger>(
                s.Read<int>());     // this
            auto r = s.Read<int>(); // return
            auto x = new SBCommandInterpreter(t->GetCommandInterpreter());
            m_index_to_object.AddObjectForIndex(r, x);
          };

  m_functions["void "
              "lldb::SBCommandInterpreter::SourceInitFileInHomeDirectory(lldb::"
              "SBCommandReturnObject &)"] = [&](SBDeserializer &s) {
    auto t = m_index_to_object.GetObjectForIndex<SBCommandInterpreter>(
        s.Read<int>()); // this
    auto a = m_index_to_object.GetObjectForIndex<SBCommandReturnObject>(
        s.Read<int>());
    s.Read<int>(); // return
    t->SourceInitFileInHomeDirectory(*a);
  };

  m_functions["lldb::SBCommandReturnObject::SBCommandReturnObject()"] =
      [&](SBDeserializer &s) {
        auto t = s.Read<int>(); // this
        s.Read<int>();          // return
        auto x = new SBCommandReturnObject();
        m_index_to_object.AddObjectForIndex(t, x);
      };

  REGISTER("lldb::SBCommandInterpreter::SBCommandInterpreter(const "
           "lldb::SBCommandInterpreter &)",
           {
             auto t = s.Read<int>(); // this
             auto a = m_index_to_object.GetObjectForIndex<SBCommandInterpreter>(
                 s.Read<int>());
             s.Read<int>(); // return
             auto x = new SBCommandInterpreter(*a);
             m_index_to_object.AddObjectForIndex(t, x);
           });

  REGISTER("void lldb::SBCommandInterpreter::AllowExitCodeOnQuit(bool)", {
    auto t = m_index_to_object.GetObjectForIndex<SBCommandInterpreter>(
        s.Read<int>()); // this
    auto a = s.Read<bool>();
    s.Read<int>(); // return
    t->AllowExitCodeOnQuit(a);
  });

  REGISTER(
      "lldb::SBDebugger &lldb::SBDebugger::operator=(const lldb::SBDebugger &)",
      {
        auto t = s.Read<int>(); // this
        auto a = m_index_to_object.GetObjectForIndex<SBDebugger>(s.Read<int>());
        s.Read<int>(); // return
        auto x = new SBDebugger(*a);
        m_index_to_object.AddObjectForIndex(t, x);
      });

  REGISTER("int lldb::SBCommandInterpreter::GetQuitStatus()", {
    auto t = m_index_to_object.GetObjectForIndex<SBCommandInterpreter>(
        s.Read<int>()); // this
    s.Read<int>();      // return
    t->GetQuitStatus();
  });

  REGISTER("void lldb::SBDebugger::SkipLLDBInitFiles(bool)", {
    auto t =
        m_index_to_object.GetObjectForIndex<SBDebugger>(s.Read<int>()); // this
    auto a = s.Read<bool>();
    s.Read<int>(); // return
    t->SkipLLDBInitFiles(a);
  });

  REGISTER("void lldb::SBDebugger::SkipAppInitFiles(bool)", {
    auto t =
        m_index_to_object.GetObjectForIndex<SBDebugger>(s.Read<int>()); // this
    auto a = s.Read<bool>();
    s.Read<int>(); // return
    t->SkipAppInitFiles(a);
  });

  REGISTER("bool lldb::SBDebugger::GetAsync()", {
    auto t =
        m_index_to_object.GetObjectForIndex<SBDebugger>(s.Read<int>()); // this
    s.Read<int>(); // return
    t->GetAsync();
  });

  REGISTER("void lldb::SBDebugger::SetAsync(bool)", {
    auto t =
        m_index_to_object.GetObjectForIndex<SBDebugger>(s.Read<int>()); // this
    auto a = s.Read<bool>();
    s.Read<int>(); // return
    t->SetAsync(a);
  });

  REGISTER(
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

  llvm::outs() << "Registered " << m_functions.size() << " functions\n";
#if 0
  for (auto &p : m_functions) {
    llvm::outs() << "Registered '" << p.first() << "'\n";
  }
#endif
}

template <> const char *SBDeserializer::Read<const char *>() {
  auto pos = m_buffer.find('\0', m_offset);
  if (pos == llvm::StringRef::npos)
    return nullptr;
  size_t begin = m_offset;
  m_offset = pos + 1;
  return m_buffer.data() + begin;
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
