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

void TestTableFill::test_fill_incomplete_table(void) {
  std::ofstream logFile("test/output/test_table_fill.txt", std::ios::trunc);
  logFile << "Starting incomplete table fill test\n";

  // Create a power table
  PTData ptData;

  // Load the incomplete power table data
  const std::string inputFilePath = "test/data/10x5-26-25.ptab";
  loadCSVToPTData(inputFilePath, ptData);
  logFile << "Loaded incomplete power table from: " << inputFilePath << "\n";

  // Create helpers object
  PTHelpers helpers;

  // Count initial data points
  int initialPoints = helpers.dataPoints(ptData);
  logFile << "Initial data points: " << initialPoints << "\n";
  int previousFilledPoints = 0;
  int filledPoints         = 1;
  // Fill the incomplete table
  while (previousFilledPoints < filledPoints) {
    previousFilledPoints = filledPoints;
    helpers.splineFill(ptData);
    logFile << "Applied splineFill to the table\n";

    // Count filled data points
    filledPoints = helpers.dataPoints(ptData);
    logFile << "Filled data points: " << filledPoints << "\n";
    logFile << "Added " << (filledPoints - initialPoints) << " points\n";
  }

  // loop through the table and check for INT16_MIN values
  helpers.linearFill(ptData);
  // Save the filled power table
  const std::string outputFilePath = "test/output/power_table_filled.ptab";
  savePTDataToCSV(ptData, outputFilePath);
  logFile << "Saved filled power table to: " << outputFilePath << "\n";

  // Verify the file was created
  std::ifstream checkFile(outputFilePath);
  TEST_ASSERT_TRUE_MESSAGE(checkFile.good(), "Output file should be created successfully");
  checkFile.close();

  logFile << "Test completed successfully\n";
  logFile.close();
}
