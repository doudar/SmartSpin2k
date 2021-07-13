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
