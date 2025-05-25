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
  const std::string inputFilePath = "test/data/power_table_incomplete.ptab";
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
    helpers.standardFill(ptData);
    logFile << "Applied standardFill to the table\n";

    // Count filled data points
    filledPoints = helpers.dataPoints(ptData);
    logFile << "Filled data points: " << filledPoints << "\n";
    logFile << "Added " << (filledPoints - initialPoints) << " points\n";
  }

  // loop through the table and check for INT16_MIN values
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      if (ptData.tableRow[i].tableEntry[j].targetPosition == INT16_MIN) {
        // lookup resistance for this position using watts and cadence
        int watts      = j * POWERTABLE_WATT_INCREMENT;
        int cad        = i * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD;
        int resistance = helpers.lookup(watts, cad, ptData) / TABLE_DIVISOR;
        ptIndex index;
        index.wattIndex     = j;
        index.cadIndex      = i;
        TestResults results = helpers.testNeighbors(index, resistance, ptData);
        logFile << "All neighbors passed: " << results.allNeighborsPassed << "\n";
        if (results.allNeighborsPassed == 1) {
          ptData.tableRow[i].tableEntry[j].targetPosition = resistance;
        } else {  // log the failure to in insert the value
          logFile << "Failed to fill position (" << i << ", " << j << ") with resistance: " << resistance << "\n";
        }// log failed neighbor resistance values
          logFile << "Left neighbor: found=" << results.leftNeighbor.found << ", pos=" << results.leftNeighbor.targetPosition
                  << ", cadIndex=" << static_cast<int>(results.leftNeighbor.index.cadIndex) << ", wattIndex=" << static_cast<int>(results.leftNeighbor.index.wattIndex)
                  << ", passed=" << results.leftNeighbor.passedTest << "\n";
          logFile << "Right neighbor: found=" << results.rightNeighbor.found << ", pos=" << results.rightNeighbor.targetPosition
                  << ", cadIndex=" << static_cast<int>(results.rightNeighbor.index.cadIndex) << ", wattIndex=" << static_cast<int>(results.rightNeighbor.index.wattIndex)
                  << ", passed=" << results.rightNeighbor.passedTest << "\n";
          logFile << "Top neighbor: found=" << results.topNeighbor.found << ", pos=" << results.topNeighbor.targetPosition
                  << ", cadIndex=" << static_cast<int>(results.topNeighbor.index.cadIndex) << ", wattIndex=" << static_cast<int>(results.topNeighbor.index.wattIndex)
                  << ", passed=" << results.topNeighbor.passedTest << "\n";
          logFile << "Bottom neighbor: found=" << results.bottomNeighbor.found << ", pos=" << results.bottomNeighbor.targetPosition
                  << ", cadIndex=" << static_cast<int>(results.bottomNeighbor.index.cadIndex) << ", wattIndex=" << static_cast<int>(results.bottomNeighbor.index.wattIndex)
                  << ", passed=" << results.bottomNeighbor.passedTest << "\n";
        logFile << "Filled position (" << i << ", " << j << ") with resistance: " << resistance << "\n";
      }
    }
  }
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
