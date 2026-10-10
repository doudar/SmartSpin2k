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
  std::reverse(digits.begin(), digits.end());  // Peloton sends least significant digit first.
  std::vector<uint8_t> bytes{PELOTON_HEADER, type, static_cast<uint8_t>(digits.size())};
  bytes.insert(bytes.end(), digits.begin(), digits.end());
  uint8_t checksum = 0;
  for (uint8_t byte : bytes) checksum += byte;
  bytes.push_back(checksum);
  return bytes;
}

void decode(PelotonData& sensor, uint8_t type, unsigned value) {
  auto bytes = packet(type, value);
  sensor.decode(bytes.data(), bytes.size());
}

void assertNoFreshData(PelotonData& sensor) {
  TEST_ASSERT_FALSE(sensor.hasPower());
  TEST_ASSERT_FALSE(sensor.hasCadence());
  TEST_ASSERT_FALSE(sensor.hasResistance());
}
}  // namespace

void TestPelotonData::test_packet_freshness() {
  SensorDataFactory factory;
  auto bytes = packet(PELOTON_POW_ID, 1500);
  auto power = factory.getSensorData(PELOTON_DATA_UUID, "Peloton", bytes.data(), bytes.size());
  TEST_ASSERT_TRUE(power->hasPower());
  TEST_ASSERT_FALSE(power->hasCadence());
  TEST_ASSERT_FALSE(power->hasResistance());
  TEST_ASSERT_EQUAL_INT(150, power->getPower());

  bytes = packet(PELOTON_CAD_ID, 90);
  auto cadence = factory.getSensorData(PELOTON_DATA_UUID, "Peloton", bytes.data(), bytes.size());
  TEST_ASSERT_TRUE(power.get() == cadence.get());  // Exercise the cached-parser path used by the collector.
  TEST_ASSERT_FALSE(cadence->hasPower());
  TEST_ASSERT_TRUE(cadence->hasCadence());
  TEST_ASSERT_FALSE(cadence->hasResistance());
  TEST_ASSERT_EQUAL_INT(150, cadence->getPower());
  TEST_ASSERT_EQUAL_FLOAT(90, cadence->getCadence());

  bytes = packet(PELOTON_RES_ID, 42);
  auto resistance = factory.getSensorData(PELOTON_DATA_UUID, "Peloton", bytes.data(), bytes.size());
  TEST_ASSERT_FALSE(resistance->hasPower());
  TEST_ASSERT_FALSE(resistance->hasCadence());
  TEST_ASSERT_TRUE(resistance->hasResistance());
  TEST_ASSERT_EQUAL_INT(42, resistance->getResistance());

  bytes = packet(PELOTON_POW_ID, 1500);  // An actual repeated power report is still fresh.
  power = factory.getSensorData(PELOTON_DATA_UUID, "Peloton", bytes.data(), bytes.size());
  TEST_ASSERT_TRUE(power->hasPower());
  TEST_ASSERT_FALSE(power->hasCadence());
  TEST_ASSERT_FALSE(power->hasResistance());

  bytes = packet(PELOTON_POW_ID, 3500);
  auto other = factory.getSensorData(PELOTON_DATA_UUID, "Other Peloton", bytes.data(), bytes.size());
  TEST_ASSERT_EQUAL_INT(350, other->getPower());  // Measurements are per device, too.
  TEST_ASSERT_EQUAL_INT(150, power->getPower());
}

void TestPelotonData::test_immediate_feedback_and_stops() {
  PelotonData sensor;
  decode(sensor, PELOTON_POW_ID, 1000);
  decode(sensor, PELOTON_CAD_ID, 90);
  for (int i = 0; i < 100; ++i) decode(sensor, PELOTON_RES_ID, 42);
  decode(sensor, PELOTON_CAD_ID, 110);
  TEST_ASSERT_EQUAL_FLOAT(110, sensor.getCadence());
  decode(sensor, PELOTON_POW_ID, 3000);
  TEST_ASSERT_EQUAL_INT(300, sensor.getPower());  // No old 100 W history delays the response.
  TEST_ASSERT_EQUAL_FLOAT(110, sensor.getCadence());

  decode(sensor, PELOTON_CAD_ID, 0);
  TEST_ASSERT_TRUE(sensor.hasCadence());
  TEST_ASSERT_FALSE(sensor.hasPower());
  TEST_ASSERT_EQUAL_FLOAT(0, sensor.getCadence());
  decode(sensor, PELOTON_CAD_ID, 80);
  TEST_ASSERT_EQUAL_FLOAT(80, sensor.getCadence());
  decode(sensor, PELOTON_POW_ID, 0);
  TEST_ASSERT_EQUAL_INT(0, sensor.getPower());
  decode(sensor, PELOTON_POW_ID, 4000);
  TEST_ASSERT_EQUAL_INT(400, sensor.getPower());

  // Round the latest report's tenths of watts.
  PelotonData fractional;
  decode(fractional, PELOTON_POW_ID, 1004);
  decode(fractional, PELOTON_POW_ID, 1014);
  TEST_ASSERT_EQUAL_INT(101, fractional.getPower());
}

void TestPelotonData::test_fast_reports_have_no_history_lag() {
  PelotonData sensor;
  for (int i = 0; i < 100; ++i) decode(sensor, PELOTON_POW_ID, 1000);
  decode(sensor, PELOTON_POW_ID, 3000);
  TEST_ASSERT_EQUAL_INT(300, sensor.getPower());
  decode(sensor, PELOTON_POW_ID, 1000);
  TEST_ASSERT_EQUAL_INT(100, sensor.getPower());
  for (int i = 0; i < 100; ++i) decode(sensor, PELOTON_CAD_ID, 90);
  decode(sensor, PELOTON_CAD_ID, 40);
  TEST_ASSERT_EQUAL_FLOAT(40, sensor.getCadence());
}

void TestPelotonData::test_invalid_packets_do_not_refresh_or_pollute() {
  PelotonData sensor;
  decode(sensor, PELOTON_POW_ID, 1000);
  auto bytes = packet(PELOTON_POW_ID, 3000);
  sensor.decode(bytes.data(), bytes.size() - 1);
  assertNoFreshData(sensor);
  bytes.back() ^= 1;  // Bad checksum.
  sensor.decode(bytes.data(), bytes.size());
  assertNoFreshData(sensor);
  sensor.decode(nullptr, 0);
  assertNoFreshData(sensor);
  uint8_t shortPacket[] = {PELOTON_HEADER, PELOTON_CAD_ID};
  sensor.decode(shortPacket, sizeof(shortPacket));
  assertNoFreshData(sensor);

  bytes = packet(PELOTON_POW_ID, 3000);
  bytes.back() += ':' - bytes[3];
  bytes[3] = ':';  // Invalid ASCII digit with otherwise valid checksum.
  sensor.decode(bytes.data(), bytes.size());
  assertNoFreshData(sensor);
  decode(sensor, PELOTON_POW_ID, 65536);  // Numeric overflow.
  assertNoFreshData(sensor);
  decode(sensor, PELOTON_POW_ID, 60000);  // Outside the accepted raw power range.
  assertNoFreshData(sensor);
  decode(sensor, PELOTON_CAD_ID, 250);
  assertNoFreshData(sensor);
  decode(sensor, PELOTON_RES_ID2, 42);
  assertNoFreshData(sensor);
  decode(sensor, 0x7f, 42);
  assertNoFreshData(sensor);

  decode(sensor, PELOTON_POW_ID, 3000);
  TEST_ASSERT_TRUE(sensor.hasPower());
  TEST_ASSERT_EQUAL_INT(300, sensor.getPower());

  // Captured cadence packet verifies the wire checksum and digit order.
  uint8_t captured[] = {0xf1, 0x41, 0x03, 0x31, 0x35, 0x30, 0xcb};
  sensor.decode(captured, sizeof(captured));
  TEST_ASSERT_TRUE(sensor.hasCadence());
  TEST_ASSERT_EQUAL_FLOAT(51, sensor.getCadence());
}
