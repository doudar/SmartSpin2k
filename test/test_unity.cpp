/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>
#include "test.h"

#ifndef ARDUINO
#include <errno.h>      // For errno and EEXIST
#include <stdio.h>      // For perror
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif
#endif

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

  // Power Table Lookup Tests
  {
    TestPTLookupResistance test;
    RUN_TEST(test.test_pt_lookup_resistance);
    TestPTLookupWatts test2;
    RUN_TEST(test2.test_pt_lookup_watts);
    TestWritePowerTable test4;
    RUN_TEST(test4.test_save_and_load);
    TestTableFill test5;
    RUN_TEST(test5.test_fill_incomplete_table);
  }

  // BLE Device Unique Name Tests
  {
    TestAdevName2UniqueName test;
    RUN_TEST(test.test_traditional_device_keeps_address_suffix);
    RUN_TEST(test.test_android_device_no_address_suffix);
    RUN_TEST(test.test_random_address_pattern_detection);
    RUN_TEST(test.test_null_device_handling);
    RUN_TEST(test.test_device_without_name);
    RUN_TEST(test.test_backward_compatibility);
    RUN_TEST(test.test_case_insensitive_device_matching);
  }

  // BLE Firmware Update Protocol Tests
  {
    TestBleFirmwareUpdateProtocol test;
    RUN_TEST(test.test_parses_start_packet);
    RUN_TEST(test.test_rejects_invalid_start_packets);
    RUN_TEST(test.test_encodes_status_packet);
    RUN_TEST(test.test_transfer_timeout);
  }

  UNITY_END();
}

void loop() {
  // Empty - required for Arduino
}

// For native testing
#ifndef ARDUINO
int main(int argc, char** argv) {
  // Create test/output directory if it doesn't exist for native builds
  const char* dir_path = "test/output";
// Attempt to create the directory.
// On POSIX systems, mkdir requires <sys/stat.h> and <sys/types.h>.
// Mode 0777 gives read, write, execute permissions for owner, group, and others.
#ifdef _WIN32
  int result = _mkdir(dir_path);
#else
  int result = mkdir(dir_path, 0777);
#endif

  if (result == -1) {
    // If mkdir failed, check why
    if (errno != EEXIST) {
      // EEXIST means the directory already exists, which is not an error for our purpose.
      // For any other error, print it.
      perror("Error creating directory test/output");
      // Depending on requirements, you might want to exit here or let tests proceed/fail.
    }
    // If errno is EEXIST, directory already exists, which is fine.
  }

  setup();
  return 0;
}
#endif
