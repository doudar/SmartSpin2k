/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "sdkconfig.h"
#include <unity.h>
#include <sensors/FitnessMachineIndoorBikeData.h>
#include "test.h"

static uint8_t data[9] = {0x44, 0x02, 0xf2, 0x08, 0xb0, 0x00, 0x40, 0x00, 0x00};

static void assertNoMeasurements(FitnessMachineIndoorBikeData& sensor) {
  TEST_ASSERT_FALSE(sensor.hasSpeed());
  TEST_ASSERT_FALSE(sensor.hasCadence());
  TEST_ASSERT_FALSE(sensor.hasResistance());
  TEST_ASSERT_FALSE(sensor.hasPower());
  TEST_ASSERT_FALSE(sensor.hasHeartRate());
  TEST_ASSERT_TRUE(std::isnan(sensor.getSpeed()));
  TEST_ASSERT_TRUE(std::isnan(sensor.getCadence()));
  TEST_ASSERT_EQUAL(INT_MIN, sensor.getResistance());
  TEST_ASSERT_EQUAL(INT_MIN, sensor.getPower());
  TEST_ASSERT_EQUAL(INT_MIN, sensor.getHeartRate());
}

void test_fitnessMachineIndoorBikeData::test_parses_heartrate(void) {
  FitnessMachineIndoorBikeData sensor = FitnessMachineIndoorBikeData();
  sensor.decode(data, 9);
  TEST_ASSERT_FALSE(sensor.hasHeartRate());
  TEST_ASSERT_EQUAL(INT_MIN, sensor.getHeartRate());
}

void test_fitnessMachineIndoorBikeData::test_parses_cadence(void) {
  FitnessMachineIndoorBikeData sensor = FitnessMachineIndoorBikeData();
  sensor.decode(data, 9);
  TEST_ASSERT_TRUE(sensor.hasCadence());
  TEST_ASSERT_EQUAL(88, sensor.getCadence());
}

void test_fitnessMachineIndoorBikeData::test_parses_power(void) {
  FitnessMachineIndoorBikeData sensor = FitnessMachineIndoorBikeData();
  sensor.decode(data, 9);
  TEST_ASSERT_TRUE(sensor.hasPower());
  TEST_ASSERT_EQUAL(64, sensor.getPower());
}

void test_fitnessMachineIndoorBikeData::test_walks_all_flagged_fields(void) {
  // Every optional field is present. Resistance remains the firmware's
  // established signed 16-bit field; the other widths follow Table 4.10.
  uint8_t complete[] = {
      0xfe, 0x1f, 0x10, 0x0e, 0xb4, 0x0c, 0xb5, 0x00, 0x78, 0x00, 0x03, 0x02, 0x01, 0x2a, 0x00,
      0xfa, 0xff, 0xfa, 0x00, 0xf4, 0x01, 0x58, 0x02, 0x0a, 0x96, 0x55, 0x10, 0x0e, 0x58, 0x02,
  };
  FitnessMachineIndoorBikeData sensor;
  sensor.decode(complete, sizeof(complete));

  TEST_ASSERT_FLOAT_WITHIN(0.01f, 36.0f, sensor.getSpeed());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.5f, sensor.getCadence());
  TEST_ASSERT_EQUAL_INT(42, sensor.getResistance());
  TEST_ASSERT_EQUAL_INT(-6, sensor.getPower());
  TEST_ASSERT_TRUE(sensor.hasHeartRate());
  TEST_ASSERT_EQUAL_INT(150, sensor.getHeartRate());
}

void test_fitnessMachineIndoorBikeData::test_rejects_short_or_null_notifications(void) {
  uint8_t complete[] = {
      0xfe, 0x1f, 0x10, 0x0e, 0xb4, 0x0c, 0xb5, 0x00, 0x78, 0x00, 0x03, 0x02, 0x01, 0x2a, 0x00,
      0xfa, 0xff, 0xfa, 0x00, 0xf4, 0x01, 0x58, 0x02, 0x0a, 0x96, 0x55, 0x10, 0x0e, 0x58, 0x02,
  };
  FitnessMachineIndoorBikeData sensor;

  for (size_t length = 0; length < sizeof(complete); length++) {
    // The factory reuses parser instances, so a malformed update must not
    // retain the preceding complete notification as a fresh sample.
    sensor.decode(complete, sizeof(complete));
    TEST_ASSERT_TRUE(sensor.hasPower());
    sensor.decode(complete, length);
    assertNoMeasurements(sensor);
  }

  sensor.decode(complete, sizeof(complete));
  TEST_ASSERT_TRUE(sensor.hasPower());
  sensor.decode(nullptr, sizeof(complete));
  assertNoMeasurements(sensor);
}
