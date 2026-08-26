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

class TestErgLogReplay {
 public:
  static void test_active_ride_log_and_gain_limits(void);
};

class TestPTLookupResistance {
 public:
  static void test_cadence_collection_boundaries(void);
  static void test_active_ride_forward_lookup(void);
  static void test_erg_slope_quality(void);
};

class TestPTLookupWatts {
  public:
   static void test_active_ride_reverse_lookup(void);
   static void test_reverse_lookup_pathological_tables(void);
 };

class TestPowerTableCsv {
  public:
    static void test_active_table_round_trip(void);
};

class TestActiveRideTable {
  public:
    static void test_active_ride_table_generation(void);
    static void test_status_ride_table_generation(void);
    static void test_active_table_status_prediction_accuracy(void);
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
