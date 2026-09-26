/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <climits>
#include <cstring>

#include <unity.h>

#include "ByteUtils.h"
#include "VirtualGearing.h"
#include "test.h"

namespace {

void assertUnchanged(const VirtualGearing::Gears& expected, const VirtualGearing::Gears& actual) {
  TEST_ASSERT_EQUAL_UINT8(expected.count, actual.count);
  TEST_ASSERT_EQUAL_UINT16_ARRAY(expected.ratios, actual.ratios, sizeof(expected.ratios) / sizeof(expected.ratios[0]));
}

void assertOffsets(const VirtualGearing::Gears& gears, int shiftStep, const int32_t* expected, size_t count) {
  TEST_ASSERT_EQUAL_UINT8(count, gears.count);
  for (size_t i = 0; i < count; ++i) TEST_ASSERT_EQUAL_INT32(expected[i], gears.offsetSteps(static_cast<int>(i + 1), shiftStep));
}

}  // namespace

void TestVirtualGearing::test_ratio_api() {
  VirtualGearing::Gears gears;
  TEST_ASSERT_TRUE(gears.unlimited());
  const uint16_t initial[] = {1000, 2000, 4545};
  TEST_ASSERT_TRUE(gears.assign(initial, 3));
  TEST_ASSERT_EQUAL_UINT16(1000, gears.ratios[gears.clampGear(-500) - 1]);
  TEST_ASSERT_EQUAL_UINT16(4545, gears.ratios[gears.clampGear(500) - 1]);

  const VirtualGearing::Gears original = gears;
  uint16_t values[26];
  for (int i = 0; i < 26; ++i) values[i] = static_cast<uint16_t>(500 + 200 * i);
  const int counts[] = {2, 3, 11, 12, 13, 22, 24, 26};
  const int startGears[] = {1, 1, 3, 4, 4, 7, 8, 8};
  for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
    const int count = counts[i];
    TEST_ASSERT_TRUE(gears.assign(values, count));
    TEST_ASSERT_EQUAL_UINT8(count, gears.count);
    TEST_ASSERT_EQUAL_INT(startGears[i], gears.startGear());
    TEST_ASSERT_EQUAL_INT(count, gears.clampGear(1000));
    TEST_ASSERT_EQUAL_INT(1, gears.clampGear(-1));
  }
  TEST_ASSERT_FALSE(gears.assign(values, 27));
  TEST_ASSERT_FALSE(gears.assign(values, 1));
  TEST_ASSERT_FALSE(gears.assign(nullptr, 10));

  gears = original;
  const uint16_t descending[] = {2000, 1000};
  TEST_ASSERT_FALSE(gears.assign(descending, 2));
  assertUnchanged(original, gears);
  const uint16_t invalid[] = {500, 6001};
  TEST_ASSERT_FALSE(gears.assign(invalid, 2));
  assertUnchanged(original, gears);
}

void TestVirtualGearing::test_offset_normalization() {
  VirtualGearing::Gears gears;

  // Three gaps exercise the odd median: median(100, 200, 300) == 200.
  const uint16_t oddRatios[] = {1000, 1100, 1300, 1600};
  TEST_ASSERT_TRUE(gears.assign(oddRatios, 4));
  const int32_t oddExpected[] = {0, 50, 150, 300};
  assertOffsets(gears, 100, oddExpected, 4);

  // Four gaps exercise the even median: (200 + 300) / 2 == 250.
  const uint16_t evenRatios[] = {1000, 1100, 1300, 1600, 2000};
  TEST_ASSERT_TRUE(gears.assign(evenRatios, 5));
  const int32_t evenExpected[] = {0, 40, 120, 240, 400};
  assertOffsets(gears, 100, evenExpected, 5);

  const uint16_t uniformRatios[] = {1000, 1100, 1200, 1300};
  TEST_ASSERT_TRUE(gears.assign(uniformRatios, 4));
  const int32_t uniformExpected[] = {0, 50, 100, 150};
  assertOffsets(gears, 50, uniformExpected, 4);
  for (int gear = 1; gear <= 4; ++gear) {
    TEST_ASSERT_EQUAL_INT32(2 * gears.offsetSteps(gear, 50), gears.offsetSteps(gear, 100));
  }
}

void TestVirtualGearing::test_duplicate_and_identical_ratios() {
  VirtualGearing::Gears gears;
  const uint16_t duplicateRatios[] = {1000, 1000, 1100, 1100, 1300};
  TEST_ASSERT_TRUE(gears.assign(duplicateRatios, 5));
  // Zero gaps are excluded before taking the even median (100 + 200) / 2.
  const int32_t expected[] = {0, 0, 67, 67, 200};
  assertOffsets(gears, 100, expected, 5);

  const uint16_t identicalRatios[] = {1200, 1200, 1200, 1200};
  TEST_ASSERT_TRUE(gears.assign(identicalRatios, 4));
  const int32_t allZero[] = {0, 0, 0, 0};
  assertOffsets(gears, INT_MAX, allZero, 4);
}

void TestVirtualGearing::test_profile_bounds_and_scaling() {
  VirtualGearing::Gears gears;
  uint16_t values[22];
  for (int i = 0; i < 22; ++i) values[i] = static_cast<uint16_t>(1000 + 100 * i);
  TEST_ASSERT_TRUE(gears.assign(values, 22));
  TEST_ASSERT_EQUAL_UINT8(22, gears.count);
  TEST_ASSERT_EQUAL_INT(22, gears.clampGear(1000));
  TEST_ASSERT_EQUAL_INT32(2100, gears.offsetSteps(22, 100));

  const uint16_t shorter[] = {1000, 1200, 1400, 1600, 1800, 2000, 2200, 2400, 2600, 2800, 3000, 3200, 3400};
  TEST_ASSERT_TRUE(gears.assign(shorter, sizeof(shorter) / sizeof(shorter[0])));
  TEST_ASSERT_EQUAL_UINT8(13, gears.count);
  TEST_ASSERT_EQUAL_INT(13, gears.clampGear(22));
  TEST_ASSERT_EQUAL_INT32(1200, gears.offsetSteps(22, 100));
  TEST_ASSERT_EQUAL_INT32(1200, gears.offsetSteps(13, 100));
  TEST_ASSERT_EQUAL_INT32(0, gears.offsetSteps(0, 100));
}

void TestVirtualGearing::test_offset_overflow() {
  VirtualGearing::Gears gears;
  const uint16_t ratios[] = {500, 501, 502};
  TEST_ASSERT_TRUE(gears.assign(ratios, 3));
  TEST_ASSERT_EQUAL_INT32(INT32_MAX, gears.offsetSteps(3, INT32_MAX));
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, gears.offsetSteps(3, INT32_MIN));
  TEST_ASSERT_EQUAL_INT32(INT32_MAX, gears.offsetSteps(3, INT_MAX));
}

void TestVirtualGearing::test_packet_validation() {
  VirtualGearing::Gears gears;
  const uint16_t originalRatios[] = {1000, 1200, 1500, 2100};
  TEST_ASSERT_TRUE(gears.assign(originalRatios, 4));

  uint8_t packet[1 + 2 * 26] = {26};
  for (int i = 0; i < 26; ++i) put_le16(packet + 1 + i * 2, static_cast<uint16_t>(500 + 200 * i));
  TEST_ASSERT_TRUE(gears.decode(packet, sizeof(packet)));
  TEST_ASSERT_EQUAL_UINT8(26, gears.count);
  for (int i = 0; i < 26; ++i) TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(500 + 200 * i), gears.ratios[i]);
  const VirtualGearing::Gears complete = gears;

  for (size_t length = 0; length < sizeof(packet); ++length) {
    TEST_ASSERT_FALSE(gears.decode(packet, length));
    assertUnchanged(complete, gears);
  }
  uint8_t oversized[sizeof(packet) + 1] = {0};
  memcpy(oversized, packet, sizeof(packet));
  TEST_ASSERT_FALSE(gears.decode(oversized, sizeof(oversized)));
  assertUnchanged(complete, gears);

  packet[0] = 27;
  TEST_ASSERT_FALSE(gears.decode(packet, sizeof(packet)));
  assertUnchanged(complete, gears);
  packet[0] = 26;
  packet[1 + 2 * 3] = 0;
  packet[1 + 2 * 3 + 1] = 0;
  TEST_ASSERT_FALSE(gears.decode(packet, sizeof(packet)));
  assertUnchanged(complete, gears);

  // A malformed packet never partially replaces an already valid profile.
  const uint8_t malformed[] = {3, 0xE8, 0x03, 0xD0};
  TEST_ASSERT_FALSE(gears.decode(malformed, sizeof(malformed)));
  assertUnchanged(complete, gears);
}

void TestVirtualGearing::test_unlimited_default_and_wire() {
  VirtualGearing::Gears gears;
  TEST_ASSERT_TRUE(gears.unlimited());
  TEST_ASSERT_EQUAL_UINT8(0, gears.count);
  TEST_ASSERT_EQUAL_INT(8, gears.startGear());
  for (int gear : {-1000, -1, 0, 1, 1000}) {
    TEST_ASSERT_EQUAL_INT(gear, gears.clampGear(gear));
    TEST_ASSERT_EQUAL_INT32(gear * 1200, gears.offsetSteps(gear, 1200));
  }
  TEST_ASSERT_EQUAL_INT32(INT32_MAX, gears.offsetSteps(INT_MAX, 1200));
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, gears.offsetSteps(INT_MIN, 1200));

  const uint16_t bounded[] = {1000, 1100, 1300, 1600};
  TEST_ASSERT_TRUE(gears.assign(bounded, 4));
  TEST_ASSERT_FALSE(gears.unlimited());
  TEST_ASSERT_EQUAL_INT(1, gears.startGear());
  TEST_ASSERT_EQUAL_INT(1, gears.clampGear(-10));
  const uint8_t unlimitedPacket[] = {0};
  TEST_ASSERT_TRUE(gears.decode(unlimitedPacket, sizeof(unlimitedPacket)));
  TEST_ASSERT_TRUE(gears.unlimited());
  TEST_ASSERT_EQUAL_INT32(-1200, gears.offsetSteps(-1, 1200));
  for (size_t i = 0; i < VirtualGearing::MAX_GEARS; ++i) TEST_ASSERT_EQUAL_UINT16(0, gears.ratios[i]);
  const uint8_t invalidPacket[] = {0, 0};
  TEST_ASSERT_FALSE(gears.decode(invalidPacket, sizeof(invalidPacket)));
  TEST_ASSERT_FALSE(gears.decode(nullptr, 1));
  TEST_ASSERT_TRUE(gears.unlimited());
  TEST_ASSERT_TRUE(gears.assign(nullptr, 0));
  TEST_ASSERT_FALSE(gears.assign(bounded, 1));
  TEST_ASSERT_TRUE(gears.unlimited());
}
