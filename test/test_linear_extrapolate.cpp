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

  // For each power column, extract data points for known cadences
  // and test extrapolation for cadences below and above the range
  for (int wattIndex = 0; wattIndex < POWERTABLE_WATT_SIZE; wattIndex++) {
    int wattValue = wattIndex * POWERTABLE_WATT_INCREMENT;

    // Extract cadence and target position data for this wattage
    std::pair<std::vector<float>, std::vector<float>> xy = helpers.getColumn(wattIndex, ptData);
    
    // Need at least 2 data points for extrapolation
    if (xy.first.size() < 2) {
      continue;
    }

    outFile << "\n\nTesting cadence extrapolation for " << wattValue << "W...\n";

    // Test cadence values below minimum (50, 40, 30 RPM)
    for (int testCadence = 0; testCadence <= 130; testCadence += 1) {
      float extrapolated = helpers.linearExtrapolate(xy, xy.first.size(), static_cast<float>(testCadence));
      outFile << "Cadence " << testCadence << " RPM -> Extrapolated position: " << extrapolated << "\n";
    }
  }

  // For each row, extract data points for known wattages
  // and test extrapolation for wattages below and above the range
  for (int cadIndex = 0; cadIndex < POWERTABLE_CAD_SIZE; cadIndex++) {
    int cadValue = MINIMUM_TABLE_CAD + cadIndex * POWERTABLE_CAD_INCREMENT;

    // Extract wattage and target position data for this cadence
    std::pair<std::vector<float>, std::vector<float>> xy = helpers.getRow(cadIndex, ptData);
    
    // Need at least 2 data points for extrapolation
    if (xy.first.size() < 2) {
      continue;
    }

    outFile << "\n\nTesting wattage extrapolation for " << cadValue << " RPM...\n";

    // Test watt values below minimum (50, 40, 30 W)
    for (int testWatt = 0; testWatt <= 800; testWatt += 10) {
      float extrapolated = helpers.linearExtrapolate(xy, xy.first.size(), static_cast<float>(testWatt));
      outFile << "Watt " << testWatt << " W -> Extrapolated position: " << extrapolated << "\n";
    }

  }

  // Direct test with controlled values
  outFile << "\n--- Testing linearExtrapolate with controlled values ---\n";

  // Test case 1: Simple linear extrapolation below range
  {
    std::vector<float> x = {60.0f, 65.0f, 70.0f};
    std::vector<float> y = {100.0f, 90.0f, 80.0f};  // Decreasing values with increasing x

    // Extrapolate to x=50
    float result = helpers.linearExtrapolate(std::make_pair(x, y), 3, 50.0f);
    outFile << "Extrapolate x=50: Expected ~120, Got " << result << "\n";
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(5.0f, 120.0f, result, "Simple low extrapolation failed");
  }

  // Test case 2: Simple linear extrapolation above range
  {
    std::vector<float> x = {80.0f, 90.0f, 100.0f};
    std::vector<float> y = {50.0f, 40.0f, 30.0f};  // Decreasing values with increasing x

    // Extrapolate to x=120
    float result = helpers.linearExtrapolate(std::make_pair(x, y), 3, 120.0f);
    outFile << "Extrapolate x=120: Expected ~10, Got " << result << "\n";
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(5.0f, 10.0f, result, "Simple high extrapolation failed");
  }

  outFile << "Test completed\n";
  outFile.close();
}