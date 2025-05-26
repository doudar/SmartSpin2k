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
  const std::string filePath = "test/data/final_5_25_25.ptab";
  loadCSVToPTData(filePath, ptData);

  // Create helpers object for lookup
  PTHelpers helpers;

  // Lambda function for reusable lookup and logging
  auto performLookup = [&](int cadValue, int wattValue) {
    outFile << "Calling lookup with cadence: " << cadValue << ", watts: " << wattValue << " ";
    int32_t result = helpers.lookup(wattValue, cadValue, ptData);
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
      previousValue = result;
    }
  }

  // Print additional debug info
  outFile << "Test completed\n";
  outFile.close();
}