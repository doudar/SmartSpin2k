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

// Helper function to save PTData to CSV file
static void savePTDataToCSV(const PTData& ptData, const std::string& filePath) {
    std::ofstream file(filePath);
    if (!file.is_open()) {
        printf("Failed to create file: %s\n", filePath.c_str());
        return;
    }

    // Write header row
    file << "Cadence/Power";
    for (int watt = 0; watt < POWERTABLE_WATT_SIZE; watt++) {
        file << "," << watt * 30 << "W"; // Assuming 30W increments
    }
    file << std::endl;

    // Write data rows
    for (int cad = 0; cad < POWERTABLE_CAD_SIZE; cad++) {
        file << (60 + cad * 5) << "RPM"; // Assuming 5 RPM increments starting at 60
        
        for (int watt = 0; watt < POWERTABLE_WATT_SIZE; watt++) {
            file << ",";
            if (ptData.tableRow[cad].tableEntry[watt].targetPosition != INT16_MIN) {
                file << ptData.tableRow[cad].tableEntry[watt].targetPosition;
            }
            // If the value is INT16_MIN, leave the cell empty
        }
        file << std::endl;
    }

    file.close();
    printf("Successfully saved power table to: %s\n", filePath.c_str());
}

