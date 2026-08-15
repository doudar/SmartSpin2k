/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

class test_fitnessMachineIndoorBikeData {
 public:
  static void test_parses_power(void);
  static void test_parses_cadence(void);
  static void test_parses_heartrate(void);
};

class test_cyclePowerData {
 public:
  static void test_parses_power(void);
  static void test_parses_cadence(void);
  static void test_parses_heartrate(void);
  static void test_parses_speed(void);
};

class TestPowerBuffer {
 public:
  static void set__should_set_values__expect_values_added_to_correct_index(void);
};

class TestPTLookupResistance {
 public:
  static void test_pt_lookup_resistance(void);
};

class TestPTLookupWatts {
  public:
   static void test_pt_lookup_watts(void);
 };

class TestWritePowerTable {
  public:
    static void test_save_and_load(void);
};

class TestTableFill {
  public:
    static void test_fill_incomplete_table(void);
};

class TestAdevName2UniqueName {
public:
    static void test_traditional_device_keeps_address_suffix(void);
    static void test_android_device_no_address_suffix(void);
    static void test_random_address_pattern_detection(void);
    static void test_null_device_handling(void);
    static void test_device_without_name(void);
    static void test_backward_compatibility(void);
    static void test_case_insensitive_device_matching(void);
};

class TestBleFirmwareUpdateProtocol {
 public:
  static void test_parses_start_packet(void);
  static void test_rejects_invalid_start_packets(void);
  static void test_encodes_status_packet(void);
  static void test_transfer_timeout(void);
};
