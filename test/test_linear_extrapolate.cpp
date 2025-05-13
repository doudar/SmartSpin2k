/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>
#include "test.h"
#include "PowerTable_Helpers.h"
#include <sdkconfig.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "data_helpers.cpp"

void TestLinearExtrapolate::test_linear_extrapolate(void) {
  std::ofstream outFile("test/output/test_linear_extrapolate.txt", std::ios::trunc);
  outFile << "Starting linear extrapolation test\n";

  // Create a test power table and load data from the .ptab file
  PTData ptData;
  const std::string filePath = "test/data/power_table.ptab";
  loadCSVToPTData(filePath, ptData);

  // Create helpers object
  PTHelpers helpers;

  // Test low cadence extrapolation (below MINIMUM_TABLE_CAD which is 60 RPM)
  outFile << "\n--- Testing low cadence extrapolation ---\n";

  // For each power column, extract data points for known cadences
  // and test extrapolation for cadences below the table minimum
  for (int wattIndex = 0; wattIndex < POWERTABLE_WATT_SIZE; wattIndex++) {
    int wattValue = wattIndex * POWERTABLE_WATT_INCREMENT;

    // Skip columns with insufficient data
    int validDataPoints = 0;
    for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
      if (ptData.tableRow[i].tableEntry[wattIndex].targetPosition != INT16_MIN) {
        validDataPoints++;
      }
    }

    // Need at least 2 data points for extrapolation
    if (validDataPoints < 2) {
      continue;
    }

    outFile << "\n\nTesting low cadence extrapolation for " << wattValue << "W...\n";

    // Extract cadence and target position data for this wattage
    std::vector<float> cadValues;
    std::vector<float> positionValues;

    for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
      if (ptData.tableRow[i].tableEntry[wattIndex].targetPosition != INT16_MIN) {
        cadValues.push_back(static_cast<float>(i * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD));
        positionValues.push_back(static_cast<float>(ptData.tableRow[i].tableEntry[wattIndex].targetPosition));
        outFile << "Cadence: " << cadValues.back() << ", Position: " << positionValues.back() << "\n";
      }
    }

    // Test cadence values below minimum (50, 40, 30 RPM)
    for (int testCadence = 70; testCadence >= 30; testCadence -= 1) {
      float extrapolated = helpers.linearExtrapolate(cadValues.data(), positionValues.data(), cadValues.size(), static_cast<float>(testCadence));
      outFile << "Cadence " << testCadence << " RPM -> Extrapolated position: " << extrapolated << "\n";
    }
  }

  // Test high cadence extrapolation (above the highest in table which is 105 RPM)
  outFile << "\n--- Testing high cadence extrapolation ---\n";

  // For each power column, extract data points and test extrapolation for high cadences
  for (int wattIndex = 0; wattIndex < POWERTABLE_WATT_SIZE; wattIndex++) {
    int wattValue = wattIndex * POWERTABLE_WATT_INCREMENT;

    // Skip columns with insufficient data
    int validDataPoints = 0;
    for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
      if (ptData.tableRow[i].tableEntry[wattIndex].targetPosition != INT16_MIN) {
        validDataPoints++;
      }
    }

    // Need at least 2 data points for extrapolation
    if (validDataPoints < 2) {
      continue;
    }

    outFile << "\n\nTesting high cadence extrapolation for " << wattValue << "W...\n";

    // Extract cadence and target position data for this wattage
    std::vector<float> cadValues;
    std::vector<float> positionValues;

    for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
      if (ptData.tableRow[i].tableEntry[wattIndex].targetPosition != INT16_MIN) {
        cadValues.push_back(static_cast<float>(i * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD));
        positionValues.push_back(static_cast<float>(ptData.tableRow[i].tableEntry[wattIndex].targetPosition));
        outFile << "Cadence: " << cadValues.back() << ", Position: " << positionValues.back() << "\n";
      }
    }

    // Test cadence values above maximum (110, 120, 130 RPM)
    for (int testCadence = 110; testCadence <= 130; testCadence += 10) {
      float extrapolated = helpers.linearExtrapolate(cadValues.data(), positionValues.data(), cadValues.size(), static_cast<float>(testCadence));
      outFile << "Cadence " << testCadence << " RPM -> Extrapolated position: " << extrapolated << "\n";
    }
  }

  // Direct test with controlled values
  outFile << "\n--- Testing linearExtrapolate with controlled values ---\n";

  // Test case 1: Simple linear extrapolation below range
  {
    float x[] = {60.0f, 65.0f, 70.0f};
    float y[] = {100.0f, 90.0f, 80.0f};  // Decreasing values with increasing x

    // Extrapolate to x=50
    float result = helpers.linearExtrapolate(x, y, 3, 50.0f);
    outFile << "Extrapolate x=50: Expected ~120, Got " << result << "\n";
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(5.0f, 120.0f, result, "Simple low extrapolation failed");
  }

  // Test case 2: Simple linear extrapolation above range
  {
    float x[] = {80.0f, 90.0f, 100.0f};
    float y[] = {50.0f, 40.0f, 30.0f};  // Decreasing values with increasing x

    // Extrapolate to x=120
    float result = helpers.linearExtrapolate(x, y, 3, 120.0f);
    outFile << "Extrapolate x=120: Expected ~10, Got " << result << "\n";
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(5.0f, 10.0f, result, "Simple high extrapolation failed");
  }

  outFile << "Test completed\n";
  outFile.close();
}