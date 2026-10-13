/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <algorithm>
#include <vector>
#include <unity.h>
#include "Constants.h"
#include "sensors/PelotonData.h"
#include "sensors/SensorDataFactory.h"
#include "test.h"

namespace {
std::vector<uint8_t> packet(uint8_t type, unsigned value) {
  std::string digits = std::to_string(value);
  std::reverse(digits.begin(), digits.end());
  std::vector<uint8_t> bytes{PELOTON_HEADER, type, static_cast<uint8_t>(digits.size())};
  bytes.insert(bytes.end(), digits.begin(), digits.end());
  uint8_t checksum = 0;
  for (uint8_t byte : bytes) checksum += byte;
  bytes.push_back(checksum);
  return bytes;
}
void decode(PelotonData& sensor, uint8_t type, unsigned value, uint32_t now) {
  auto bytes = packet(type, value);
  sensor.decode(bytes.data(), bytes.size(), now);
}
void assertNoFreshData(PelotonData& sensor) {
  TEST_ASSERT_FALSE(sensor.hasPower());
  TEST_ASSERT_FALSE(sensor.hasCadence());
  TEST_ASSERT_FALSE(sensor.hasResistance());
}
}

void TestPelotonData::test_window_averages_and_boundaries() {
  PelotonData sensor;
  decode(sensor, PELOTON_POW_ID, 1000, 0);
  decode(sensor, PELOTON_CAD_ID, 80, 20);
  decode(sensor, PELOTON_RES_ID, 40, 40);
  decode(sensor, PELOTON_POW_ID, 3000, 500);
  decode(sensor, PELOTON_CAD_ID, 100, 600);
  decode(sensor, PELOTON_RES_ID, 44, 999);
  assertNoFreshData(sensor);
  decode(sensor, PELOTON_POW_ID, 9000, 1000);
  TEST_ASSERT_TRUE(sensor.hasPower());
  TEST_ASSERT_TRUE(sensor.hasCadence());
  TEST_ASSERT_TRUE(sensor.hasResistance());
  TEST_ASSERT_EQUAL_INT(200, sensor.getPower());
  TEST_ASSERT_EQUAL_FLOAT(90, sensor.getCadence());
  TEST_ASSERT_EQUAL_INT(42, sensor.getResistance());
  decode(sensor, PELOTON_CAD_ID, 60, 1100);
  assertNoFreshData(sensor);
  decode(sensor, PELOTON_CAD_ID, 70, 2000);
  TEST_ASSERT_EQUAL_INT(900, sensor.getPower());  // Boundary packet starts the next window.
  TEST_ASSERT_EQUAL_FLOAT(60, sensor.getCadence());
  TEST_ASSERT_FALSE(sensor.hasResistance());
  decode(sensor, PELOTON_RES_ID, 40, 3000);
  TEST_ASSERT_FALSE(sensor.hasPower());  // Empty power windows cannot refresh held watts.
  TEST_ASSERT_TRUE(sensor.hasCadence());
}

void TestPelotonData::test_independent_counts_precision_and_stops() {
  PelotonData sensor;
  decode(sensor, PELOTON_POW_ID, 1004, 0);
  decode(sensor, PELOTON_POW_ID, 1014, 10);
  for (int i = 0; i < 100; ++i) decode(sensor, PELOTON_RES_ID, 42, 20 + i);
  decode(sensor, PELOTON_CAD_ID, 80, 200);
  decode(sensor, PELOTON_CAD_ID, 81, 300);
  decode(sensor, PELOTON_CAD_ID, 0, 1000);
  TEST_ASSERT_EQUAL_INT(101, sensor.getPower());  // Round after averaging wire tenths of watts.
  TEST_ASSERT_EQUAL_FLOAT(80.5f, sensor.getCadence());
  TEST_ASSERT_EQUAL_INT(42, sensor.getResistance());
  decode(sensor, PELOTON_POW_ID, 0, 1010);
  decode(sensor, PELOTON_RES_ID, 0, 1020);
  decode(sensor, PELOTON_POW_ID, 4000, 2000);
  TEST_ASSERT_TRUE(sensor.hasPower());
  TEST_ASSERT_TRUE(sensor.hasCadence());
  TEST_ASSERT_TRUE(sensor.hasResistance());
  TEST_ASSERT_EQUAL_INT(0, sensor.getPower());
  TEST_ASSERT_EQUAL_FLOAT(0, sensor.getCadence());
  TEST_ASSERT_EQUAL_INT(0, sensor.getResistance());
  decode(sensor, PELOTON_POW_ID, 4000, 3000);
  TEST_ASSERT_EQUAL_INT(400, sensor.getPower());
}

void TestPelotonData::test_gap_wrap_and_device_isolation() {
  PelotonData sensor;
  decode(sensor, PELOTON_POW_ID, 1000, 0);
  decode(sensor, PELOTON_POW_ID, 3000, 2000);
  assertNoFreshData(sensor);  // Discard interrupted windows.
  decode(sensor, PELOTON_POW_ID, 4000, 3000);
  TEST_ASSERT_EQUAL_INT(300, sensor.getPower());
  PelotonData wrapped;
  const uint32_t start = UINT32_MAX - 499;
  decode(wrapped, PELOTON_POW_ID, 1000, start);
  decode(wrapped, PELOTON_POW_ID, 3000, start + 500);
  decode(wrapped, PELOTON_CAD_ID, 90, start + 999);
  assertNoFreshData(wrapped);
  decode(wrapped, PELOTON_CAD_ID, 90, start + 1000);
  TEST_ASSERT_TRUE(wrapped.hasPower());
  TEST_ASSERT_EQUAL_INT(200, wrapped.getPower());
  SensorDataFactory factory;
  auto first = factory.getSensorData(PELOTON_DATA_UUID, "Peloton", nullptr, 0);
  auto second = factory.getSensorData(PELOTON_DATA_UUID, "Other Peloton", nullptr, 0);
  auto cached = factory.getSensorData(PELOTON_DATA_UUID, "Peloton", nullptr, 0);
  TEST_ASSERT_TRUE(first.get() == cached.get());
  TEST_ASSERT_TRUE(first.get() != second.get());
  decode(static_cast<PelotonData&>(*first), PELOTON_POW_ID, 1000, 0);
  decode(static_cast<PelotonData&>(*second), PELOTON_POW_ID, 3000, 0);
  decode(static_cast<PelotonData&>(*cached), PELOTON_CAD_ID, 90, 1000);
  decode(static_cast<PelotonData&>(*second), PELOTON_CAD_ID, 90, 1000);
  TEST_ASSERT_EQUAL_INT(100, first->getPower());
  TEST_ASSERT_EQUAL_INT(300, second->getPower());
}

void TestPelotonData::test_invalid_packets_do_not_refresh_or_pollute() {
  PelotonData sensor;
  decode(sensor, PELOTON_POW_ID, 1000, 0);
  auto bytes = packet(PELOTON_POW_ID, 3000);
  sensor.decode(bytes.data(), bytes.size() - 1, 500);
  assertNoFreshData(sensor);
  bytes.back() ^= 1;
  sensor.decode(bytes.data(), bytes.size(), 500);
  assertNoFreshData(sensor);
  sensor.decode(nullptr, 0, 500);
  assertNoFreshData(sensor);
  uint8_t shortPacket[] = {PELOTON_HEADER, PELOTON_CAD_ID};
  sensor.decode(shortPacket, sizeof(shortPacket), 500);
  assertNoFreshData(sensor);
  bytes = packet(PELOTON_POW_ID, 3000);
  bytes.back() += ':' - bytes[3];
  bytes[3] = ':';  // Invalid digit, valid checksum.
  sensor.decode(bytes.data(), bytes.size(), 500);
  assertNoFreshData(sensor);
  decode(sensor, PELOTON_POW_ID, 65536, 500);
  assertNoFreshData(sensor);
  decode(sensor, PELOTON_POW_ID, 60000, 500);
  assertNoFreshData(sensor);
  decode(sensor, PELOTON_CAD_ID, 250, 500);
  assertNoFreshData(sensor);
  decode(sensor, PELOTON_RES_ID, 101, 500);
  assertNoFreshData(sensor);
  decode(sensor, PELOTON_RES_ID2, 42, 500);
  assertNoFreshData(sensor);
  decode(sensor, 0x7f, 42, 500);
  assertNoFreshData(sensor);
  uint8_t captured[] = {0xf1, 0x41, 0x03, 0x31, 0x35, 0x30, 0xcb};
  sensor.decode(captured, sizeof(captured), 500);
  assertNoFreshData(sensor);
  sensor.decode(nullptr, 0, 1000);  // Invalid traffic cannot publish a window.
  assertNoFreshData(sensor);
  decode(sensor, PELOTON_POW_ID, 3000, 1010);
  TEST_ASSERT_TRUE(sensor.hasPower());
  TEST_ASSERT_TRUE(sensor.hasCadence());
  TEST_ASSERT_FALSE(sensor.hasResistance());
  TEST_ASSERT_EQUAL_INT(100, sensor.getPower());
  TEST_ASSERT_EQUAL_FLOAT(51, sensor.getCadence());
}
