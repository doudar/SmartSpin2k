/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>

#include "BLE_Firmware_Update.h"
#include "test.h"

void TestBleFirmwareUpdateProtocol::test_parses_start_packet() {
  uint8_t packet[BleFirmwareUpdate::START_PACKET_SIZE] = {
      static_cast<uint8_t>(BleFirmwareUpdate::Command::Start), BleFirmwareUpdate::PROTOCOL_VERSION, 0x78, 0x56, 0x34, 0x12, 0xef, 0xcd, 0xab, 0x90,
  };
  BleFirmwareUpdate::StartRequest request{};

  TEST_ASSERT_TRUE(BleFirmwareUpdate::parseStartRequest(packet, sizeof(packet), request));
  TEST_ASSERT_EQUAL_HEX32(0x12345678, request.imageSize);
  TEST_ASSERT_EQUAL_HEX32(0x90abcdef, request.imageCrc32);
}

void TestBleFirmwareUpdateProtocol::test_rejects_invalid_start_packets() {
  uint8_t packet[BleFirmwareUpdate::START_PACKET_SIZE]{};
  packet[0] = static_cast<uint8_t>(BleFirmwareUpdate::Command::Start);
  packet[1] = BleFirmwareUpdate::PROTOCOL_VERSION;
  BleFirmwareUpdate::StartRequest request{};

  TEST_ASSERT_FALSE(BleFirmwareUpdate::parseStartRequest(nullptr, sizeof(packet), request));
  TEST_ASSERT_FALSE(BleFirmwareUpdate::parseStartRequest(packet, sizeof(packet) - 1, request));
  packet[1]++;
  TEST_ASSERT_FALSE(BleFirmwareUpdate::parseStartRequest(packet, sizeof(packet), request));
}

void TestBleFirmwareUpdateProtocol::test_encodes_status_packet() {
  const auto packet = BleFirmwareUpdate::makeStatusPacket(BleFirmwareUpdate::State::Flashing, BleFirmwareUpdate::Error::None, 0x12345678, 0x90abcdef);

  TEST_ASSERT_EQUAL(BleFirmwareUpdate::STATUS_PACKET_SIZE, packet.size());
  TEST_ASSERT_LESS_OR_EQUAL(20, packet.size());
  TEST_ASSERT_EQUAL(BleFirmwareUpdate::PROTOCOL_VERSION, packet[0]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(BleFirmwareUpdate::State::Flashing), packet[1]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(BleFirmwareUpdate::Error::None), packet[2]);
  TEST_ASSERT_EQUAL_HEX8(BleFirmwareUpdate::CAPABILITIES, packet[3]);
  TEST_ASSERT_NOT_EQUAL(0, packet[3] & BleFirmwareUpdate::CAP_WRITE_NO_RSP);
  TEST_ASSERT_EQUAL_HEX32(0x12345678, BleFirmwareUpdate::readUint32LE(packet.data() + 4));
  TEST_ASSERT_EQUAL_HEX32(0x90abcdef, BleFirmwareUpdate::readUint32LE(packet.data() + 8));
}

void TestBleFirmwareUpdateProtocol::test_transfer_timeout() {
  TEST_ASSERT_FALSE(BleFirmwareUpdate::hasTransferTimedOut(29999, 0));
  TEST_ASSERT_TRUE(BleFirmwareUpdate::hasTransferTimedOut(30000, 0));
  TEST_ASSERT_TRUE(BleFirmwareUpdate::hasTransferTimedOut(100, UINT32_MAX - 30000));
}
