/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>

#include <cstdio>

#include "PowerTable_Helpers.h"
#include "../src/PowerTable_Helpers.cpp"
#include "test.h"
#include "test_data_helpers.h"

void TestPTLookupResistance::test_cadence_collection_boundaries(void) {
  PTHelpers helpers;

  TEST_ASSERT_FALSE_MESSAGE(helpers.cadenceIsWithinTable(57), "57 RPM must remain below the 60 RPM collection bin");
  TEST_ASSERT_TRUE_MESSAGE(helpers.cadenceIsWithinTable(58), "58 RPM must be collected into the 60 RPM bin");
  TEST_ASSERT_EQUAL_INT8_MESSAGE(0, helpers.calculateIndex(0, 58).cadIndex, "58 RPM must map to the 60 RPM row");
  TEST_ASSERT_TRUE_MESSAGE(helpers.cadenceIsWithinTable(62), "62 RPM must be collected into the 60 RPM bin");
  TEST_ASSERT_EQUAL_INT8_MESSAGE(0, helpers.calculateIndex(0, 62).cadIndex, "62 RPM must map to the 60 RPM row");
  TEST_ASSERT_EQUAL_INT8_MESSAGE(1, helpers.calculateIndex(0, 63).cadIndex, "63 RPM must map to the 65 RPM row");

  TEST_ASSERT_TRUE_MESSAGE(helpers.cadenceIsWithinTable(103), "103 RPM must be collected into the 105 RPM bin");
  TEST_ASSERT_EQUAL_INT8_MESSAGE(9, helpers.calculateIndex(0, 103).cadIndex, "103 RPM must map to the 105 RPM row");
  TEST_ASSERT_TRUE_MESSAGE(helpers.cadenceIsWithinTable(107), "107 RPM must be collected into the 105 RPM bin");
  TEST_ASSERT_EQUAL_INT8_MESSAGE(9, helpers.calculateIndex(0, 107).cadIndex, "107 RPM must map to the 105 RPM row");
  TEST_ASSERT_FALSE_MESSAGE(helpers.cadenceIsWithinTable(108), "108 RPM must remain above the 105 RPM collection bin");
}

void TestPTLookupResistance::test_active_ride_forward_lookup(void) {
  PTData ptData;
  RideReplaySummary summary;
  TEST_ASSERT_TRUE_MESSAGE(replayActiveRideLog(ptData, summary), "active ride log could not be opened for forward lookup test");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, summary.invalidEntries, "active ride log contains invalid table entries");
  TEST_ASSERT_EQUAL_INT_MESSAGE(552, summary.entries, "forward lookup did not replay every active-log entry");

  PTHelpers helpers;
  TEST_ASSERT_EQUAL_INT32_MESSAGE(RETURN_ERROR, helpers.lookup(200, 0, ptData), "zero cadence must be rejected by forward lookup");
  TEST_ASSERT_EQUAL_INT32_MESSAGE(RETURN_ERROR, helpers.lookup(-1, 75, ptData), "negative watts must be rejected by forward lookup");
  TEST_ASSERT_NOT_EQUAL_MESSAGE(RETURN_ERROR, helpers.lookup(INT32_MAX, 1, ptData), "extreme forward lookup overflowed into RETURN_ERROR");

  char failure[220];
  for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
    int16_t previousPosition = INT16_MIN;
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      const TableEntry& entry = ptData.tableRow[row].tableEntry[col];
      if (entry.targetPosition == INT16_MIN) continue;
      std::snprintf(failure, sizeof(failure),
                    "stored table decreased with watts: cadence=%d watts=%d previous_position=%d position=%d",
                    MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT, col * POWERTABLE_WATT_INCREMENT,
                    previousPosition, entry.targetPosition);
      TEST_ASSERT_GREATER_OR_EQUAL_INT16_MESSAGE(previousPosition, entry.targetPosition, failure);
      previousPosition = entry.targetPosition;
    }
  }
  for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
    int16_t previousPosition = INT16_MAX;
    for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
      const TableEntry& entry = ptData.tableRow[row].tableEntry[col];
      if (entry.targetPosition == INT16_MIN) continue;
      std::snprintf(failure, sizeof(failure),
                    "stored table increased with cadence: watts=%d cadence=%d previous_position=%d position=%d",
                    col * POWERTABLE_WATT_INCREMENT, MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT,
                    previousPosition, entry.targetPosition);
      TEST_ASSERT_LESS_OR_EQUAL_INT16_MESSAGE(previousPosition, entry.targetPosition, failure);
      previousPosition = entry.targetPosition;
    }
  }

  for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
    int reliableSamples = 0;
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      const TableEntry& sample = ptData.tableRow[row].tableEntry[col];
      if (sample.targetPosition != INT16_MIN && sample.readings >= 2) ++reliableSamples;
    }
    if (reliableSamples < 2) continue;

    const int cadence = MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT;
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      const TableEntry& sample = ptData.tableRow[row].tableEntry[col];
      if (sample.targetPosition == INT16_MIN || sample.readings < 2) continue;
      const int watts = col * POWERTABLE_WATT_INCREMENT;
      const int32_t actual = helpers.lookup(watts, cadence, ptData);
      const int32_t expected = static_cast<int32_t>(sample.targetPosition * TABLE_DIVISOR);
      std::snprintf(failure, sizeof(failure), "measured forward sample mismatch: cadence=%d watts=%d expected_position=%ld actual_position=%ld",
                    cadence, watts, static_cast<long>(expected), static_cast<long>(actual));
      TEST_ASSERT_EQUAL_INT32_MESSAGE(expected, actual, failure);
    }
  }

  for (int cadence = 40; cadence <= 130; ++cadence) {
    int32_t previousPosition = INT32_MIN;
    for (int watts = 0; watts <= 1200; watts += 15) {
      const int32_t position = helpers.lookup(watts, cadence, ptData);
      std::snprintf(failure, sizeof(failure), "invalid forward lookup: cadence=%d watts=%d returned=%ld", cadence, watts, static_cast<long>(position));
      TEST_ASSERT_NOT_EQUAL_MESSAGE(RETURN_ERROR, position, failure);
      if (previousPosition != INT32_MIN) {
        std::snprintf(failure, sizeof(failure),
                      "forward lookup decreased with watts: cadence=%d previous_watts=%d previous_position=%ld watts=%d position=%ld",
                      cadence, watts - 15, static_cast<long>(previousPosition), watts, static_cast<long>(position));
        TEST_ASSERT_GREATER_OR_EQUAL_INT32_MESSAGE(previousPosition, position, failure);
      }
      previousPosition = position;
    }
  }

  // Monotonic enforcement must correct measured violations without filling
  // empty cells that were never observed in the active ride.
  PTData sparseTable;
  sparseTable.tableRow[2].tableEntry[1].targetPosition = 200;
  sparseTable.tableRow[2].tableEntry[1].readings = 2;
  sparseTable.tableRow[2].tableEntry[3].targetPosition = 100;
  sparseTable.tableRow[2].tableEntry[3].readings = 2;
  ptIndex newSample;
  newSample.cadIndex = 2;
  newSample.wattIndex = 5;
  helpers.enterData(sparseTable, newSample, 300);
  TEST_ASSERT_EQUAL_INT16_MESSAGE(150, sparseTable.tableRow[2].tableEntry[1].targetPosition, "power monotonic correction failed at lower sample");
  TEST_ASSERT_EQUAL_INT16_MESSAGE(INT16_MIN, sparseTable.tableRow[2].tableEntry[2].targetPosition, "power monotonic correction filled an unmeasured cell");
  TEST_ASSERT_EQUAL_INT16_MESSAGE(150, sparseTable.tableRow[2].tableEntry[3].targetPosition, "power monotonic correction failed at upper sample");
}

void TestPTLookupResistance::test_erg_slope_quality(void) {
  PTHelpers helpers;
  PTData slopeTable;
  for (int row = 0; row < 2; ++row) {
    for (int col = 0; col <= 4; ++col) {
      slopeTable.tableRow[row].tableEntry[col].targetPosition = 100 + col * 100 - row * 10;
      slopeTable.tableRow[row].tableEntry[col].readings = 2;
    }
  }

  double stepsPerWatt = 0.0;
  TEST_ASSERT_TRUE_MESSAGE(helpers.lookupSlope(60, 60, stepsPerWatt, slopeTable), "consistent interior cadence rows should provide an ERG slope");
  TEST_ASSERT_GREATER_THAN_FLOAT_MESSAGE(0.0f, static_cast<float>(stepsPerWatt), "accepted ERG slope must be positive and finite");
  TEST_ASSERT_FALSE_MESSAGE(helpers.lookupSlope(120, 60, stepsPerWatt, slopeTable), "right-edge extrapolation must not provide an ERG slope");

  slopeTable.tableRow[1].tableEntry[3].targetPosition = 700;
  slopeTable.tableRow[1].tableEntry[4].targetPosition = 1000;
  TEST_ASSERT_FALSE_MESSAGE(helpers.lookupSlope(60, 60, stepsPerWatt, slopeTable), "nonparallel cadence rows must not provide an ERG slope");

  PTData activeTable;
  RideReplaySummary summary;
  TEST_ASSERT_TRUE_MESSAGE(replayActiveRideLog(activeTable, summary), "active ride log could not be opened for slope-quality test");
  TEST_ASSERT_FALSE_MESSAGE(helpers.lookupSlope(385, 93, stepsPerWatt, activeTable), "385 W at 93 RPM must reject extrapolated active-log edge data");
  TEST_ASSERT_FALSE_MESSAGE(helpers.lookupSlope(385, 94, stepsPerWatt, activeTable), "385 W at 94 RPM must reject extrapolated active-log edge data");
}
