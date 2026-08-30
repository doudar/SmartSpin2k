/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "CustomCharacteristicProtocol.h"

namespace ScanResultProtocol {

constexpr uint8_t VERSION       = 1;
constexpr size_t HEADER_LENGTH  = 10;

enum class Event : uint8_t {
  Begin  = 0,
  Device = 1,
  End    = 2,
};

// A device body is deliberately compact and independent of JSON. UUID strings
// are at most 36 bytes and advertised BLE names are short, so the body normally
// fits in one negotiated ATT packet. It can still be fragmented for the 23-byte
// minimum MTU.
inline std::vector<uint8_t> makeDeviceBody(const std::string& uuid, const std::string& name) {
  const size_t uuidLength = std::min<size_t>(uuid.length(), UINT8_MAX);
  std::vector<uint8_t> body;
  body.reserve(1 + uuidLength + name.length());
  body.push_back(static_cast<uint8_t>(uuidLength));
  body.insert(body.end(), uuid.begin(), uuid.begin() + uuidLength);
  body.insert(body.end(), name.begin(), name.end());
  return body;
}

inline std::vector<uint8_t> makePacket(Event event, uint16_t scanId, uint16_t sequence, uint8_t fragment, uint8_t fragmentCount,
                                       const uint8_t* payload = nullptr, size_t payloadLength = 0) {
  std::vector<uint8_t> packet(HEADER_LENGTH + payloadLength);
  packet[0] = cc_success;
  packet[1] = BLE_scanResults;
  packet[2] = VERSION;
  packet[3] = static_cast<uint8_t>(event);
  packet[4] = static_cast<uint8_t>(scanId & 0xff);
  packet[5] = static_cast<uint8_t>(scanId >> 8);
  packet[6] = static_cast<uint8_t>(sequence & 0xff);
  packet[7] = static_cast<uint8_t>(sequence >> 8);
  packet[8] = fragment;
  packet[9] = fragmentCount;
  if (payloadLength != 0 && payload != nullptr) {
    std::copy_n(payload, payloadLength, packet.begin() + HEADER_LENGTH);
  }
  return packet;
}

}  // namespace ScanResultProtocol
