//===-- GDBRemoteCommunicationHistory.h--------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_GDBRemoteCommunicationHistory_h_
#define liblldb_GDBRemoteCommunicationHistory_h_

#include <string>
#include <vector>

#include "lldb/lldb-public.h"

namespace lldb_private {
namespace process_gdb_remote {

class GDBRemoteCommunicationHistory {
public:
  enum PacketType { ePacketTypeInvalid = 0, ePacketTypeSend, ePacketTypeRecv };

  struct Entry {
    Entry()
        : packet(), type(ePacketTypeInvalid), bytes_transmitted(0),
          packet_idx(0), tid(LLDB_INVALID_THREAD_ID) {}

    void Clear() {
      packet.clear();
      type = ePacketTypeInvalid;
      bytes_transmitted = 0;
      packet_idx = 0;
      tid = LLDB_INVALID_THREAD_ID;
    }

    std::string packet;
    PacketType type;
    uint32_t bytes_transmitted;
    uint32_t packet_idx;
    lldb::tid_t tid;
  };

  GDBRemoteCommunicationHistory(uint32_t size);

  ~GDBRemoteCommunicationHistory();

  // For single char packets for ack, nack and /x03
  void AddPacket(char packet_char, PacketType type, uint32_t bytes_transmitted);

  void AddPacket(const std::string &src, uint32_t src_len, PacketType type,
                 uint32_t bytes_transmitted);

  void Dump(Stream &strm) const;

  void Dump(Log *log) const;

  bool DidDumpToLog() const { return m_dumped_to_log; }

protected:
  uint32_t GetFirstSavedPacketIndex() const {
    if (m_total_packet_count < m_packets.size())
      return 0;
    else
      return m_curr_idx + 1;
  }

  uint32_t GetNumPacketsInHistory() const {
    if (m_total_packet_count < m_packets.size())
      return m_total_packet_count;
    else
      return (uint32_t)m_packets.size();
  }

  uint32_t GetNextIndex() {
    ++m_total_packet_count;
    const uint32_t idx = m_curr_idx;
    m_curr_idx = NormalizeIndex(idx + 1);
    return idx;
  }

  uint32_t NormalizeIndex(uint32_t i) const { return i % m_packets.size(); }

  std::vector<Entry> m_packets;
  uint32_t m_curr_idx;
  uint32_t m_total_packet_count;
  mutable bool m_dumped_to_log;
};

} // namespace process_gdb_remote
} // namespace lldb_private

#endif // liblldb_GDBRemoteCommunicationHistory_h_
