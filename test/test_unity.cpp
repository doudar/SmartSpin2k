/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>
#include "test.h"

// Basic test functions
void test_dummy_function(void) { TEST_ASSERT_EQUAL(1, 1); }

void test_basic_math(void) {
  TEST_ASSERT_EQUAL(4, 2 + 2);
  TEST_ASSERT_EQUAL(6, 2 * 3);
}

void setUp(void) {
  // set stuff up here
}

void tearDown(void) {
  // clean stuff up here
}

void setup() {
// delay for stability (when running on actual hardware)
#ifdef ARDUINO
  delay(2000);
#endif

  // Start Unity
  UNITY_BEGIN();

  // Run simple tests
  RUN_TEST(test_dummy_function);
  RUN_TEST(test_basic_math);

  // FitnessMachineIndoorBike Tests
  {
    test_fitnessMachineIndoorBikeData test;
    RUN_TEST(test.test_parses_heartrate);
    RUN_TEST(test.test_parses_cadence);
    RUN_TEST(test.test_parses_power);
  }

  // Cycle Power Tests
  {
    test_cyclePowerData test;
    RUN_TEST(test.test_parses_power);
    RUN_TEST(test.test_parses_cadence);
    RUN_TEST(test.test_parses_heartrate);
    RUN_TEST(test.test_parses_speed);
  }

  UNITY_END();
}

void loop() {
  // Empty - required for Arduino
}

// For native testing
#ifndef ARDUINO
int main(int argc, char **argv) {
  setup();
  return 0;
}
#endif