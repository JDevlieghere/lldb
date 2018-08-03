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

Reproducer::Generator *Reproducer::GetGenerator() {
  if (m_generate_reproducer)
    return &m_generator;
  return nullptr;
}

Reproducer::Loader *Reproducer::GetLoader() {
  if (m_use_reproducer)
    return &m_loader;
  return nullptr;
}

void Reproducer::SetGenerateReproducer(bool value) {
  assert(!value ||
         !m_use_reproducer && "Cannot generate reproducer when using one.");
  m_generate_reproducer = value;
  m_generator.SetEnabled(value);
}

void Reproducer::SetUseReproducer(bool value) {
  assert(!value || !m_generate_reproducer &&
                       "Cannot use reproducer when generating one.");
  m_use_reproducer = value;
}

Reproducer::Generator::Generator() : m_enabled(false), m_done(false) {
  m_directory = HostInfo::GetReproducerTempDir();
}

Reproducer::Generator::~Generator() {
  if (m_done)
    return;
  if (m_enabled)
    Keep();
  else
    Discard();
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

  errs() << "Reproducer written to '" << m_directory.GetPath() << "'\n";
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

void Reproducer::Generator::ChangeDirectory(const FileSpec &directory) {
  assert(m_providers.empty() && "Changing the directory after providers have "
                                "been registered would invalidate the index.");
  m_directory = directory;
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

  m_directory = directory;
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
