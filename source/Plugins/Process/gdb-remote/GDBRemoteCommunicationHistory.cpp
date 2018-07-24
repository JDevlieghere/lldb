//===-- GDBRemoteCommunicationHistory.cpp -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "GDBRemoteCommunicationHistory.h"

// Other libraries and framework includes
#include "lldb/Core/StreamFile.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"

using namespace llvm;
using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_gdb_remote;

void GDBRemoteCommunicationHistory::Entry::Serialize(raw_ostream &strm) const {
  yaml::Output yout(strm);
  yout << const_cast<GDBRemoteCommunicationHistory::Entry &>(*this);
}

llvm::Expected<std::vector<GDBRemoteCommunicationHistory::Entry>>
Deserialize(const FileSpec &path) {
  auto error_or_file = MemoryBuffer::getFile(path.GetPath());
  if (auto err = error_or_file.getError())
    return errorCodeToError(err);

  std::vector<GDBRemoteCommunicationHistory::Entry> entries;
  yaml::Input yin((*error_or_file)->getBuffer());
  yin >> entries;

  if (auto err = yin.error())
    return errorCodeToError(err);

  // We'll want to manipulate the vector like a stack so we need to reverse the
  // order of the packets to have the oldest on at the back.
  std::reverse(entries.begin(), entries.end());

  return entries;
}

GDBRemoteCommunicationHistory::GDBRemoteCommunicationHistory(uint32_t size)
    : m_packets(), m_curr_idx(0), m_total_packet_count(0),
      m_dumped_to_log(false) {
  if (size)
    m_packets.resize(size);
}

GDBRemoteCommunicationHistory::~GDBRemoteCommunicationHistory() {}

void GDBRemoteCommunicationHistory::AddPacket(char packet_char, PacketType type,
                                              uint32_t bytes_transmitted) {
  const size_t size = m_packets.size();
  if (size > 0) {
    const uint32_t idx = GetNextIndex();
    m_packets[idx].packet.assign(1, packet_char);
    m_packets[idx].type = type;
    m_packets[idx].bytes_transmitted = bytes_transmitted;
    m_packets[idx].packet_idx = m_total_packet_count;
    m_packets[idx].tid = llvm::get_threadid();
    if (m_stream_up)
      m_packets[idx].Serialize(*m_stream_up);
  }
}

void GDBRemoteCommunicationHistory::AddPacket(const std::string &src,
                                              uint32_t src_len, PacketType type,
                                              uint32_t bytes_transmitted) {
  const size_t size = m_packets.size();
  if (size > 0) {
    const uint32_t idx = GetNextIndex();
    m_packets[idx].packet.assign(src, 0, src_len);
    m_packets[idx].type = type;
    m_packets[idx].bytes_transmitted = bytes_transmitted;
    m_packets[idx].packet_idx = m_total_packet_count;
    m_packets[idx].tid = llvm::get_threadid();
    if (m_stream_up)
      m_packets[idx].Serialize(*m_stream_up);
  }
}

void GDBRemoteCommunicationHistory::Dump(Stream &strm) const {
  const uint32_t size = GetNumPacketsInHistory();
  const uint32_t first_idx = GetFirstSavedPacketIndex();
  const uint32_t stop_idx = m_curr_idx + size;
  for (uint32_t i = first_idx; i < stop_idx; ++i) {
    const uint32_t idx = NormalizeIndex(i);
    const Entry &entry = m_packets[idx];
    if (entry.type == ePacketTypeInvalid || entry.packet.empty())
      break;
    strm.Printf("history[%u] tid=0x%4.4" PRIx64 " <%4u> %s packet: %s\n",
                entry.packet_idx, entry.tid, entry.bytes_transmitted,
                (entry.type == ePacketTypeSend) ? "send" : "read",
                entry.packet.c_str());
  }
}

void GDBRemoteCommunicationHistory::Dump(Log *log) const {
  if (log && !m_dumped_to_log) {
    m_dumped_to_log = true;
    const uint32_t size = GetNumPacketsInHistory();
    const uint32_t first_idx = GetFirstSavedPacketIndex();
    const uint32_t stop_idx = m_curr_idx + size;
    for (uint32_t i = first_idx; i < stop_idx; ++i) {
      const uint32_t idx = NormalizeIndex(i);
      const Entry &entry = m_packets[idx];
      if (entry.type == ePacketTypeInvalid || entry.packet.empty())
        break;
      log->Printf("history[%u] tid=0x%4.4" PRIx64 " <%4u> %s packet: %s",
                  entry.packet_idx, entry.tid, entry.bytes_transmitted,
                  (entry.type == ePacketTypeSend) ? "send" : "read",
                  entry.packet.c_str());
    }
  }
}
