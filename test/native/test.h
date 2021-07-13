/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

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
