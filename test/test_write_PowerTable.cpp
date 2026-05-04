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

void TestWritePowerTable::test_save_and_load(void) {
  std::ofstream logFile("test/output/test_ptdata_save_load.txt", std::ios::trunc);
  logFile << "Starting PTData save/load test\n";

  // Create a test power table
  PTData ptData;

  // Load the power table data from the source .ptab file
  const std::string inputFilePath = "test/data/Nebula3.ptab";
  loadCSVToPTData(inputFilePath, ptData);
  logFile << "Loaded power table from: " << inputFilePath << "\n";

  // Save the power table to the output file
  const std::string outputFilePath = "test/output/power_table.ptab";
  savePTDataToCSV(ptData, outputFilePath, false);
  logFile << "Saved power table to: " << outputFilePath << "\n";
  
  // Verify the file was created
  std::ifstream checkFile(outputFilePath);
  TEST_ASSERT_TRUE_MESSAGE(checkFile.good(), "Output file should be created successfully");
  checkFile.close();
  
  // Load the saved file back to verify
  PTData verifyData;
  loadCSVToPTData(outputFilePath, verifyData);
  
  // Verify that the data is identical
  bool dataMatches = true;
  for (int rowIndex = 0; rowIndex < POWERTABLE_CAD_SIZE; rowIndex++) {
    for (int colIndex = 0; colIndex < POWERTABLE_WATT_SIZE; colIndex++) {
      if (ptData.tableRow[rowIndex].tableEntry[colIndex].targetPosition != 
          verifyData.tableRow[rowIndex].tableEntry[colIndex].targetPosition) {
        dataMatches = false;
        logFile << "Data mismatch at row " << rowIndex << ", col " << colIndex << ": "
                << ptData.tableRow[rowIndex].tableEntry[colIndex].targetPosition << " vs "
                << verifyData.tableRow[rowIndex].tableEntry[colIndex].targetPosition << "\n";
      }
    }
  }
  
  TEST_ASSERT_TRUE_MESSAGE(dataMatches, "Round-trip data should be identical");
  logFile << "Test completed successfully - data preserved in round-trip conversion\n";
  logFile.close();
}
