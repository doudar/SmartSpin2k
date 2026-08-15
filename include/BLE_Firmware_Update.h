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

namespace BleFirmwareUpdate {

constexpr uint8_t PROTOCOL_VERSION = 1;

enum class Command : uint8_t {
  Start  = 0x01,
  Finish = 0x02,
  Abort  = 0x03,
  Query  = 0x04,
};

enum class State : uint8_t {
  Waiting   = 0x00,
  Preparing = 0x01,
  Updating  = 0x02,
  Flashing  = 0x03,
  Verifying = 0x04,
  Rebooting = 0x05,
  Error     = 0xff,
};

enum class Error : uint8_t {
  None               = 0x00,
  InvalidCommand     = 0x01,
  UnsupportedVersion = 0x02,
  InvalidStartPacket = 0x03,
  Busy               = 0x04,
  InvalidImageSize   = 0x05,
  NoUpdatePartition  = 0x06,
  WrongConnection    = 0x07,
  NotStarted         = 0x08,
  EmptyData          = 0x09,
  TooMuchData        = 0x0a,
  InvalidImageHeader = 0x0b,
  OtaBeginFailed     = 0x0c,
  OtaWriteFailed     = 0x0d,
  IncompleteImage    = 0x0e,
  ChecksumMismatch   = 0x0f,
  ImageVerifyFailed  = 0x10,
  SetBootFailed      = 0x11,
  TransferTimedOut   = 0x12,
  Aborted            = 0x13,
  ConnectionLost     = 0x14,
};

constexpr uint8_t CAP_LENGTH_AWARE   = 1U << 0;
constexpr uint8_t CAP_CRC32          = 1U << 1;
constexpr uint8_t CAP_VARIABLE_CHUNK = 1U << 2;
constexpr uint8_t CAP_WRITE_NO_RSP   = 1U << 3;
constexpr uint8_t CAPABILITIES       = CAP_LENGTH_AWARE | CAP_CRC32 | CAP_VARIABLE_CHUNK | CAP_WRITE_NO_RSP;

constexpr size_t START_PACKET_SIZE      = 10;
constexpr size_t STATUS_PACKET_SIZE     = 12;
constexpr size_t MAX_DATA_CHUNK_SIZE    = 512;
constexpr uint32_t TRANSFER_TIMEOUT_MS = 30000;

struct StartRequest {
  uint32_t imageSize;
  uint32_t imageCrc32;
};

inline uint32_t readUint32LE(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) | (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

inline void writeUint32LE(uint8_t* data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
  data[2] = static_cast<uint8_t>(value >> 16);
  data[3] = static_cast<uint8_t>(value >> 24);
}

inline bool parseStartRequest(const uint8_t* data, size_t length, StartRequest& request) {
  if (data == nullptr || length != START_PACKET_SIZE || data[0] != static_cast<uint8_t>(Command::Start) || data[1] != PROTOCOL_VERSION) return false;
  request.imageSize  = readUint32LE(data + 2);
  request.imageCrc32 = readUint32LE(data + 6);
  return true;
}

inline bool hasTransferTimedOut(uint32_t nowMs, uint32_t lastActivityMs) {
  return nowMs - lastActivityMs >= TRANSFER_TIMEOUT_MS;
}

inline std::array<uint8_t, STATUS_PACKET_SIZE> makeStatusPacket(State state, Error error, uint32_t receivedBytes, uint32_t imageSize) {
  std::array<uint8_t, STATUS_PACKET_SIZE> packet{};
  packet[0] = PROTOCOL_VERSION;
  packet[1] = static_cast<uint8_t>(state);
  packet[2] = static_cast<uint8_t>(error);
  packet[3] = CAPABILITIES;
  writeUint32LE(packet.data() + 4, receivedBytes);
  writeUint32LE(packet.data() + 8, imageSize);
  return packet;
}

}  // namespace BleFirmwareUpdate
