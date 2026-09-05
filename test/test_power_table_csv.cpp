/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>

#include "test.h"
#include "test_data_helpers.h"

void TestPowerTableCsv::test_active_table_round_trip(void) {
  PTData source;
  RideReplaySummary summary;
  TEST_ASSERT_TRUE_MESSAGE(replayActiveRideLog(source, summary), "active ride log could not be opened for CSV round-trip test");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, summary.invalidEntries, "invalid active-log entry reached CSV round-trip test");

  TEST_ASSERT_TRUE_MESSAGE(savePTDataToCSV(source, ACTIVE_POWER_TABLE_OUTPUT_PATH), "failed to save active ride power table");
  PTData reloaded;
  TEST_ASSERT_TRUE_MESSAGE(loadCSVToPTData(ACTIVE_POWER_TABLE_OUTPUT_PATH, reloaded), "failed to reload active ride power table");

  std::string failure;
  TEST_ASSERT_TRUE_MESSAGE(powerTablePositionsMatch(source, reloaded, failure), failure.c_str());
}
