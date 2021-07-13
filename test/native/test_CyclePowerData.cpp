/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "sdkconfig.h"
#include <unity.h>
#include "sensors/CyclePowerData.h"
#include "test.h"

// Data from session w/ Assioma Uno pedals
static uint8_t t0[]  = {0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x0b};  // <- 0x1818 | 0x2a63 | CPS:[ CD(0.00) PW(0) ]
static uint8_t t1[]  = {0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x0b};  // <- 0x1818 | 0x2a63 | CPS:[ CD(0.00) PW(0) ]
static uint8_t t2[]  = {0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x0b};  // <- 0x1818 | 0x2a63 | CPS:[ CD(0.00) PW(0) ]
static uint8_t t3[]  = {0x20, 0x00, 0x2d, 0x00, 0x02, 0x00, 0xb8, 0x12};  // <- 0x1818 | 0x2a63 | CPS:[ CD(31.09) PW(45) ]
static uint8_t t4[]  = {0x20, 0x00, 0x2d, 0x00, 0x02, 0x00, 0xb8, 0x12};  // <- 0x1818 | 0x2a63 | CPS:[ CD(31.09) PW(45) ]
static uint8_t t5[]  = {0x20, 0x00, 0x2e, 0x00, 0x03, 0x00, 0xae, 0x17};  // <- 0x1818 | 0x2a63 | CPS:[ CD(48.38) PW(46) ]
static uint8_t t6[]  = {0x20, 0x00, 0x2e, 0x00, 0x03, 0x00, 0xae, 0x17};  // <- 0x1818 | 0x2a63 | CPS:[ CD(48.38) PW(46) ]
static uint8_t t7[]  = {0x20, 0x00, 0x39, 0x00, 0x05, 0x00, 0x31, 0x20};  // <- 0x1818 | 0x2a63 | CPS:[ CD(56.39) PW(57) ]
static uint8_t t8[]  = {0x20, 0x00, 0x39, 0x00, 0x05, 0x00, 0x31, 0x20};  // <- 0x1818 | 0x2a63 | CPS:[ CD(56.39) PW(57) ]
static uint8_t t9[]  = {0x20, 0x00, 0x39, 0x00, 0x05, 0x00, 0x31, 0x20};  // <- 0x1818 | 0x2a63 | CPS:[ CD(56.39) PW(57) ]
static uint8_t t10[] = {0x20, 0x00, 0x39, 0x00, 0x05, 0x00, 0x31, 0x20};  // <- 0x1818 | 0x2a63 | CPS:[ CD(56.39) PW(57) ]
static uint8_t t11[] = {0x20, 0x00, 0x39, 0x00, 0x05, 0x00, 0x31, 0x20};  // <- 0x1818 | 0x2a63 | CPS:[ CD(0) PW(57) ]

// Test wrap around uint16 overflow
static uint8_t t12[] = {0x20, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff};  // <- 0x1818 | 0x2a63 | CPS:[ CD(??) PW(??) ]
static uint8_t t13[] = {0x20, 0x00, 0x2d, 0x00, 0x00, 0x00, 0xb7, 0x07};  // <- 0x1818 | 0x2a63 | CPS:[ CD(31.09) PW(45) ]

void test_cyclePowerData::test_parses_cadence(void) {
  CyclePowerData sensor = CyclePowerData();
  // Pre parse state
  TEST_ASSERT_FALSE(sensor.hasCadence());
  TEST_ASSERT_EQUAL_FLOAT(NAN, sensor.getCadence());

  // Cadence relies on past values, shouldn't have a non-zero value until 2 readings have been decode()-ed
  sensor.decode(t0, sizeof(t0));
  TEST_ASSERT_EQUAL_FLOAT(0.0, sensor.getCadence());

  // Now cadence should be set, but zero
  sensor.decode(t1, sizeof(t0));
  TEST_ASSERT_EQUAL_FLOAT(0.0, sensor.getCadence());

  // Still no change
  sensor.decode(t2, sizeof(t0));
  TEST_ASSERT_TRUE(sensor.hasCadence());
  TEST_ASSERT_EQUAL_FLOAT(0.0, sensor.getCadence());

  // Now we have cadence
  sensor.decode(t3, sizeof(t0));
  TEST_ASSERT_EQUAL_FLOAT(31.09312, sensor.getCadence());

  // No change to cadence reading, cadence should remain unchanged because missed
  // reading count hasn't hit 3
  sensor.decode(t4, sizeof(t0));
  TEST_ASSERT_EQUAL_FLOAT(31.09312, sensor.getCadence());
  sensor.decode(t5, sizeof(t0));
  TEST_ASSERT_EQUAL_FLOAT(48.37795, sensor.getCadence());
  sensor.decode(t6, sizeof(t0));
  TEST_ASSERT_EQUAL_FLOAT(48.37795, sensor.getCadence());
  sensor.decode(t7, sizeof(t0));
  TEST_ASSERT_EQUAL_FLOAT(56.39284, sensor.getCadence());
  sensor.decode(t8, sizeof(t0));
  TEST_ASSERT_EQUAL_FLOAT(56.39284, sensor.getCadence());
  sensor.decode(t9, sizeof(t0));
  TEST_ASSERT_EQUAL_FLOAT(56.39284, sensor.getCadence());
  sensor.decode(t10, sizeof(t0));
  TEST_ASSERT_EQUAL_FLOAT(56.39284, sensor.getCadence());

  // Unchanged 4X, now realize that cadence is 0
  sensor.decode(t11, sizeof(t0));
  TEST_ASSERT_EQUAL_FLOAT(0.0, sensor.getCadence());

  // Test overflow
  sensor.decode(t12, sizeof(t0));
  sensor.decode(t13, sizeof(t0));
  TEST_ASSERT_EQUAL_FLOAT(31.09312, sensor.getCadence());
}

void test_cyclePowerData::test_parses_heartrate(void) {
  // No heart rate data, confirm
  CyclePowerData sensor = CyclePowerData();
  TEST_ASSERT_FALSE(sensor.hasHeartRate());
  TEST_ASSERT_EQUAL_INT(INT_MIN, sensor.getHeartRate());
}

void test_cyclePowerData::test_parses_speed(void) {
  // No speed data, confirm
  CyclePowerData sensor = CyclePowerData();
  TEST_ASSERT_FALSE(sensor.hasSpeed());
  TEST_ASSERT_EQUAL_FLOAT(NAN, sensor.getSpeed());
}

void test_cyclePowerData::test_parses_power(void) {
  CyclePowerData sensor = CyclePowerData();
  // Pre parse state
  TEST_ASSERT_FALSE(sensor.hasPower());
  TEST_ASSERT_EQUAL_INT(INT_MIN, sensor.getPower());

  // Power is instantaneous, not much calculation to do so skipping some samples
  sensor.decode(t0, sizeof(t0));
  TEST_ASSERT_TRUE(sensor.hasPower());
  TEST_ASSERT_EQUAL_INT(0, sensor.getPower());

  sensor.decode(t3, sizeof(t0));
  TEST_ASSERT_TRUE(sensor.hasPower());
  TEST_ASSERT_EQUAL_INT(45, sensor.getPower());

  sensor.decode(t7, sizeof(t0));
  TEST_ASSERT_EQUAL_INT(57, sensor.getPower());
}
