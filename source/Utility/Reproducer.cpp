//===-- Reproducer.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Reproducer.h"
#include "lldb/Host/HostInfo.h"

#include "llvm/Support/raw_ostream.h"

using namespace lldb_private;
using namespace llvm;
using namespace llvm::yaml;

std::atomic<Reproducer *> Reproducer::g_instance;
std::mutex Reproducer::g_instance_mutex;

Reproducer::Reproducer() : m_enabled(false), m_done(false) {
  m_directory = HostInfo::GetReproducerTempDir();
}

Reproducer::~Reproducer() { assert(m_done); }

Reproducer *Reproducer::Instance() {
  if (g_instance == nullptr) {
    std::lock_guard<std::mutex> lock(g_instance_mutex);
    if (g_instance == nullptr) {
      g_instance = new Reproducer();
    }
  }
  return g_instance;
}

Reproducer::Provider &
Reproducer::Register(std::unique_ptr<Reproducer::Provider> &&provider) {
  std::lock_guard<std::mutex> lock(m_providers_mutex);

  AddProviderToIndex(provider->GetInfo());

  m_providers.push_back(std::move(provider));
  return *m_providers.back();
}

void Reproducer::Keep() {
  assert(!m_done);
  m_done = true;

  if (!m_enabled)
    return;

  for (auto &provider : m_providers)
    provider->Keep();
}

void Reproducer::Discard() {
  assert(!m_done);
  m_done = true;

  if (!m_enabled)
    return;

  for (auto &provider : m_providers)
    provider->Discard();
}

void Reproducer::AddProviderToIndex(
    const Reproducer::ProviderInfo &provider_info) {
  FileSpec index = m_directory;
  index.AppendPathComponent("index.yaml");

  std::error_code EC;
  auto strm = make_unique<raw_fd_ostream>(index.GetPath(), EC,
                                          sys::fs::OpenFlags::F_None);
  yaml::Output yout(*strm);
  yout << const_cast<Reproducer::ProviderInfo &>(provider_info);
}
