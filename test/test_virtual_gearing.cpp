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
  TEST_ASSERT_EQUAL_UINT16_ARRAY(expected.teeth, actual.teeth, VirtualGearing::MAX_GEARS);
}

void assertOffsets(const VirtualGearing::Gears& gears, int shiftStep, const int32_t* expected, size_t count) {
  TEST_ASSERT_EQUAL_UINT8(count, gears.count);
  for (size_t i = 0; i < count; ++i) TEST_ASSERT_EQUAL_INT32(expected[i], gears.offsetSteps(static_cast<int>(i + 1), shiftStep));
}

}  // namespace

void TestVirtualGearing::test_tooth_pairs_and_selection() {
  VirtualGearing::Gears gears;
  const uint16_t cassette[] = {11, 12, 13, 14, 15, 17, 19, 21, 24, 28, 30, 34};
  uint16_t pairs[24];
  for (size_t i = 0; i < 12; ++i) { pairs[i] = 5000 + cassette[i]; pairs[i + 12] = 3400 + cassette[i]; }
  TEST_ASSERT_TRUE(gears.assign(pairs, 24));
  TEST_ASSERT_EQUAL_UINT16(3434, gears.teeth[0]);
  TEST_ASSERT_EQUAL_UINT16(5011, gears.teeth[23]);
  TEST_ASSERT_EQUAL_INT(8, gears.startGear());
  for (int i = 1; i <= 24; ++i) {
    const auto selection = gears.selection(i);
    TEST_ASSERT_EQUAL_UINT8(2, selection.frontCount);
    TEST_ASSERT_EQUAL_UINT8(12, selection.rearCount);
    TEST_ASSERT_EQUAL_UINT8(gears.teeth[i - 1] / 100 == 34 ? 1 : 2, selection.front);
    TEST_ASSERT_EQUAL_UINT16(cassette[12 - selection.rear], gears.teeth[i - 1] % 100);
  }
  TEST_ASSERT_EQUAL_UINT8(0, gears.selection(0).frontCount);
  TEST_ASSERT_EQUAL_UINT8(0, gears.selection(25).frontCount);
  const uint16_t equalRatios[] = {5025, 3417};
  TEST_ASSERT_TRUE(gears.assign(equalRatios, 2));
  TEST_ASSERT_EQUAL_UINT16(3417, gears.teeth[0]);
  TEST_ASSERT_EQUAL_UINT16(5025, gears.teeth[1]);
  TEST_ASSERT_EQUAL_INT32(0, gears.offsetSteps(2, 1200));
  TEST_ASSERT_EQUAL_INT(2, gears.closestGear(20000, 2)); // Equal ratios retain the selected tooth pair.
  TEST_ASSERT_EQUAL_INT(1, gears.closestGear(20000, 0));
  const uint16_t oneBy[] = {3210, 3252, 3224};
  TEST_ASSERT_TRUE(gears.assign(oneBy, 3));
  TEST_ASSERT_EQUAL_UINT8(1, gears.selection(2).frontCount);
  TEST_ASSERT_EQUAL_UINT8(2, gears.selection(2).rear);
  TEST_ASSERT_TRUE(gears.assign(nullptr, 0));
  TEST_ASSERT_EQUAL_UINT8(0, gears.selection(1).frontCount);
  TEST_ASSERT_EQUAL_INT(0, gears.closestGear(16800, 8));
  TEST_ASSERT_TRUE(gears.assignPreset(VirtualGearing::MIXED_TERRAIN_1X24));
  const uint16_t mixed[] = {750,870,990,1110,1230,1380,1530,1680,1860,2040,2220,2400,
                            2610,2820,3030,3240,3490,3740,3990,4240,4540,4840,5140,5490};
  TEST_ASSERT_EQUAL_UINT8(24, gears.count);
  TEST_ASSERT_EQUAL_UINT16_ARRAY(mixed, gears.ratios, 24);
  TEST_ASSERT_EQUAL_INT(8, gears.closestGear(16800, 24));
  TEST_ASSERT_EQUAL_INT(13, gears.closestGear(26099, 8));
  TEST_ASSERT_EQUAL_INT(1, gears.closestGear(1, 8));
  TEST_ASSERT_EQUAL_INT(24, gears.closestGear(UINT32_MAX, 8));
  TEST_ASSERT_EQUAL_INT(0, gears.closestGear(0, 8));
  TEST_ASSERT_EQUAL_INT(8, gears.startGear());
  for (int gear = 1; gear <= 24; ++gear) {
    const auto selected = gears.selection(gear);
    TEST_ASSERT_EQUAL_UINT8(1, selected.frontCount);
    TEST_ASSERT_EQUAL_UINT8(gear, selected.rear);
    TEST_ASSERT_EQUAL_UINT8(24, selected.rearCount);
    TEST_ASSERT_EQUAL_UINT16(0, gears.teeth[gear-1]);
  }
  TEST_ASSERT_EQUAL_INT32(5314, gears.offsetSteps(8, 1200));
  const auto preset = gears;
  TEST_ASSERT_FALSE(gears.assignPreset(2));
  TEST_ASSERT_TRUE(gears == preset);
  TEST_ASSERT_TRUE(gears.assign(oneBy, 3));
  TEST_ASSERT_EQUAL_UINT8(VirtualGearing::CUSTOM_TEETH, gears.preset);
  TEST_ASSERT_FALSE(gears == preset);
  TEST_ASSERT_TRUE(gears.assignPreset(0));
  TEST_ASSERT_TRUE(gears.unlimited());
}

void TestVirtualGearing::test_tooth_packet_validation() {
  VirtualGearing::Gears gears;
  const uint8_t packet[] = {2, 0xc7, 0x14, 0xd4, 0x14}; // 5319, 5332, unsorted.
  TEST_ASSERT_TRUE(gears.decode(packet, sizeof(packet)));
  TEST_ASSERT_EQUAL_UINT16(5332, gears.teeth[0]);
  TEST_ASSERT_EQUAL_UINT16(1656, gears.ratios[0]);
  const auto saved = gears;
  for (size_t n = 0; n < sizeof(packet); ++n) {
    TEST_ASSERT_FALSE(gears.decode(packet, n));
    assertUnchanged(saved, gears);
  }
  const uint16_t invalid[][2] = {{0, 5332}, {5300, 5332}, {32, 5332}, {10032, 5332}, {9910, 5332}, {1099, 5332}, {5332, 5332}};
  for (const auto& pair : invalid) {
    TEST_ASSERT_FALSE(gears.assign(pair, 2));
    assertUnchanged(saved, gears);
  }
  TEST_ASSERT_FALSE(gears.assign(nullptr, 2));
  TEST_ASSERT_FALSE(gears.assign(saved.teeth, 27));
  TEST_ASSERT_FALSE(gears.assign(saved.teeth, 1));
  const uint8_t unlimited[] = {0};
  TEST_ASSERT_TRUE(gears.decode(unlimited, 1));
  TEST_ASSERT_TRUE(gears.unlimited());
}

void TestVirtualGearing::test_ratio_api() {
  VirtualGearing::Gears gears;
  TEST_ASSERT_TRUE(gears.unlimited());
  const uint16_t initial[] = {1010, 2010, 5011};
  TEST_ASSERT_TRUE(gears.assign(initial, 3));
  TEST_ASSERT_EQUAL_UINT16(1000, gears.ratios[gears.clampGear(-500) - 1]);
  TEST_ASSERT_EQUAL_UINT16(4545, gears.ratios[gears.clampGear(500) - 1]);

  const VirtualGearing::Gears original = gears;
  uint16_t values[26];
  for (int i = 0; i < 26; ++i) values[i] = static_cast<uint16_t>((5 + 2 * i) * 100 + 10);
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
  const uint16_t invalid[] = {510, 6101};
  TEST_ASSERT_FALSE(gears.assign(invalid, 2));
  assertUnchanged(original, gears);
}

void TestVirtualGearing::test_offset_normalization() {
  VirtualGearing::Gears gears;

  // Three gaps exercise the odd median: median(100, 200, 300) == 200.
  const uint16_t oddRatios[] = {1010, 1110, 1310, 1610};
  TEST_ASSERT_TRUE(gears.assign(oddRatios, 4));
  const int32_t oddExpected[] = {0, 50, 150, 300};
  assertOffsets(gears, 100, oddExpected, 4);

  // Four gaps exercise the even median: (200 + 300) / 2 == 250.
  const uint16_t evenRatios[] = {1010, 1110, 1310, 1610, 2010};
  TEST_ASSERT_TRUE(gears.assign(evenRatios, 5));
  const int32_t evenExpected[] = {0, 40, 120, 240, 400};
  assertOffsets(gears, 100, evenExpected, 5);

  const uint16_t uniformRatios[] = {1010, 1110, 1210, 1310};
  TEST_ASSERT_TRUE(gears.assign(uniformRatios, 4));
  const int32_t uniformExpected[] = {0, 50, 100, 150};
  assertOffsets(gears, 50, uniformExpected, 4);
  for (int gear = 1; gear <= 4; ++gear) {
    TEST_ASSERT_EQUAL_INT32(2 * gears.offsetSteps(gear, 50), gears.offsetSteps(gear, 100));
  }
}

void TestVirtualGearing::test_duplicate_and_identical_ratios() {
  VirtualGearing::Gears gears;
  const uint16_t duplicateRatios[] = {1010, 2020, 1110, 2220, 1310};
  TEST_ASSERT_TRUE(gears.assign(duplicateRatios, 5));
  // Zero gaps are excluded before taking the even median (100 + 200) / 2.
  const int32_t expected[] = {0, 0, 67, 67, 200};
  assertOffsets(gears, 100, expected, 5);

  const uint16_t identicalRatios[] = {1210, 2420, 3630, 4840};
  TEST_ASSERT_TRUE(gears.assign(identicalRatios, 4));
  const int32_t allZero[] = {0, 0, 0, 0};
  assertOffsets(gears, INT_MAX, allZero, 4);
}

void TestVirtualGearing::test_profile_bounds_and_scaling() {
  VirtualGearing::Gears gears;
  uint16_t values[22];
  for (int i = 0; i < 22; ++i) values[i] = static_cast<uint16_t>(1010 + 100 * i);
  TEST_ASSERT_TRUE(gears.assign(values, 22));
  TEST_ASSERT_EQUAL_UINT8(22, gears.count);
  TEST_ASSERT_EQUAL_INT(22, gears.clampGear(1000));
  TEST_ASSERT_EQUAL_INT32(2100, gears.offsetSteps(22, 100));

  const uint16_t shorter[] = {1010, 1210, 1410, 1610, 1810, 2010, 2210, 2410, 2610, 2810, 3010, 3210, 3410};
  TEST_ASSERT_TRUE(gears.assign(shorter, sizeof(shorter) / sizeof(shorter[0])));
  TEST_ASSERT_EQUAL_UINT8(13, gears.count);
  TEST_ASSERT_EQUAL_INT(13, gears.clampGear(22));
  TEST_ASSERT_EQUAL_INT32(1200, gears.offsetSteps(22, 100));
  TEST_ASSERT_EQUAL_INT32(1200, gears.offsetSteps(13, 100));
  TEST_ASSERT_EQUAL_INT32(0, gears.offsetSteps(0, 100));
}

void TestVirtualGearing::test_offset_overflow() {
  VirtualGearing::Gears gears;
  const uint16_t ratios[] = {5050, 5150, 5250};
  TEST_ASSERT_TRUE(gears.assign(ratios, 3));
  TEST_ASSERT_EQUAL_INT32(INT32_MAX, gears.offsetSteps(3, INT32_MAX));
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, gears.offsetSteps(3, INT32_MIN));
  TEST_ASSERT_EQUAL_INT32(INT32_MAX, gears.offsetSteps(3, INT_MAX));
}

void TestVirtualGearing::test_packet_validation() {
  VirtualGearing::Gears gears;
  const uint16_t originalRatios[] = {1010, 1210, 1510, 2110};
  TEST_ASSERT_TRUE(gears.assign(originalRatios, 4));

  uint8_t packet[1 + 2 * 26] = {26};
  for (int i = 0; i < 26; ++i) put_le16(packet + 1 + i * 2, static_cast<uint16_t>((5 + 2 * i) * 100 + 10));
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

  const uint16_t bounded[] = {1010, 1110, 1310, 1610};
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
