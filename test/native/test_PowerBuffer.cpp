/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>
#include <task.h>
#include "test.h"
#include "ERG_Mode.h"

void TestPowerBuffer::set__should_set_values__expect_values_added_to_correct_index(void) {
  const PowerBuffer powerBuffer;
  // // Pre parse state
  // TEST_ASSERT_FALSE(sensor.hasPower());
  // TEST_ASSERT_EQUAL_INT(INT_MIN, sensor.getPower());

  // // Power is instantaneous, not much calculation to do so skipping some samples
  // sensor.decode(t0, sizeof(t0));
  // TEST_ASSERT_TRUE(sensor.hasPower());
  // TEST_ASSERT_EQUAL_INT(0, sensor.getPower());

  // sensor.decode(t3, sizeof(t0));
  // TEST_ASSERT_TRUE(sensor.hasPower());
  // TEST_ASSERT_EQUAL_INT(45, sensor.getPower());

  // sensor.decode(t7, sizeof(t0));
  // TEST_ASSERT_EQUAL_INT(57, sensor.getPower());
}
