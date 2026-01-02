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
#include <regex>

#ifdef _WIN32
  #include <direct.h> // for _mkdir, _rmdir
  #include <io.h>     // for _findfirst, _findnext, _findclose, _unlink
#else
  #include <dirent.h>  // for opendir, readdir, closedir
  #include <sys/stat.h> // for mkdir
  #include <unistd.h>  // for rmdir, unlink
#endif

#include "data_helpers.cpp"

// Helper to clean and recreate a directory
static void cleanAndCreateDir(const std::string& dirPath) {
#ifdef _WIN32
    // Remove all files in the directory (Windows)
    struct _finddata_t fileinfo;
    std::string pattern = dirPath + "*";
    intptr_t hFile = _findfirst(pattern.c_str(), &fileinfo);
    if (hFile != -1) {
        do {
            if (!(fileinfo.attrib & _A_SUBDIR)) {
                std::string filepath = dirPath + fileinfo.name;
                _unlink(filepath.c_str());
            }
        } while (_findnext(hFile, &fileinfo) == 0);
        _findclose(hFile);
    }
    // Remove the directory itself if it exists
    _rmdir(dirPath.c_str());
    // Recreate the directory
    _mkdir(dirPath.c_str());
#else
    // Remove all files in the directory (POSIX)
    DIR* dir = opendir(dirPath.c_str());
    if (dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            if (filename != "." && filename != "..") {
                std::string filepath = dirPath + filename;
                unlink(filepath.c_str());
            }
        }
        closedir(dir);
    }
    // Remove the directory itself if it exists
    rmdir(dirPath.c_str());
    // Recreate the directory
    mkdir(dirPath.c_str(), 0755);
#endif
}

void TestTableFill::test_fill_incomplete_table(void) {
    std::ofstream logFile("test/output/test_table_fill.txt", std::ios::trunc);
    logFile << "Starting Ride_Log replay test\n";

    // Clean and recreate output directory
    const std::string outputDir = "test/output/ridedata/";
    cleanAndCreateDir(outputDir);

    // Open Ride_Log.txt
    std::ifstream rideLog("test/data/Ride_Log.txt");
    TEST_ASSERT_TRUE_MESSAGE(rideLog.good(), "Ride_Log.txt could not be opened");

    // Prepare regex for Averaged Entry lines
    std::regex entryRegex(R"(\[(\d+)\]\[E\]\(PTable\): Averaged Entry: watts=([\d\.\-]+), cad=([\d\.\-]+), targetPosition=([\d\.\-]+), \((\d+)\)\((\d+)\))");
    std::string line;
    PTData ptData; // Start with an empty table
    PTHelpers helpers;
    int entryCount = 0;
    int lineNumber = 0;

    while (std::getline(rideLog, line)) {
        lineNumber++;
        std::smatch match;
        if (std::regex_search(line, match, entryRegex)) {
            // Extract values (timestamp still extracted but not used for filename)
            std::string timestamp = match[1];
            float watts = std::stof(match[2]);
            float cad = std::stof(match[3]);
            float targetPosition = std::stof(match[4]);
            int cadIndex = std::stoi(match[5]);
            int wattIndex = std::stoi(match[6]);

            // Log extraction
            logFile << "Entry " << entryCount << ": ts=" << timestamp << ", watts=" << watts << ", cad=" << cad << ", targetPosition=" << targetPosition << ", cadIndex=" << cadIndex << ", wattIndex=" << wattIndex << "\n";

            // Call enterData
            ptIndex idx;
            idx.cadIndex = cadIndex;
            idx.wattIndex = wattIndex;
            helpers.enterData(ptData, idx, static_cast<int>(targetPosition));

            // Save PTData to .ptab file named with line number
            std::string outFile = outputDir + std::to_string(lineNumber) + ".ptab";
            savePTDataToCSV(ptData, outFile, true); // true = skipHeatmap

            entryCount++;
        }
    }


    rideLog.close();
    createPowerTableHeatmap("test/output/ridedata/3028648.ptab", "test/output/heatmap_slider.html", false, true);
    logFile << "Processed " << entryCount << " Averaged Entry lines.\n";
    logFile.close();

    // Check that at least one .ptab file was created
    // Count .ptab files in outputDir
    int ptabCount = 0;
#ifdef _WIN32
    struct _finddata_t fileinfo;
    std::string pattern = outputDir + "*.ptab";
    intptr_t hFile = _findfirst(pattern.c_str(), &fileinfo);
    if (hFile != -1) {
        do {
            ptabCount++;
        } while (_findnext(hFile, &fileinfo) == 0);
        _findclose(hFile);
    }
#else
    DIR* dir = opendir(outputDir.c_str());
    if (dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            if (filename.size() > 5 && filename.substr(filename.size() - 5) == ".ptab") {
                ptabCount++;
            }
        }
        closedir(dir);
    }
#endif
    TEST_ASSERT_TRUE_MESSAGE(ptabCount > 0, "No .ptab files were created in output directory");
}
