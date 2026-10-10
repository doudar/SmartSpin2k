/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <deque>
#include <mutex>
#include <vector>

// Maintenance owns sending/completion. Producers may enqueue from other tasks.
// Never expire an in-flight indication locally: a late ACK cannot be distinguished
// from an ACK for a replacement. The BLE stack owns that transaction's timeout.
template <size_t PeerCount>
class BleTxScheduler {
 public:
  enum class Kind { Response, Snapshot, Log };
  struct Packet {
    uint16_t peer;
    uint64_t session;
    std::vector<uint8_t> data;
    Kind kind;
    uint32_t queued;
  };
  static constexpr size_t CAPACITY = 24;

  bool enqueue(Packet packet) {
    std::lock_guard<std::mutex> lock(mutex);
    if (packet.kind == Kind::Log) {
      for (auto& old : queue) {
        if (old.kind == Kind::Log && old.peer == packet.peer) { old = std::move(packet); return true; }
      }
      if (queue.size() >= CAPACITY / 2) return false;
    }
    if (queue.size() >= CAPACITY) {
      for (auto it = queue.begin(); it != queue.end(); ++it) {
        if (it->kind == Kind::Log) { queue.erase(it); break; }
      }
      if (queue.size() >= CAPACITY) return false;
    }
    queue.push_back(std::move(packet));
    return true;
  }

  void cancelSnapshots() {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto it = queue.begin(); it != queue.end();) {
      if (it->kind == Kind::Snapshot) it = queue.erase(it);
      else ++it;
    }
    // Preserve the outstanding ATT transaction but ignore its eventual snapshot ACK.
    for (auto& slot : busy) if (slot.kind == Kind::Snapshot) slot.kind = Kind::Response;
  }

  bool complete(uint16_t peer, uint64_t session, Kind& kind) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& slot : busy) {
      if (slot.session == session && slot.peer == peer) {
        kind = slot.kind;
        slot = {};
        return true;
      }
    }
    return false;
  }

  template <typename Connected, typename Send>
  bool pump(uint32_t now, bool allowLogs, Connected connected, Send send) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& slot : busy) if (slot.session && !connected(slot.peer, slot.session)) slot = {};
    for (auto it = queue.begin(); it != queue.end();) {
      if (!connected(it->peer, it->session) || uint32_t(now - it->queued) > (it->kind == Kind::Log ? 1000u : 5000u)) it = queue.erase(it);
      else ++it;
    }
    if (uint32_t(now - lastAttempt) < 20) return false;
    // Responses/snapshot chunks precede logs. A slow peer cannot block other peers.
    for (int priority = 0; priority != 2; ++priority) {
      for (int fairness = 0; fairness != 2; ++fairness) {
        for (auto it = queue.begin(); it != queue.end(); ++it) {
          if ((it->peer == lastFailedPeer) != (fairness == 1)) continue;
          const bool log = it->kind == Kind::Log;
          if (log != (priority == 1) || (log && (!allowLogs || uint32_t(now - lastLog) < 250))) continue;
          bool blocked = false;
          for (const auto& slot : busy) if (slot.session && slot.peer == it->peer) blocked = true;
          if (blocked) continue;
          for (auto& slot : busy) {
            if (slot.session) continue;
            lastAttempt = now;
            if (send(*it)) {
              lastFailedPeer = UINT16_MAX;
              slot = {it->peer, it->session, it->kind};
              if (log) lastLog = now;
              queue.erase(it);
            } else {
              lastFailedPeer = it->peer;
            }
            return true; // At most one attempt, including buffer exhaustion.
          }
        }
      }
    }
    return false;
  }

 private:
  struct Busy { uint16_t peer = UINT16_MAX; uint64_t session = 0; Kind kind = Kind::Response; };
  std::mutex mutex;
  std::array<Busy, PeerCount> busy{};
  std::deque<Packet> queue;
  uint32_t lastAttempt = 0, lastLog = 0;
  uint16_t lastFailedPeer = UINT16_MAX;
};
