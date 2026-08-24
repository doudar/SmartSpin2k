/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>
#include "test.h"
#include "PowerTable_Helpers.h"
// Doesn't need to be included again, since is't already in test_pt_lookup_resistance.cpp
// #include "../src/PowerTable_Helpers.cpp"
#include <sdkconfig.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "data_helpers.cpp"

void TestPTLookupWatts::test_pt_lookup_watts(void) {
  std::ofstream outFile("test/output/test_pt_lookup_watts.txt", std::ios::trunc);
  outFile << "Starting lookup test\n";

  // Create a test power table with simple values
  PTData ptData;

  // Load the power table data from the .ptab file
  const std::string filePath = "test/data/converged_nebula3.ptab";
  loadCSVToPTData(filePath, ptData);

    // Print the loaded power table data
  outFile << "\n=== Power Table Data ===\n";
  outFile << "Cadence/Power";
  for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
    outFile << "," << (j * POWERTABLE_WATT_INCREMENT) << "W";
  }
  outFile << "\n";
  
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    outFile << (MINIMUM_TABLE_CAD + i * POWERTABLE_CAD_INCREMENT) << "RPM";
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      outFile << ",";
      if (ptData.tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
        outFile << ptData.tableRow[i].tableEntry[j].targetPosition;
      } else {
        outFile << " ";
      }
    }
    outFile << "\n";
  }
  outFile << "=== End Power Table ===\n\n";

  // Create helpers object for lookup
  PTHelpers helpers;

  // Known samples must round-trip exactly. Intermediate and out-of-range
  // resistance values use the nearest segment for interpolation/extrapolation.
  const int stoppedWatts = helpers.lookupWatts(0, 0, ptData);
  const int knownWatts = helpers.lookupWatts(75, 8780, ptData);
  const int interpolatedWatts = helpers.lookupWatts(75, 9310, ptData);
  const int resistanceExtrapolatedWatts = helpers.lookupWatts(75, 16000, ptData);
  const int cadenceExtrapolatedWatts = helpers.lookupWatts(50, 10000, ptData);
  outFile << "Boundary checks: stopped=" << stoppedWatts << ", known=" << knownWatts << ", interpolated=" << interpolatedWatts
          << ", resistance extrapolated=" << resistanceExtrapolatedWatts << ", cadence extrapolated=" << cadenceExtrapolatedWatts << "\n";
  outFile.flush();
  TEST_ASSERT_EQUAL_INT(0, stoppedWatts);
  TEST_ASSERT_EQUAL_INT(90, knownWatts);
  TEST_ASSERT_EQUAL_INT(105, interpolatedWatts);
  TEST_ASSERT_TRUE(resistanceExtrapolatedWatts > 390);
  TEST_ASSERT_TRUE(cadenceExtrapolatedWatts > 0 && cadenceExtrapolatedWatts < helpers.lookupWatts(75, 10000, ptData));
  TEST_ASSERT_EQUAL_INT(68, helpers.lookupWatts(75, 8000, ptData));
  TEST_ASSERT_GREATER_OR_EQUAL_INT(68, helpers.lookupWatts(76, 8000, ptData));
  TEST_ASSERT_EQUAL_INT(113, helpers.lookupWatts(75, 9600, ptData));
  TEST_ASSERT_GREATER_OR_EQUAL_INT(113, helpers.lookupWatts(76, 9600, ptData));

  // A measured plateau has no unique inverse. Use the midpoint of its known
  // watt interval and remain finite on either side of a completely flat row.
  PTData plateauTable;
  plateauTable.tableRow[3].tableEntry[3].targetPosition = 1000;
  plateauTable.tableRow[3].tableEntry[3].readings       = 2;
  plateauTable.tableRow[3].tableEntry[4].targetPosition = 1000;
  plateauTable.tableRow[3].tableEntry[4].readings       = 2;
  plateauTable.tableRow[3].tableEntry[5].targetPosition = 1100;
  plateauTable.tableRow[3].tableEntry[5].readings       = 2;
  TEST_ASSERT_EQUAL_INT(105, helpers.lookupWatts(75, 10000, plateauTable));

  PTData flatTable;
  flatTable.tableRow[3].tableEntry[3].targetPosition = 1000;
  flatTable.tableRow[3].tableEntry[3].readings       = 2;
  flatTable.tableRow[3].tableEntry[5].targetPosition = 1000;
  flatTable.tableRow[3].tableEntry[5].readings       = 2;
  TEST_ASSERT_EQUAL_INT(120, helpers.lookupWatts(75, 10000, flatTable));
  TEST_ASSERT_EQUAL_INT(0, helpers.lookupWatts(75, 9000, flatTable));
  TEST_ASSERT_EQUAL_INT(150, helpers.lookupWatts(75, 11000, flatTable));

  // Public lookup inputs are bounded and conversions saturate instead of
  // overflowing, even for corrupted or adversarial callers.
  TEST_ASSERT_EQUAL_INT32(RETURN_ERROR, helpers.lookup(-1, 75, ptData));
  TEST_ASSERT_NOT_EQUAL(RETURN_ERROR, helpers.lookup(INT32_MAX, 1, ptData));
  TEST_ASSERT_EQUAL_INT(0, helpers.lookupWatts(75, INT32_MIN, ptData));
  TEST_ASSERT_EQUAL_INT(4000, helpers.lookupWatts(75, INT32_MAX, ptData));
  TEST_ASSERT_EQUAL_INT(4000, helpers.lookupWatts(INT32_MAX, INT32_MAX, ptData));

  // Every populated calibration sample in the actual ptab must invert back to
  // its watt column exactly when its row has enough points to define the
  // forward curve. Singleton rows are deliberately ignored by lookup().
  for (int row = 0; row < POWERTABLE_CAD_SIZE; row++) {
    const int sampleCadence = MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT;
    int reliableSamples = 0;
    for (int col = 0; col < POWERTABLE_WATT_SIZE; col++) {
      const TableEntry& sample = ptData.tableRow[row].tableEntry[col];
      if (sample.targetPosition != INT16_MIN && sample.readings >= 2) reliableSamples++;
    }
    if (reliableSamples < 2) continue;

    for (int col = 0; col < POWERTABLE_WATT_SIZE; col++) {
      const TableEntry& sample = ptData.tableRow[row].tableEntry[col];
      if (sample.targetPosition == INT16_MIN || sample.readings < 2) continue;
      const int expectedWatts = col * POWERTABLE_WATT_INCREMENT;
      const int actualWatts = helpers.lookupWatts(sampleCadence, sample.targetPosition * TABLE_DIVISOR, ptData);
      if (actualWatts != expectedWatts) {
        outFile << "Round-trip mismatch: cadence=" << sampleCadence << ", position=" << sample.targetPosition * TABLE_DIVISOR
                << ", expected=" << expectedWatts << ", actual=" << actualWatts << "\n";
        outFile.flush();
      }
      TEST_ASSERT_EQUAL_INT(expectedWatts, actualWatts);
    }
  }

  int maximumRoundTripError = 0;
  int worstRoundTripCadence = 0;
  int worstRoundTripExpected = 0;
  int worstRoundTripActual = 0;
  for (int cadence = 75; cadence <= 100; ++cadence) {
    for (int expectedWatts = 150; expectedWatts <= 600; expectedWatts += 30) {
      const int32_t position = helpers.lookup(expectedWatts, cadence, ptData);
      TEST_ASSERT_NOT_EQUAL(RETURN_ERROR, position);
      const int actualWatts = helpers.lookupWatts(cadence, position, ptData);
      const int error = abs(actualWatts - expectedWatts);
      if (error > maximumRoundTripError) {
        maximumRoundTripError = error;
        worstRoundTripCadence = cadence;
        worstRoundTripExpected = expectedWatts;
        worstRoundTripActual = actualWatts;
      }
    }
  }
  outFile << "Maximum forward/reverse round-trip error: " << maximumRoundTripError << "W at " << worstRoundTripCadence
          << " RPM (expected " << worstRoundTripExpected << ", actual " << worstRoundTripActual << ")\n";
  outFile.flush();
  TEST_ASSERT_LESS_OR_EQUAL_INT(30, maximumRoundTripError);

  // Lambda function for reusable lookup and logging
  auto performLookup = [&](int cad, int resistance) {
    outFile << "Calling lookup CAD " << cad << ", with Resistance: " << resistance;
    int32_t result = helpers.lookupWatts(cad, resistance, ptData);
    outFile << "Lookup returned: " << result << "w\n";
    return result;
  };

  // Test cadence from 10-130 using resistance range from -DEFAULT_STEPPER_TRAVEL to +DEFAULT_STEPPER_TRAVEL. For each cadence, as resistance increases, check that the output watt
  // values are increasing. When a new cadence is reached, the watt values should be higher than the previous cadence or the test will fail.

  // Define test cadence range
  int minCadence  = 40;
  int maxCadence  = 130;
  int cadenceStep = 1;

#define MIN_TEST_RANGE     0
#define MAX_TEST_RANGE     1600 * TABLE_DIVISOR
#define POINTS_PER_CADENCE 10
  // Define resistance test points (using a smaller range for testing efficiency)
  int resistancePoints = POINTS_PER_CADENCE;
  int resistanceStep   = MAX_TEST_RANGE / POINTS_PER_CADENCE;

  int32_t previousMaxWatts = INT32_MIN;  // Track max watts from previous cadence
  int32_t previousCadenceWatts[POINTS_PER_CADENCE + 1];
  for (int i = 0; i <= POINTS_PER_CADENCE; ++i) previousCadenceWatts[i] = INT32_MIN;

  outFile << "Testing cadence range " << minCadence << "-" << maxCadence << " with " << resistancePoints << " resistance points\n";

  // Iterate through each cadence value
  for (int cadence = minCadence; cadence <= maxCadence; cadence += cadenceStep) {
    outFile << "\n--- Testing cadence " << cadence << " ---\n";

    int32_t previousWatts      = INT32_MIN;  // Track previous watts within this cadence
    int32_t maxWattsForCadence = INT32_MIN;  // Track max watts for this cadence

    // Test resistance points from negative to positive values
    int resistanceIndex = 0;
    for (int32_t resistance = 0; resistance <= MAX_TEST_RANGE; resistance += resistanceStep, ++resistanceIndex) {
      int32_t watts = performLookup(cadence, resistance);

      // Check that watts increase as resistance increases for the same cadence
      if ((previousWatts != INT32_MIN) && (watts < previousWatts)) {
        outFile << "Watts should increase or stay the same as resistance increases. Previous watts: " << previousWatts << ", Current watts: " << watts << "\n";
      }
      TEST_ASSERT_GREATER_OR_EQUAL_INT32(previousWatts, watts);

      if (previousCadenceWatts[resistanceIndex] != INT32_MIN) {
        TEST_ASSERT_GREATER_OR_EQUAL_INT32(previousCadenceWatts[resistanceIndex], watts);
        TEST_ASSERT_LESS_OR_EQUAL_INT32(30, watts - previousCadenceWatts[resistanceIndex]);
      }
      previousCadenceWatts[resistanceIndex] = watts;

      // Update max watts for this cadence
      if (watts > maxWattsForCadence) {
        maxWattsForCadence = watts;
      }

      previousWatts = watts;
    }

    // Check that max watts increase as cadence increases
    if (previousMaxWatts != INT32_MIN && cadence > minCadence) {
      // TEST_ASSERT_TRUE_MESSAGE(
      //   maxWattsForCadence > previousMaxWatts,
      //   "Max watts should increase with higher cadence"
      // );

      outFile << "Max watts increased from " << previousMaxWatts << " to " << maxWattsForCadence << " when cadence changed from " << (cadence - cadenceStep) << " to " << cadence
              << "\n";
    }

    previousMaxWatts = maxWattsForCadence;
  }

  // Print additional debug info
  outFile << "Test completed\n";
}
