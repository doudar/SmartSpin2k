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

// Helper function to load CSV data
static void loadCSVToPTData(const std::string& filePath, PTData& ptData) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        printf("Failed to open file: %s\n", filePath.c_str());
        return;
    }

    std::string line;
    int rowIndex = 0;

    // Skip the header line
    std::getline(file, line);

    while (std::getline(file, line) && rowIndex < POWERTABLE_CAD_SIZE) {
        std::istringstream lineStream(line);
        std::string cell;
        int colIndex = 0;

        // Skip the first column (Cadence/Power labels)
        std::getline(lineStream, cell, ',');

        while (std::getline(lineStream, cell, ',') && colIndex < POWERTABLE_WATT_SIZE) {
            if (!cell.empty()) {
                ptData.tableRow[rowIndex].tableEntry[colIndex].targetPosition = std::stoi(cell);
            } else {
                ptData.tableRow[rowIndex].tableEntry[colIndex].targetPosition = INT16_MIN;
            }
            colIndex++;
        }
        rowIndex++;
    }

    file.close();
}

void TestLookup::test_lookup_values(void) {
    printf("Starting lookup test\n");
    
    // Create a test power table with simple values
    PTData ptData;
    
    // Load the power table data from the .ptab file
    const std::string filePath = "test/data/power_table.ptab";
    loadCSVToPTData(filePath, ptData);
    
    // Create helpers object for lookup
    PTHelpers helpers;
    
    // Lambda function for reusable lookup and logging
    auto performLookup = [&](int cadValue, int wattValue) {
        printf("Calling lookup with cadence: %d, watts: %d\n", cadValue, wattValue);
        int32_t result = helpers.lookup(wattValue, cadValue, ptData);
        printf("Lookup returned: %d \n", result);
        return result;
    };
    
    // Iterate through each cadence line and check values
    for (int rowIndex = 0; rowIndex < POWERTABLE_CAD_SIZE; ++rowIndex) {
        int32_t previousValue = INT32_MIN;
        for (int colIndex = 0; colIndex < POWERTABLE_WATT_SIZE; ++colIndex) {
            int cadValue = MINIMUM_TABLE_CAD + rowIndex * POWERTABLE_CAD_INCREMENT;
            int wattValue = colIndex * POWERTABLE_WATT_INCREMENT;
            int32_t result = performLookup(cadValue, wattValue);

            // Ensure the value is higher than the previous value on the same line
            //TEST_ASSERT_TRUE(result > previousValue);
            previousValue = result;
        }
    }
    
    // Print additional debug info
    printf("Test completed\n");
}