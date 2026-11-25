/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>
#include <ss2k_protocol.h>
#include "test.h"

void TestSettingsStreamProtocol::test_begin_shape() {
  uint8_t begin[39] = {SS2K_WRITE, SS2K_ID_FIRMWARE_STREAM, SS2K_OTA_BEGIN};
  TEST_ASSERT_EQUAL(SS2K_INVALID, ss2k_validate_request(begin, sizeof(begin)));
  begin[3] = 1;
  TEST_ASSERT_EQUAL(SS2K_OK, ss2k_validate_request(begin, sizeof(begin)));
  TEST_ASSERT_EQUAL(SS2K_INVALID, ss2k_validate_request(begin, sizeof(begin) - 1));
}

void TestSettingsStreamProtocol::test_chunk_bounds() {
  uint8_t chunk[SS2K_SECURE_MAX_INNER + 1] = {SS2K_WRITE, SS2K_ID_FIRMWARE_STREAM, SS2K_OTA_CHUNK};
  TEST_ASSERT_EQUAL(SS2K_INVALID, ss2k_validate_request(chunk, 7));
  TEST_ASSERT_EQUAL(SS2K_OK, ss2k_validate_request(chunk, 8));
  TEST_ASSERT_EQUAL(SS2K_OK, ss2k_validate_request(chunk, SS2K_SECURE_MAX_INNER));
  TEST_ASSERT_EQUAL(SS2K_INVALID, ss2k_validate_request(chunk, sizeof(chunk)));
}

void TestSettingsStreamProtocol::test_offset_response() {
  uint8_t ack[] = {SS2K_SUCCESS, SS2K_ID_FIRMWARE_STREAM, SS2K_OTA_CHUNK, 0x78, 0x56, 0x34, 0x12};
  ss2k_packet view;
  TEST_ASSERT_EQUAL(SS2K_OK, ss2k_decode_response(ack, sizeof(ack), SS2K_REPLY_WRITE, false, &view));
  TEST_ASSERT_EQUAL(SS2K_FORMAT_OTA_REPLY, view.format);
  TEST_ASSERT_EQUAL(0x12345678, ss2k_get_i32(view.payload + 1));
  TEST_ASSERT_NOT_EQUAL(SS2K_OK, ss2k_decode_response(ack, sizeof(ack) - 1, SS2K_REPLY_WRITE, false, &view));
}

void TestSettingsStreamProtocol::test_finish_and_abort() {
  uint8_t action[] = {SS2K_WRITE, SS2K_ID_FIRMWARE_STREAM, SS2K_OTA_FINISH, 0};
  TEST_ASSERT_EQUAL(SS2K_OK, ss2k_validate_request(action, 3));
  action[2] = SS2K_OTA_ABORT;
  TEST_ASSERT_EQUAL(SS2K_OK, ss2k_validate_request(action, 3));
  TEST_ASSERT_EQUAL(SS2K_INVALID, ss2k_validate_request(action, 4));
}
