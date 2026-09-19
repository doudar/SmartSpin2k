/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

class TestThermalSafety {
 public:
  static void test_tmc_cooldown_and_recovery();
  static void test_tmc_missing_samples_and_timer_wrap();
  static void test_tmc_uart_probe_while_disabled();
  static void test_tmc_uart_probe_rejects_invalid_responses();
  static void test_s3_thresholds_and_hysteresis();
  static void test_s3_failed_sensor_preserves_protection();
  static void test_combined_limits_and_setting_changes();
};

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
  static void test_active_ride_new_gain_replay(void);
  static void test_table_position_confidence(void);
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
  static void test_compact_status_log_replay(void);
  static void test_status_ride_table_generation(void);
  static void test_active_table_status_prediction_accuracy(void);
  static void test_active_table_transient_power_estimation(void);
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

class TestEndian {
 public:
  static void test_little_endian_signed_decode_and_round_trip(void);
};

class TestFtmsHoming {
 public:
  static void test_repeatable_startup(void);
  static void test_both_ends_and_legacy(void);
  static void test_missing_stuck_and_skipped_reports(void);
  static void test_abort_and_feedback(void);
  static void test_measurement_value_timer(void);
  static void test_report_published_during_read(void);
  static void test_wrong_direction_stops_motor(void);
  static void test_delayed_crossing_after_stop(void);
  static void test_stationary_reading_confirmation(void);
  static void test_adjacent_boundary_noise(void);
  static void test_shifted_crossing_retries(void);
  static void test_responsive_retries_until_deadline(void);
  static void test_accelerated_probe_repeatability(void);
  static void test_one_second_startup_check(void);
  static void test_wide_resistance_four(void);
  static void test_calibrated_startup_and_map(void);
  static void test_stationary_drift_guard(void);
  static void test_sparse_noisy_observations(void);
  static void test_manual_knob_resync(void);
};

class TestBleWireRoundTrip {
 public:
  static void test_dircon_uuid_round_trip(void);
  static void test_all_custom_characteristic_formats(void);
  static void test_ftms_round_trip(void);
  static void test_csc_round_trip(void);
  static void test_heart_rate_round_trip(void);
  static void test_zwift_round_trip(void);
};

class TestVirtualGearing {
 public:
  static void test_unlimited_default_and_wire();
  static void test_ratio_api();
  static void test_offset_normalization();
  static void test_duplicate_and_identical_ratios();
  static void test_profile_bounds_and_scaling();
  static void test_offset_overflow();
  static void test_packet_validation();
};
