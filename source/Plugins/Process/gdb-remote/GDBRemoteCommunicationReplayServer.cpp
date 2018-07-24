//===-- GDBRemoteCommunicationReplayServer.cpp ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <errno.h>

#include "lldb/Host/Config.h"

#include "GDBRemoteCommunicationReplayServer.h"
#include "ProcessGDBRemoteLog.h"

// C Includes
// C++ Includes
#include <cstring>

// Project includes
#include "lldb/Core/Event.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StringExtractorGDBRemote.h"

using namespace llvm;
using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_gdb_remote;

GDBRemoteCommunicationReplayServer::GDBRemoteCommunicationReplayServer()
    : GDBRemoteCommunication("gdb-remote.server",
                             "gdb-remote.server.rx_packet"),
      m_async_broadcaster(nullptr, "lldb.gdb-remote.server.async-broadcaster"),
      m_async_listener_sp(
          Listener::MakeListener("lldb.gdb-remote.server.async-listener")),
      m_async_thread_state_mutex() {
  m_async_broadcaster.SetEventName(eBroadcastBitAsyncContinue,
                                   "async thread continue");
  m_async_broadcaster.SetEventName(eBroadcastBitAsyncThreadShouldExit,
                                   "async thread should exit");

  const uint32_t async_event_mask =
      eBroadcastBitAsyncContinue | eBroadcastBitAsyncThreadShouldExit;
  m_async_listener_sp->StartListeningForEvents(&m_async_broadcaster,
                                               async_event_mask);
}

GDBRemoteCommunicationReplayServer::~GDBRemoteCommunicationReplayServer() {
  StopAsyncThread();
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationReplayServer::GetPacketAndSendResponse(
    Timeout<std::micro> timeout, Status &error, bool &interrupt, bool &quit) {
  StringExtractorGDBRemote packet;

  printf("Waiting for packet\n");
  PacketResult packet_result = WaitForPacketNoLock(packet, timeout, false);
  if (packet_result == PacketResult::Success) {

    m_async_broadcaster.BroadcastEvent(eBroadcastBitAsyncContinue);
    const StringExtractorGDBRemote::ServerPacketType packet_type =
        packet.GetServerPacketType();

    printf("Received '%s'\n", packet.GetStringRef().c_str());

    switch (packet_type) {
    case StringExtractorGDBRemote::eServerPacketType_nack:
    case StringExtractorGDBRemote::eServerPacketType_ack:
      // Process the next packet.
      return PacketResult::Success;
    default:
      break;
    }

    while (!m_packet_history.empty()) {
      // Pop last packet from the history.
      GDBRemoteCommunicationHistory::Entry entry = m_packet_history.back();
      m_packet_history.pop_back();

      // We only care about what we received from the server. Skip everything
      // the client sent.
      if (entry.type != GDBRemoteCommunicationHistory::ePacketTypeRecv)
        continue;

      // We always disable acks so it's safe to ignore those.
      if (entry.packet == "+")
        continue;

      printf("Sent response '%s'\n", entry.packet.c_str());
      return SendRawPacketNoLock(entry.packet, true);
    }

    quit = true;
  } else {
    if (!IsConnected()) {
      error.SetErrorString("lost connection");
      quit = true;
    } else {
      error.SetErrorString("timeout");
    }
  }

  return packet_result;
}

LLVM_YAML_IS_DOCUMENT_LIST_VECTOR(
    std::vector<
        lldb_private::process_gdb_remote::GDBRemoteCommunicationHistory::Entry>)

llvm::Error
GDBRemoteCommunicationReplayServer::LoadReplayHistory(const FileSpec &path) {
  auto error_or_file = MemoryBuffer::getFile(path.GetPath());
  if (auto err = error_or_file.getError())
    return errorCodeToError(err);

  yaml::Input yin((*error_or_file)->getBuffer());
  yin >> m_packet_history;

  if (auto err = yin.error())
    return errorCodeToError(err);

  // We want to manipulate the vector like a stack so we need to reverse the
  // order of the packets to have the oldest on at the back.
  std::reverse(m_packet_history.begin(), m_packet_history.end());

  return Error::success();
}

bool GDBRemoteCommunicationReplayServer::StartAsyncThread() {
  std::lock_guard<std::recursive_mutex> guard(m_async_thread_state_mutex);
  if (!m_async_thread.IsJoinable()) {
    // Create a thread that watches our internal state and controls which
    // events make it to clients (into the DCProcess event queue).
    m_async_thread = ThreadLauncher::LaunchThread(
        "<lldb.gdb-remote.server.async>",
        GDBRemoteCommunicationReplayServer::AsyncThread, this, nullptr);
  }

  // Wait for handshake.
  m_async_broadcaster.BroadcastEvent(eBroadcastBitAsyncContinue);

  return m_async_thread.IsJoinable();
}

void GDBRemoteCommunicationReplayServer::StopAsyncThread() {
  std::lock_guard<std::recursive_mutex> guard(m_async_thread_state_mutex);

  if (m_async_thread.IsJoinable()) {
    // Request thread to stop.
    m_async_broadcaster.BroadcastEvent(eBroadcastBitAsyncThreadShouldExit);

    // Disconnect client.
    Disconnect();

    // Stop the thread.
    m_async_thread.Join(nullptr);
    m_async_thread.Reset();
  }
}

void GDBRemoteCommunicationReplayServer::ReceivePacket(
    GDBRemoteCommunicationReplayServer &server, bool &done) {
  Status error;
  bool interrupt;
  auto packet_result = server.GetPacketAndSendResponse(std::chrono::seconds(1),
                                                       error, interrupt, done);
  if (packet_result != GDBRemoteCommunication::PacketResult::Success &&
      packet_result !=
          GDBRemoteCommunication::PacketResult::ErrorReplyTimeout) {
    done = true;
  } else {
    server.m_async_broadcaster.BroadcastEvent(eBroadcastBitAsyncContinue);
  }
}

thread_result_t GDBRemoteCommunicationReplayServer::AsyncThread(void *arg) {
  GDBRemoteCommunicationReplayServer *server =
      (GDBRemoteCommunicationReplayServer *)arg;

  EventSP event_sp;

  bool done = false;

  while (!done) {
    printf("Waiting for event\n");
    if (server->m_async_listener_sp->GetEvent(event_sp, llvm::None)) {
      printf("Received async event\n");
      const uint32_t event_type = event_sp->GetType();
      if (event_sp->BroadcasterIs(&server->m_async_broadcaster)) {
        switch (event_type) {
        case eBroadcastBitAsyncContinue:
          ReceivePacket(*server, done);
          break;
        case eBroadcastBitAsyncThreadShouldExit:
          done = true;
          break;
        default:
          done = true;
          break;
        }
      }
    }
  }

  printf("Thread done\n");

  return nullptr;
}
