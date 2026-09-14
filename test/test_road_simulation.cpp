/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>
#include <limits>
#include "test.h"
#include "SmartSpin_parameters.h"

void TestRoadSimulation::test_gears_and_weight() {
  RoadSimulation::Gears gears;
  TEST_ASSERT_EQUAL(24, gears.count);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 1.0, gears.ratio(-500));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 4.545, gears.ratio(500));
  const RoadSimulation::Gears original = gears;
  uint16_t values[26];
  for (int i = 0; i < 26; ++i) values[i] = 500 + 200 * i;
  for (int count : {11, 12, 13, 22, 24, 26}) {
    TEST_ASSERT_TRUE(gears.assign(values, count));
    TEST_ASSERT_EQUAL(count, gears.count);
    TEST_ASSERT_EQUAL(count, gears.clampGear(1000));
  }
  TEST_ASSERT_FALSE(gears.assign(values, 27));
  TEST_ASSERT_FALSE(gears.assign(values, 1));
  TEST_ASSERT_FALSE(gears.assign(nullptr, 10));
  gears = original;
  const uint16_t descending[] = {2000, 1000};
  TEST_ASSERT_FALSE(gears.assign(descending, 2));
  TEST_ASSERT_TRUE(gears == original);
  const uint16_t invalid[] = {500, 6001};
  TEST_ASSERT_FALSE(gears.assign(invalid, 2));
  TEST_ASSERT_TRUE(gears == original);
  uint8_t packet[53] = {26};
  for (int i = 0; i < 26; ++i) put_le16(packet + 1 + i * 2, values[i]);
  TEST_ASSERT_TRUE(gears.decode(packet, sizeof(packet)));
  TEST_ASSERT_EQUAL_UINT16_ARRAY(values, gears.ratios, 26);
  const RoadSimulation::Gears complete = gears;
  for (size_t length = 0; length < sizeof(packet); ++length) {
    TEST_ASSERT_FALSE(gears.decode(packet, length));
    TEST_ASSERT_TRUE(gears == complete);
  }
  packet[0] = 27;
  TEST_ASSERT_FALSE(gears.decode(packet, sizeof(packet)));
  userParameters config;
  TEST_ASSERT_FLOAT_WITHIN(0.001, 75, config.getRiderWeightKg());
  TEST_ASSERT_TRUE(config.setRiderWeightKg(82.35f));
  for (float invalidWeight : {0.0f, 19.99f, 250.01f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
    TEST_ASSERT_FALSE(config.setRiderWeightKg(invalidWeight));
    TEST_ASSERT_FLOAT_WITHIN(0.001, 82.35, config.getRiderWeightKg());
  }
}

void TestRoadSimulation::test_simulation_parameters() {
  RoadSimulation::Parameters parameters;
  TEST_ASSERT_FLOAT_WITHIN(0.000001, 0.004, parameters.rollingResistance);
  TEST_ASSERT_FLOAT_WITHIN(0.000001, 0.51, parameters.windResistance);
  uint8_t packet[] = {0x11, 0, 0, 0, 0, 40, 51};
  put_le16s(packet + 1, -1234);
  put_le16s(packet + 3, -567);
  TEST_ASSERT_TRUE(parameters.decode(packet, sizeof(packet)));
  TEST_ASSERT_FLOAT_WITHIN(0.000001, -1.234, parameters.windSpeedMps);
  TEST_ASSERT_FLOAT_WITHIN(0.000001, -5.67, parameters.gradePercent);
  for (size_t length = 0; length < sizeof(packet); ++length) {
    TEST_ASSERT_FALSE(parameters.decode(packet, length));
    TEST_ASSERT_FLOAT_WITHIN(0.000001, -5.67, parameters.gradePercent);
  }
  TEST_ASSERT_FALSE(parameters.decode(packet, 8));
  packet[0] = 0x10;
  TEST_ASSERT_FALSE(parameters.decode(packet, 7));
  packet[0] = 0x11;
  packet[5] = 255;
  packet[6] = 255;
  TEST_ASSERT_TRUE(parameters.decode(packet, 7));
  TEST_ASSERT_FLOAT_WITHIN(0.000001, 0.0255, parameters.rollingResistance);
  TEST_ASSERT_FLOAT_WITHIN(0.000001, 2.55, parameters.windResistance);
  packet[5] = 0;
  packet[6] = 0;
  TEST_ASSERT_TRUE(parameters.decode(packet, 7));
  TEST_ASSERT_EQUAL_FLOAT(0, parameters.rollingResistance);
  TEST_ASSERT_EQUAL_FLOAT(0, parameters.windResistance);
}

void TestRoadSimulation::test_road_load() {
  using namespace RoadSimulation;
  Parameters parameters;
  parameters.windResistance = 0;
  const float speed = roadSpeed(90, 2);
  TEST_ASSERT_FLOAT_WITHIN(0.00001, 6.315, speed);
  const float rollingPower = 85 * 9.80665f * 0.004f * 6.315f / 0.97f;
  TEST_ASSERT_FLOAT_WITHIN(0.001, rollingPower, brakeWatts(90, 2, speed, 75, parameters));
  parameters.gradePercent = 5;
  const float climb = brakeWatts(90, 2, speed, 75, parameters);
  TEST_ASSERT_TRUE(climb > rollingPower);
  TEST_ASSERT_TRUE(brakeWatts(90, 2, speed, 100, parameters) > climb);
  // At equal road speed, a 25% harder gear needs 25% more crank torque.
  TEST_ASSERT_FLOAT_WITHIN(0.001, climb * 1.25f, brakeWatts(90, 2.5, speed, 75, parameters));
  // Once cadence drops 20%, the same speed requires the same power.
  TEST_ASSERT_FLOAT_WITHIN(0.001, climb, brakeWatts(72, 2.5, speed, 75, parameters));
  parameters.gradePercent = 0;
  parameters.rollingResistance = 0;
  parameters.windResistance = 0.51f;
  const float aerodynamic = brakeWatts(90, 2, speed, 75, parameters);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.5f * 0.51f * speed * speed * speed / 0.97f, aerodynamic);
  TEST_ASSERT_FLOAT_WITHIN(0.001, aerodynamic * 8, brakeWatts(90, 4, speed * 2, 75, parameters));
  parameters.windSpeedMps = 3;
  TEST_ASSERT_TRUE(brakeWatts(90, 2, speed, 75, parameters) > aerodynamic);
  parameters.windSpeedMps = -3;
  TEST_ASSERT_TRUE(brakeWatts(90, 2, speed, 75, parameters) < aerodynamic);
  parameters.windSpeedMps = -20;
  TEST_ASSERT_EQUAL_FLOAT(0, brakeWatts(90, 2, speed, 75, parameters));
  parameters.windSpeedMps = 0;
  parameters.gradePercent = -20;
  TEST_ASSERT_EQUAL_FLOAT(0, brakeWatts(90, 2, speed, 75, parameters));
  TEST_ASSERT_EQUAL_FLOAT(0, brakeWatts(0, 2, speed, 75, parameters));
  TEST_ASSERT_EQUAL_FLOAT(0, brakeWatts(250, 2, speed, 75, parameters));
  parameters.gradePercent = 327.67f;
  TEST_ASSERT_EQUAL_FLOAT(4000, brakeWatts(249, 6, 50, 250, parameters));
  parameters.gradePercent = std::numeric_limits<float>::quiet_NaN();
  TEST_ASSERT_EQUAL_FLOAT(0, brakeWatts(90, 2, speed, 75, parameters));
}

void TestRoadSimulation::test_shift_transitions() {
  RoadSimulation::SpeedFilter speed;
  const float initial = speed.update(90, 2, 0);
  TEST_ASSERT_EQUAL_FLOAT(initial, speed.update(90, 2.5, 100));
  TEST_ASSERT_EQUAL_FLOAT(initial, speed.update(90, 2.5, 900));
  TEST_ASSERT_EQUAL_FLOAT(initial, speed.update(72, 2.5, 1100));
  TEST_ASSERT_TRUE(speed.update(90, 2.5, 1200) > initial);
  speed.reset();
  TEST_ASSERT_FLOAT_WITHIN(0.001, RoadSimulation::roadSpeed(90, 4), speed.update(90, 4, 1300));

  RoadSimulation::ShiftFeel shift;
  TEST_ASSERT_EQUAL_FLOAT(1, shift.update(2, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.00001, 1.05, shift.update(2.5, 100));
  TEST_ASSERT_FLOAT_WITHIN(0.00001, 1.025, shift.update(2.5, 600));
  TEST_ASSERT_EQUAL_FLOAT(1, shift.update(2.5, 1100));
  TEST_ASSERT_FLOAT_WITHIN(0.00001, 0.95, shift.update(2, 1200));
  TEST_ASSERT_FLOAT_WITHIN(0.00001, 0.95, shift.update(1.5, 1300));
  TEST_ASSERT_FLOAT_WITHIN(0.00001, 1.05, shift.update(4, 1400));
  TEST_ASSERT_EQUAL_FLOAT(1, shift.update(4, 2400));
  shift.reset();
  TEST_ASSERT_EQUAL_FLOAT(1, shift.update(2, UINT32_MAX - 500));
  TEST_ASSERT_FLOAT_WITHIN(0.00001, 1.05, shift.update(3, UINT32_MAX - 100));
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 1.025, shift.update(3, 399));
}
