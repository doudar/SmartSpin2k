/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>
#include "BLE_Common.h"

static uint8_t data[9] = {0x44, 0x02, 0xf2, 0x08, 0xb0, 0x00, 0x40, 0x00, 0x00};

void test_parses_heartrate(void) {
  FitnessMachineIndoorBikeData sensor = FitnessMachineIndoorBikeData();
  sensor.decode(data, 9);
  TEST_ASSERT_TRUE(sensor.hasHeartRate());
  TEST_ASSERT_EQUAL(0, sensor.getHeartRate());
}

void test_parses_cadence(void) {
  FitnessMachineIndoorBikeData sensor = FitnessMachineIndoorBikeData();
  sensor.decode(data, 9);
  TEST_ASSERT_TRUE(sensor.hasCadence());
  TEST_ASSERT_EQUAL(88, sensor.getCadence());
}

void test_parses_power(void) {
  FitnessMachineIndoorBikeData sensor = FitnessMachineIndoorBikeData();
  sensor.decode(data, 9);
  TEST_ASSERT_TRUE(sensor.hasPower());
  TEST_ASSERT_EQUAL(64, sensor.getPower());
}

void process() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_heartrate);
  RUN_TEST(test_parses_cadence);
  RUN_TEST(test_parses_power);
  UNITY_END();
}

#ifdef ARDUINO

#include <Arduino.h>
void setup() {
  delay(2000);
  process();
}

void loop() {}

#else

int main(int argc, char **argv) {
  process();
  return 0;
}

#endif
