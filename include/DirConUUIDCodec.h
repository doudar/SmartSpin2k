/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <vector>

#ifndef PLATFORMIO_ENV_NATIVE
#include <NimBLEUUID.h>
#endif

namespace DirConUUIDCodec {

inline void appendValueBytes(const uint8_t* uuidBytes, std::vector<uint8_t>& message) {
  for (size_t i = 16; i > 0; i--) {
    message.push_back(uuidBytes[i - 1]);
  }
}

#ifndef PLATFORMIO_ENV_NATIVE
inline void appendBytes(NimBLEUUID uuid, std::vector<uint8_t>& message) {
  uuid.to128();
  appendValueBytes(uuid.getValue(), message);
}

inline NimBLEUUID fromBytes(const uint8_t* data) {
  NimBLEUUID uuid(data, 16);
  uuid.reverseByteOrder();
  return uuid;
}
#endif

}  // namespace DirConUUIDCodec
