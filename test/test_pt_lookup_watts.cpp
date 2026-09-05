/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>

#include <cstdio>
#include <fstream>

#include "PowerTable_Helpers.h"
#include "test.h"
#include "test_data_helpers.h"

void TestPTLookupWatts::test_active_ride_reverse_lookup(void) {
  PTData ptData;
  RideReplaySummary summary;
  TEST_ASSERT_TRUE_MESSAGE(replayActiveRideLog(ptData, summary), "active ride log could not be opened for reverse lookup test");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, summary.invalidEntries, "active ride log contains invalid reverse-lookup inputs");
  TEST_ASSERT_EQUAL_INT_MESSAGE(594, summary.entries, "reverse lookup did not replay every active-log entry");

  PTHelpers helpers;
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, helpers.lookupWatts(0, 10000, ptData), "zero cadence must estimate zero watts");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, helpers.lookupWatts(75, INT32_MIN, ptData), "minimum position must not underflow reverse lookup");
  TEST_ASSERT_EQUAL_INT_MESSAGE(4000, helpers.lookupWatts(75, INT32_MAX, ptData), "maximum position must saturate reverse lookup");
  TEST_ASSERT_EQUAL_INT_MESSAGE(4000, helpers.lookupWatts(INT32_MAX, INT32_MAX, ptData), "extreme cadence and position must remain bounded");

  char failure[260];
  int maximumRoundTripError = 0;
  int worstCadence = 0;
  int worstExpectedWatts = 0;
  int worstActualWatts = 0;
  int trustedRoundTrips = 0;
  for (int cadence = 40; cadence <= 130; ++cadence) {
    for (int expectedWatts = 30; expectedWatts <= 900; expectedWatts += 15) {
      double localStepsPerWatt;
      if (!helpers.lookupSlope(expectedWatts, cadence, localStepsPerWatt, ptData)) continue;
      ++trustedRoundTrips;
      const int32_t position = helpers.lookup(expectedWatts, cadence, ptData);
      std::snprintf(failure, sizeof(failure), "forward lookup failed before reverse lookup: cadence=%d watts=%d", cadence, expectedWatts);
      TEST_ASSERT_NOT_EQUAL_MESSAGE(RETURN_ERROR, position, failure);
      const int actualWatts = helpers.lookupWatts(cadence, position, ptData);
      std::snprintf(failure, sizeof(failure), "reverse lookup returned invalid watts: cadence=%d position=%ld watts=%d",
                    cadence, static_cast<long>(position), actualWatts);
      TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(0, actualWatts, failure);
      TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(4000, actualWatts, failure);
      const int error = std::abs(actualWatts - expectedWatts);
      if (error > maximumRoundTripError) {
        maximumRoundTripError = error;
        worstCadence = cadence;
        worstExpectedWatts = expectedWatts;
        worstActualWatts = actualWatts;
      }
    }
  }

  // Sweep the full operating surface. Watts may plateau, but must never fall
  // when either resistance or cadence increases.
  static const int RESISTANCE_STEP = 200;
  static const int MAX_RESISTANCE = 24000;
  int previousCadenceWatts[MAX_RESISTANCE / RESISTANCE_STEP + 1];
  for (size_t i = 0; i < sizeof(previousCadenceWatts) / sizeof(previousCadenceWatts[0]); ++i) previousCadenceWatts[i] = -1;

  for (int cadence = 1; cadence <= 130; ++cadence) {
    int previousWatts = -1;
    int resistanceIndex = 0;
    for (int resistance = 0; resistance <= MAX_RESISTANCE; resistance += RESISTANCE_STEP, ++resistanceIndex) {
      const int watts = helpers.lookupWatts(cadence, resistance, ptData);
      std::snprintf(failure, sizeof(failure), "reverse lookup out of range: cadence=%d resistance=%d watts=%d", cadence, resistance, watts);
      TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(0, watts, failure);
      TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(4000, watts, failure);
      if (previousWatts >= 0) {
        std::snprintf(failure, sizeof(failure),
                      "reverse lookup decreased with resistance: cadence=%d previous_resistance=%d previous_watts=%d resistance=%d watts=%d",
                      cadence, resistance - RESISTANCE_STEP, previousWatts, resistance, watts);
        TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(previousWatts, watts, failure);
      }
      if (previousCadenceWatts[resistanceIndex] >= 0) {
        std::snprintf(failure, sizeof(failure),
                      "reverse lookup decreased with cadence: resistance=%d previous_cadence=%d previous_watts=%d cadence=%d watts=%d",
                      resistance, cadence - 1, previousCadenceWatts[resistanceIndex], cadence, watts);
        TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(previousCadenceWatts[resistanceIndex], watts, failure);
      }
      previousWatts = watts;
      previousCadenceWatts[resistanceIndex] = watts;
    }
  }

  std::ofstream report("test/output/active_power_table_audit.txt", std::ios::trunc);
  report << "active ride entries=" << summary.entries << " invalid=" << summary.invalidEntries << '\n'
         << "trusted forward/reverse samples=" << trustedRoundTrips << '\n'
         << "maximum forward/reverse error=" << maximumRoundTripError << "W cadence=" << worstCadence
         << " expected=" << worstExpectedWatts << "W actual=" << worstActualWatts << "W\n";
  report.close();
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, trustedRoundTrips, "active table produced no locally trustworthy round-trip samples");
  std::snprintf(failure, sizeof(failure),
                "measured-envelope round-trip error exceeded one watt column: cadence=%d expected=%dW actual=%dW error=%dW",
                worstCadence, worstExpectedWatts, worstActualWatts, maximumRoundTripError);
  TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(30, maximumRoundTripError, failure);
}

void TestPTLookupWatts::test_reverse_lookup_pathological_tables(void) {
  PTHelpers helpers;

  PTData plateauTable;
  plateauTable.tableRow[3].tableEntry[3].targetPosition = 1000;
  plateauTable.tableRow[3].tableEntry[3].readings = 2;
  plateauTable.tableRow[3].tableEntry[4].targetPosition = 1000;
  plateauTable.tableRow[3].tableEntry[4].readings = 2;
  plateauTable.tableRow[3].tableEntry[5].targetPosition = 1100;
  plateauTable.tableRow[3].tableEntry[5].readings = 2;
  TEST_ASSERT_EQUAL_INT_MESSAGE(105, helpers.lookupWatts(75, 10000, plateauTable), "measured plateau midpoint changed");

  PTData flatTable;
  flatTable.tableRow[3].tableEntry[3].targetPosition = 1000;
  flatTable.tableRow[3].tableEntry[3].readings = 2;
  flatTable.tableRow[3].tableEntry[5].targetPosition = 1000;
  flatTable.tableRow[3].tableEntry[5].readings = 2;
  TEST_ASSERT_EQUAL_INT_MESSAGE(120, helpers.lookupWatts(75, 10000, flatTable), "flat table should return its observed watt midpoint");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, helpers.lookupWatts(75, 9000, flatTable), "flat table below its position must return zero watts");
  TEST_ASSERT_EQUAL_INT_MESSAGE(150, helpers.lookupWatts(75, 11000, flatTable), "flat table above its position must return its safe upper edge");
}
