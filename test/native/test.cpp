/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "sdkconfig.h"
#include <unity.h>
#include "test.h"
//#include "test_FitnessMachineIndoorBikeData.cpp"
//#include "test_CyclePowerData.cpp"

void process() {
  UNITY_BEGIN();
  // FitnessMachineIndoorBike Tests
  {
    test_fitnessMachineIndoorBikeData test;
    RUN_TEST(test.test_parses_heartrate);
    RUN_TEST(test.test_parses_cadence);
    RUN_TEST(test.test_parses_power);
  }
  // Cycle Power Tests
  test_cyclePowerData test;
  {
    RUN_TEST(test.test_parses_power);
    RUN_TEST(test.test_parses_cadence);
    RUN_TEST(test.test_parses_heartrate);
    RUN_TEST(test.test_parses_speed);
  }
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

#endif  // ARDUINO