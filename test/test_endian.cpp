/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>

#include "ByteUtils.h"
#include "test.h"

void TestEndian::test_little_endian_signed_decode_and_round_trip(void) {
  const uint8_t positiveGradient[] = {0x2C, 0x01};
  const uint8_t negativeGradient[] = {0xD4, 0xFE};
  const uint8_t minimumValue[] = {0x00, 0x80};

  TEST_ASSERT_EQUAL_INT16(300, get_le16s(positiveGradient));
  TEST_ASSERT_EQUAL_INT16(-300, get_le16s(negativeGradient));
  TEST_ASSERT_EQUAL_INT16(INT16_MIN, get_le16s(minimumValue));
  TEST_ASSERT_EQUAL_UINT16(65236, get_le16(negativeGradient));

  uint8_t encoded16[2];
  put_le16s(encoded16, -300);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(negativeGradient, encoded16, sizeof(encoded16));

  uint8_t encoded32[4];
  put_le32s(encoded32, -123456);
  TEST_ASSERT_EQUAL_INT32(-123456, get_le32s(encoded32));

  const uint8_t dirConLength[] = {0x12, 0x34};
  TEST_ASSERT_EQUAL_UINT16(0x1234, get_be16(dirConLength));

  uint8_t encodedBigEndian[2];
  put_be16(encodedBigEndian, 0xBEEF);
  const uint8_t expectedBigEndian[] = {0xBE, 0xEF};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedBigEndian, encodedBigEndian, sizeof(encodedBigEndian));
}
