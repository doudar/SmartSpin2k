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
  printf("Starting neighbor debug test\n");

  // Create a test power table with simple values
  PTData ptData;

  // Load the power table data from the .ptab file
  const std::string filePath = "test/data/power_table.ptab";
  loadCSVToPTData(filePath, ptData);

  // Create helpers object
  PTHelpers helpers;

  // First, let's try to reproduce the specific issue with cadence 60, watts 270
  ptIndex currentIndex;
  currentIndex.cadIndex = round(((float)60 - (float)MINIMUM_TABLE_CAD) / (float)POWERTABLE_CAD_INCREMENT);
  currentIndex.wattIndex = round((float)270 / (float)POWERTABLE_WATT_INCREMENT);
  
  printf("Indices for cadence 60, watts 270: cadIndex=%d, wattIndex=%d\n", currentIndex.cadIndex, currentIndex.wattIndex);
  
  // Print the value at this position
  printf("Value at position: %d\n", ptData.tableRow[currentIndex.cadIndex].tableEntry[currentIndex.wattIndex].targetPosition);
  
  // Print neighboring values in the raw table
  printf("Left neighbor value (raw): %d\n", ptData.tableRow[currentIndex.cadIndex].tableEntry[currentIndex.wattIndex-1].targetPosition);
  printf("Right neighbor value (raw): %d\n", ptData.tableRow[currentIndex.cadIndex].tableEntry[currentIndex.wattIndex+1].targetPosition);
  
  // Now test the neighbors function
  TestResults result = helpers.testNeighbors(currentIndex, INT16_MIN, ptData);
  
  // Print each returned found flag and value for manual verification
  printf("Left neighbor: found=%d, pos=%d, cadIndex=%d, wattIndex=%d\n",
         result.leftNeighbor.found, result.leftNeighbor.targetPosition,
         result.leftNeighbor.index.cadIndex, result.leftNeighbor.index.wattIndex);
  
  printf("Right neighbor: found=%d, pos=%d, cadIndex=%d, wattIndex=%d\n",
         result.rightNeighbor.found, result.rightNeighbor.targetPosition,
         result.rightNeighbor.index.cadIndex, result.rightNeighbor.index.wattIndex);

  // Try the lookup function as well to see the overall behavior
  int32_t lookupResult = helpers.lookup(270, 60, ptData);
  printf("Lookup result for 60rpm, 270w: %d\n", lookupResult);
}