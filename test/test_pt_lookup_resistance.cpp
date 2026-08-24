/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>
#include "test.h"
#include "PowerTable_Helpers.h"
#include "../src/PowerTable_Helpers.cpp"
#include <sdkconfig.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "data_helpers.cpp"

void TestPTLookupResistance::test_pt_lookup_resistance(void) {
  std::ofstream outFile("test/output/test_pt_lookup_resistance.txt", std::ios::trunc);
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

  TEST_ASSERT_EQUAL_INT32(8780, helpers.lookup(90, 75, ptData));
  TEST_ASSERT_EQUAL_INT32(9310, helpers.lookup(105, 75, ptData));
  TEST_ASSERT_EQUAL_INT32(15980, helpers.lookup(420, 75, ptData));
  TEST_ASSERT_EQUAL_INT32(11524, helpers.lookup(210, 77, ptData));
  // 390 W at 130 RPM has the same torque as 300 W at the highest reliable
  // 100 RPM row, where the measured position is 1152.
  TEST_ASSERT_EQUAL_INT32(11520, helpers.lookup(390, 130, ptData));
  TEST_ASSERT_EQUAL_INT32(RETURN_ERROR, helpers.lookup(200, 0, ptData));

  // Monotonic correction must compare measured samples across empty grid
  // cells without synthesizing values into those gaps.
  PTData sparsePowerTable;
  sparsePowerTable.tableRow[2].tableEntry[1].targetPosition = 200;
  sparsePowerTable.tableRow[2].tableEntry[1].readings       = 2;
  sparsePowerTable.tableRow[2].tableEntry[3].targetPosition = 100;
  sparsePowerTable.tableRow[2].tableEntry[3].readings       = 2;
  ptIndex newPowerSample;
  newPowerSample.cadIndex  = 2;
  newPowerSample.wattIndex = 5;
  helpers.enterData(sparsePowerTable, newPowerSample, 300);
  TEST_ASSERT_EQUAL_INT16(150, sparsePowerTable.tableRow[2].tableEntry[1].targetPosition);
  TEST_ASSERT_EQUAL_INT16(INT16_MIN, sparsePowerTable.tableRow[2].tableEntry[2].targetPosition);
  TEST_ASSERT_EQUAL_INT16(150, sparsePowerTable.tableRow[2].tableEntry[3].targetPosition);

  PTData sparseCadenceTable;
  sparseCadenceTable.tableRow[1].tableEntry[5].targetPosition = 100;
  sparseCadenceTable.tableRow[1].tableEntry[5].readings       = 2;
  sparseCadenceTable.tableRow[3].tableEntry[5].targetPosition = 200;
  sparseCadenceTable.tableRow[3].tableEntry[5].readings       = 2;
  ptIndex newCadenceSample;
  newCadenceSample.cadIndex  = 5;
  newCadenceSample.wattIndex = 5;
  helpers.enterData(sparseCadenceTable, newCadenceSample, 50);
  TEST_ASSERT_EQUAL_INT16(150, sparseCadenceTable.tableRow[1].tableEntry[5].targetPosition);
  TEST_ASSERT_EQUAL_INT16(INT16_MIN, sparseCadenceTable.tableRow[2].tableEntry[5].targetPosition);
  TEST_ASSERT_EQUAL_INT16(150, sparseCadenceTable.tableRow[3].tableEntry[5].targetPosition);

  // Every populated calibration sample in the actual ptab must map back to its
  // exact resistance position when its row contains enough data to define a
  // local curve. A singleton row is deliberately ignored as underdetermined.
  for (int row = 0; row < POWERTABLE_CAD_SIZE; row++) {
    int reliableSamples = 0;
    for (int col = 0; col < POWERTABLE_WATT_SIZE; col++) {
      const TableEntry& sample = ptData.tableRow[row].tableEntry[col];
      if (sample.targetPosition != INT16_MIN && sample.readings >= 2) reliableSamples++;
    }
    if (reliableSamples < 2) continue;

    const int sampleCadence = MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT;
    for (int col = 0; col < POWERTABLE_WATT_SIZE; col++) {
      const TableEntry& sample = ptData.tableRow[row].tableEntry[col];
      if (sample.targetPosition == INT16_MIN || sample.readings < 2) continue;
      TEST_ASSERT_EQUAL_INT32(sample.targetPosition * TABLE_DIVISOR,
                              helpers.lookup(col * POWERTABLE_WATT_INCREMENT, sampleCadence, ptData));
    }
  }

  // Lambda function for reusable lookup and logging
  auto performLookup = [&](int cadValue, int wattValue) {
    outFile << "Calling lookup with cadence: " << cadValue << ", watts: " << wattValue << " ";
    int32_t result = helpers.lookup(wattValue, cadValue, ptData);
    //int32_t result = helpers.getInterpolatedPosition(ptData, wattValue, cadValue);
    outFile << "Lookup returned: " << result << "\n";
    return result;
  };

  // Iterate through each cadence line and check values
  for (int rowIndex = 0; rowIndex < POWERTABLE_CAD_SIZE; ++rowIndex) {
    int32_t previousValue = INT32_MIN;
    for (int colIndex = 0; colIndex < POWERTABLE_WATT_SIZE; ++colIndex) {
      int cadValue   = MINIMUM_TABLE_CAD + rowIndex * POWERTABLE_CAD_INCREMENT;
      int wattValue  = colIndex * POWERTABLE_WATT_INCREMENT;
      int32_t result = performLookup(cadValue, wattValue);

      // Ensure the value is higher than the previous value on the same line
      if (result < previousValue) {
        outFile << "Error: Lookup value " << result << " is less than previous value " << previousValue << " for cadence " << cadValue << ", watts " << wattValue << "\n";
      }
      TEST_ASSERT_GREATER_OR_EQUAL_INT32(previousValue, result);
      previousValue = result;
    }
  }

  // Print additional debug info
  outFile << "Test completed\n";
  outFile.close();
}
