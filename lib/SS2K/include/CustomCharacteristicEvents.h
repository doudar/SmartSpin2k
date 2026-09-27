/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

// BLE callbacks only enqueue work. Session tokens prevent queued writes/status
// callbacks from being applied after a connection handle has been reused.
template <size_t PeerCount>
class CustomCharacteristicEvents {
 public:
  static constexpr size_t CAPACITY = 8;
  struct Event {
    uint16_t peer = UINT16_MAX, mtu = 23;
    uint64_t session = 0;
    int status = 0;
    bool isStatus = false;
    std::string value;
  };

  void connect(uint16_t peer) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& slot : peers) {
      if (slot.peer == peer) {
        slot.session = ++generation;
        return;
      }
    }
    for (auto& slot : peers) {
      if (!slot.session) {
        slot = {peer, ++generation};
        return;
      }
    }
  }

  void disconnect(uint16_t peer) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& slot : peers) if (slot.peer == peer) slot = {};
  }

  bool push(Event event) {
    std::lock_guard<std::mutex> lock(mutex);
    event.session = sessionFor(event.peer);
    if (!event.session) return false;
    // Leave one slot for an acknowledgment when writes arrive in a burst.
    if (count >= (event.isStatus ? CAPACITY : CAPACITY - 1)) {
      ++dropped;
      return false;
    }
    events[(head + count) % CAPACITY] = std::move(event);
    ++count;
    return true;
  }

  bool pop(Event& event) {
    std::lock_guard<std::mutex> lock(mutex);
    while (count) {
      event = std::move(events[head]);
      head = (head + 1) % CAPACITY;
      --count;
      if (event.session == sessionFor(event.peer)) return true;
    }
    return false;
  }

  bool connected(uint16_t peer, uint64_t session) {
    std::lock_guard<std::mutex> lock(mutex);
    return session && session == sessionFor(peer);
  }

  unsigned takeDropped() {
    std::lock_guard<std::mutex> lock(mutex);
    const unsigned result = dropped;
    dropped = 0;
    return result;
  }

 private:
  struct Peer { uint16_t peer = UINT16_MAX; uint64_t session = 0; };
  uint64_t sessionFor(uint16_t peer) const {
    for (const auto& slot : peers) if (slot.peer == peer) return slot.session;
    return 0;
  }
  std::mutex mutex;
  std::array<Peer, PeerCount> peers{};
  std::array<Event, CAPACITY> events{};
  size_t head = 0, count = 0;
  uint64_t generation = 0;
  unsigned dropped = 0;
};
