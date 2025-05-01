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