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

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

using namespace lldb_private;
using namespace llvm;
using namespace llvm::yaml;

std::atomic<Reproducer::Generator *> Reproducer::g_generator;
std::mutex Reproducer::g_generator_mutex;

std::atomic<Reproducer::Loader *> Reproducer::g_loader;
std::mutex Reproducer::g_loader_mutex;

Reproducer::Generator *Reproducer::GetGenerator() {
  if (g_generator == nullptr) {
    std::lock_guard<std::mutex> lock(g_generator_mutex);
    if (g_generator == nullptr) {
      g_generator = new Reproducer::Generator();
    }
  }
  return g_generator;
}

Reproducer::Loader *Reproducer::GetLoader() {
  if (g_loader == nullptr) {
    std::lock_guard<std::mutex> lock(g_loader_mutex);
    if (g_loader == nullptr) {
      g_loader = new Reproducer::Loader();
    }
  }
  return g_loader;
}

Reproducer::Generator::Generator() : m_enabled(false), m_done(false) {
  m_directory = HostInfo::GetReproducerTempDir();
}

Reproducer::Provider &Reproducer::Generator::Register(
    std::unique_ptr<Reproducer::Provider> &&provider) {
  std::lock_guard<std::mutex> lock(m_providers_mutex);

  AddProviderToIndex(provider->GetInfo());

  m_providers.push_back(std::move(provider));
  return *m_providers.back();
}

void Reproducer::Generator::Keep() {
  assert(!m_done);
  m_done = true;

  if (!m_enabled)
    return;

  for (auto &provider : m_providers)
    provider->Keep();
}

void Reproducer::Generator::Discard() {
  assert(!m_done);
  m_done = true;

  if (!m_enabled)
    return;

  for (auto &provider : m_providers)
    provider->Discard();

  llvm::sys::fs::remove_directories(m_directory.GetPath());
}

void Reproducer::Generator::AddProviderToIndex(
    const Reproducer::ProviderInfo &provider_info) {
  FileSpec index = m_directory;
  index.AppendPathComponent("index.yaml");

  std::error_code EC;
  auto strm = make_unique<raw_fd_ostream>(index.GetPath(), EC,
                                          sys::fs::OpenFlags::F_None);
  yaml::Output yout(*strm);
  yout << const_cast<Reproducer::ProviderInfo &>(provider_info);
}

Reproducer::Loader::Loader() : m_loaded(false) {}

llvm::Error Reproducer::Loader::LoadIndex(const FileSpec &directory) {
  if (m_loaded)
    return llvm::Error::success();

  FileSpec index = directory;
  index.AppendPathComponent("index.yaml");

  auto error_or_file = MemoryBuffer::getFile(index.GetPath());
  if (auto err = error_or_file.getError())
    return errorCodeToError(err);

  std::vector<Reproducer::ProviderInfo> provider_info;
  yaml::Input yin((*error_or_file)->getBuffer());
  yin >> provider_info;

  if (auto err = yin.error())
    return errorCodeToError(err);

  for (auto &info : provider_info)
    m_provider_info[info.name] = info;

  m_loaded = true;

  return llvm::Error::success();
}

llvm::Optional<Reproducer::ProviderInfo>
Reproducer::Loader::GetProviderInfo(StringRef name) {
  assert(m_loaded);

  auto it = m_provider_info.find(name);
  if (it == m_provider_info.end())
    return llvm::None;

  return it->second;
}
