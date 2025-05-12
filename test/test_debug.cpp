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

void TestPTLookupWatts::test_debug_neighbors(void) {
  // Open output file and clear it at the start
  std::ofstream outFile("test/output/test_debug_neighbors.txt", std::ios::trunc);

  outFile << "Starting neighbor debug test\n";

  // Create a test power table with simple values
  PTData ptData;

  // Load the power table data from the .ptab file
  const std::string filePath = "test/data/power_table.ptab";
  loadCSVToPTData(filePath, ptData);

  // Create helpers object
  PTHelpers helpers;

  // List of (cadence, watts) pairs to test
  std::vector<std::pair<int, int>> testPairs = {
    {60, 270}, {65, 150}, {70, 300}, {80, 120}, {85, 390}
  };

  for (const auto& pair : testPairs) {
    int cadence = pair.first;
    int watts = pair.second;
    outFile << "\nTesting cadence " << cadence << ", watts " << watts << ":\n";
    ptIndex currentIndex = helpers.calculateIndex(watts, cadence);
    outFile << "Indices: cadIndex=" << static_cast<int>(currentIndex.cadIndex) << ", wattIndex=" << static_cast<int>(currentIndex.wattIndex) << "\n";
    outFile << "Value at position: " << ptData.tableRow[currentIndex.cadIndex].tableEntry[currentIndex.wattIndex].targetPosition << "\n";
    if (currentIndex.wattIndex > 0)
      outFile << "Left neighbor value (raw): " << ptData.tableRow[currentIndex.cadIndex].tableEntry[currentIndex.wattIndex-1].targetPosition << "\n";
    if (currentIndex.wattIndex < POWERTABLE_WATT_SIZE-1)
      outFile << "Right neighbor value (raw): " << ptData.tableRow[currentIndex.cadIndex].tableEntry[currentIndex.wattIndex+1].targetPosition << "\n";
    TestResults result = helpers.testNeighbors(currentIndex, INT16_MIN, ptData);
    outFile << "Left neighbor: found=" << result.leftNeighbor.found << ", pos=" << result.leftNeighbor.targetPosition << ", cadIndex=" << static_cast<int>(result.leftNeighbor.index.cadIndex) << ", wattIndex=" << static_cast<int>(result.leftNeighbor.index.wattIndex) << "\n";
    outFile << "Right neighbor: found=" << result.rightNeighbor.found << ", pos=" << result.rightNeighbor.targetPosition << ", cadIndex=" << static_cast<int>(result.rightNeighbor.index.cadIndex) << ", wattIndex=" << static_cast<int>(result.rightNeighbor.index.wattIndex) << "\n";
    int32_t lookupResult = helpers.lookup(watts, cadence, ptData);
    outFile << "Lookup result for " << cadence << "rpm, " << watts << "w: " << lookupResult << "\n";
  }
  outFile.close();
}