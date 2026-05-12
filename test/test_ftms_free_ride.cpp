/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "test.h"
#include "FTMS_Utils.h"
#include <unity.h>

void TestFTMSFreeRide::test_zero_watt_target_requests_free_ride(void) {
  TEST_ASSERT_TRUE(ftmsTargetPowerRequestsFreeRide(0));
}

void TestFTMSFreeRide::test_nonzero_watt_target_stays_erg(void) {
  TEST_ASSERT_FALSE(ftmsTargetPowerRequestsFreeRide(1));
  TEST_ASSERT_FALSE(ftmsTargetPowerRequestsFreeRide(250));
}
