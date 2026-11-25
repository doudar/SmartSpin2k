/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include "ByteUtils.h"

// Read exactly one TCP frame. Short reads and coalesced frames need no memmove.
// The extended buffer belongs only to the active updater and is reused across chunks.
template <size_t SmallCapacity>
class DirConFrameReader {
 public:
  enum Result { Pending, Complete, Invalid };

  template <typename Client>
  Result read(Client& client, size_t limit) {
    if (limit <= SmallCapacity && extended) {
      if (used > 0 && length > SmallCapacity) return Invalid;
      extended.reset();
    }
    if (!client.available()) return Pending;
    if (used < headerSize) {
      int n = client.read(small + used, headerSize - used);
      if (n < 0) return Invalid;
      used += n;
      if (used < headerSize) return Pending;
      length = headerSize + get_be16(small + 4);
    }
    if (length > limit) return Invalid;
    if (length > SmallCapacity && !extended) {
      extended.reset(new (std::nothrow) uint8_t[limit]);
      if (!extended) return Invalid;
    }
    uint8_t* frame = data();
    if (frame != small && used == headerSize) std::memcpy(frame, small, headerSize);
    if (used != length) {
      int n = client.read(frame + used, length - used);
      if (n < 0) return Invalid;
      used += n;
    }
    return used == length ? Complete : Pending;
  }

  uint8_t* data() { return length > SmallCapacity ? extended.get() : small; }
  size_t size() const { return length; }
  void consume() { used = length = 0; }
  void reset() { consume(); extended.reset(); }

 private:
  static constexpr size_t headerSize = 6;
  uint8_t small[SmallCapacity];
  std::unique_ptr<uint8_t[]> extended;
  size_t used = 0, length = 0;
};
