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
    {30, 270},{40, 270},{50, 270},{55, 270},{58, 270},{60, 270}, {62, 270}, {63, 270}, {64, 270}, {85, 390}, {60,355}, {58, 355}, {55,355}, {105, 355}, {102,355}
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
    outFile << "Top neighbor: found=" << result.topNeighbor.found << ", pos=" << result.topNeighbor.targetPosition << ", cadIndex=" << static_cast<int>(result.topNeighbor.index.cadIndex) << ", wattIndex=" << static_cast<int>(result.topNeighbor.index.wattIndex) << "\n";
    outFile << "Bottom neighbor: found=" << result.bottomNeighbor.found << ", pos=" << result.bottomNeighbor.targetPosition << ", cadIndex=" << static_cast<int>(result.bottomNeighbor.index.cadIndex) << ", wattIndex=" << static_cast<int>(result.bottomNeighbor.index.wattIndex) << "\n";
    int32_t lookupResult = helpers.lookup(watts, cadence, ptData);
    outFile << "Lookup result for " << cadence << "rpm, " << watts << "w: " << lookupResult << "\n";
    //print all neighbors found value
    outFile << "All neighbors found: " << result.allNeighborsFound << "\n";
    //print all neighbors passed value
    outFile << "All neighbors passed: " << result.allNeighborsPassed << "\n";
  }
  outFile.close();
}