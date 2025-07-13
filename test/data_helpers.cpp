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
#include <io.h>      // For _findfirst, _findnext, _findclose
#include <direct.h>  // For _mkdir, _rmdir
#include <algorithm> // For std::sort

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

// Function to create a heatmap visualization of the power table data
static void createPowerTableHeatmap(const std::string& inputFilePath, const std::string& outputFilePath, bool skip = false, bool addTimeSlider = false) {
    if (skip) return;
    PTData ptData;
    // Load the power table data
    loadCSVToPTData(inputFilePath, ptData);

    // If addTimeSlider is true, gather all .ptab files in ridedata and prepare their data
    std::vector<std::string> ptabFiles;
    if (addTimeSlider) {
        // Windows directory scan for .ptab files
        struct _finddata_t fileinfo;
        std::string dirPath = "test/output/ridedata/";
        std::string pattern = dirPath + "*.ptab";
        intptr_t hFile = _findfirst(pattern.c_str(), &fileinfo);
        if (hFile != -1) {
            do {
                ptabFiles.push_back(dirPath + fileinfo.name);
            } while (_findnext(hFile, &fileinfo) == 0);
            _findclose(hFile);
        }
        // Sort files by timestamp in filename (assuming numeric)
        std::sort(ptabFiles.begin(), ptabFiles.end(), [](const std::string& a, const std::string& b) {
            // Extract numeric part from filename
            auto getNum = [](const std::string& path) {
                size_t lastSlash = path.find_last_of("/\\");
                size_t dot = path.find_last_of('.');
                std::string num = path.substr(lastSlash + 1, dot - lastSlash - 1);
                return std::stoll(num);
            };
            return getNum(a) < getNum(b);
        });
    }
    // If addTimeSlider is true, we will later gather all .ptab files in ridedata and embed their data in the HTML.
    // Find min and max values for color mapping
    int16_t minValue = INT16_MAX;
    int16_t maxValue = INT16_MIN;
    for (int row = 0; row < POWERTABLE_CAD_SIZE; row++) {
        for (int col = 0; col < POWERTABLE_WATT_SIZE; col++) {
            int16_t val = ptData.tableRow[row].tableEntry[col].targetPosition;
            if (val != INT16_MIN) { // Skip empty cells
                minValue = std::min(minValue, val);
                maxValue = std::max(maxValue, val);
            }
        }
    }

    // Create an HTML file with a colored table
    std::ofstream htmlFile(outputFilePath);
    if (!htmlFile.is_open()) {
        printf("Failed to create output file: %s\n", outputFilePath.c_str());
        return;
    }

    // HTML header
    htmlFile << "<!DOCTYPE html>\n";
    htmlFile << "<html>\n<head>\n";
    htmlFile << "<title>Power Table Heatmap</title>\n";
    htmlFile << "<style>\n";
    htmlFile << "  body { font-family: Arial, sans-serif; margin: 20px; }\n";
    htmlFile << "  table { border-collapse: collapse; margin: 0 auto; }\n";
    htmlFile << "  td { width: 40px; height: 30px; text-align: center; border: 1px solid #ddd; }\n";
    htmlFile << "  .legend-container { display: flex; flex-direction: column; align-items: center; margin-top: 40px; }\n";
    htmlFile << "  .legend { width: 300px; height: 20px; margin-bottom: 10px; }\n";
    htmlFile << "  .legend-labels { display: flex; flex-direction: row; justify-content: space-between; width: 300px; }\n";
    htmlFile << "  .empty-cell { background-color: white; }\n";
    htmlFile << "  .header-cell { font-weight: bold; background-color: #f2f2f2; }\n";
    htmlFile << "  #timeSliderContainer { width: 80%; margin: 30px auto 0 auto; display: flex; flex-direction: column; align-items: center; }\n";
    htmlFile << "  #timeSlider { width: 100%; }\n";
    htmlFile << "</style>\n";
    htmlFile << "</head>\n<body>\n";

    // Table title
    htmlFile << "<h2>Power Table Heatmap</h2>\n";

    // If addTimeSlider, embed all .ptab data as a JS array
    if (addTimeSlider && !ptabFiles.empty()) {
        htmlFile << "<script>\n";
        htmlFile << "// Embedded .ptab data for all time indices\n";
        htmlFile << "const ptabDataArray = [\n";
        for (size_t i = 0; i < ptabFiles.size(); ++i) {
            // Load each .ptab file as a 2D array of numbers (skip header)
            std::ifstream f(ptabFiles[i]);
            std::string line;
            std::getline(f, line); // skip header
            htmlFile << "  [\n";
            while (std::getline(f, line)) {
                htmlFile << "    [";
                std::istringstream ss(line);
                std::string cell;
                bool first = true;
                std::getline(ss, cell, ','); // skip row label
                while (std::getline(ss, cell, ',')) {
                    if (!first) htmlFile << ", ";
                    if (!cell.empty()) htmlFile << cell;
                    else htmlFile << "null";
                    first = false;
                }
                htmlFile << "],\n";
            }
            htmlFile << "  ]";
            if (i < ptabFiles.size() - 1) htmlFile << ",";
            htmlFile << "\n";
        }
        htmlFile << "];\n";
        // Also embed the timestamps for slider labels
        htmlFile << "const ptabTimestamps = [";
        for (size_t i = 0; i < ptabFiles.size(); ++i) {
            size_t lastSlash = ptabFiles[i].find_last_of("/\\");
            size_t dot = ptabFiles[i].find_last_of('.');
            std::string ts = ptabFiles[i].substr(lastSlash + 1, dot - lastSlash - 1);
            htmlFile << '"' << ts << '"';
            if (i < ptabFiles.size() - 1) htmlFile << ", ";
        }
        htmlFile << "];\n";
        htmlFile << "</script>\n";
    }
    
    // Function to map value to color (blue to red gradient)
    auto valueToColor = [minValue, maxValue](int16_t value) -> std::string {
        if (value == INT16_MIN) {
            return std::string("white"); // White for empty cells
        }
        
        // Normalize value to [0, 1]
        float normalized = static_cast<float>(value - minValue) / (maxValue - minValue);
        
        // Map to blue (0) -> purple (0.5) -> red (1)
        int r = std::min(255, static_cast<int>(255 * normalized));
        int b = std::min(255, static_cast<int>(255 * (1.0f - normalized)));
        int g = 0; // Keep green at 0 for more vivid colors
        
        char colorHex[8];
        sprintf(colorHex, "#%02X%02X%02X", r, g, b);
        return std::string(colorHex);
    };
    

    if (addTimeSlider && !ptabFiles.empty()) {
        // Add slider UI
        htmlFile << "<div id=\"timeSliderContainer\">\n";
        htmlFile << "  <label for=\"timeSlider\">Time Index: <span id=\"timeSliderValue\"></span></label>\n";
        htmlFile << "  <input type=\"range\" id=\"timeSlider\" min=\"0\" max=\"" << (ptabFiles.size() - 1) << "\" value=\"0\" step=\"1\">\n";
        htmlFile << "</div>\n";
        // Add table container for dynamic update
        htmlFile << "<div id=\"heatmapTableContainer\"></div>\n";
    } else {
        // Static table as before
        htmlFile << "<table>\n";
        // Table header row with watt values
        htmlFile << "  <tr>\n    <td class=\"header-cell\">CAD/WATTS</td>\n"; // Corner cell
        for (int col = 0; col < POWERTABLE_WATT_SIZE; col++) {
            htmlFile << "    <td class=\"header-cell\">" << col * POWERTABLE_WATT_INCREMENT << "W</td>\n";
        }
        htmlFile << "  </tr>\n";
        // Table data
        for (int row = 0; row < POWERTABLE_CAD_SIZE; row++) {
            htmlFile << "  <tr>\n";
            // Row header with cadence
            htmlFile << "    <td class=\"header-cell\">" << (MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT) << "RPM</td>\n";
            // Data cells
            for (int col = 0; col < POWERTABLE_WATT_SIZE; col++) {
                int16_t value = ptData.tableRow[row].tableEntry[col].targetPosition;
                std::string cellColor = valueToColor(value);
                htmlFile << "    <td style=\"background-color: " << cellColor << ";\"";
                if (value == INT16_MIN) {
                    htmlFile << " class=\"empty-cell\"";
                }
                htmlFile << ">";
                if (value != INT16_MIN) {
                    htmlFile << value;
                }
                htmlFile << "</td>\n";
            }
            htmlFile << "  </tr>\n";
        }
        htmlFile << "</table>\n";
    }
    
    // Add horizontal color legend
    htmlFile << "<div class=\"legend-container\">\n";
    htmlFile << "  <div class=\"legend\" style=\"background: linear-gradient(to right, blue, purple, red);\"></div>\n";
    htmlFile << "  <div class=\"legend-labels\">\n";
    htmlFile << "    <div>" << minValue << " (Min)</div>\n";
    htmlFile << "    <div>" << "Resistance" << "</div>\n";
    htmlFile << "    <div>" << maxValue << " (Max)</div>\n";
    htmlFile << "  </div>\n";
    htmlFile << "</div>\n";


    // Add Chart.js library
    htmlFile << "<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>\n";

    // Add canvas for the chart with increased height
    htmlFile << "<div style=\"width: 80%; height: 600px; margin: 40px auto;\">\n";
    htmlFile << "  <canvas id=\"resistanceWattChart\"></canvas>\n";
    htmlFile << "</div>\n";

    // Add Y-axis range control HTML *before* the script that uses it
    htmlFile << "<div style=\"width: 80%; margin: 20px auto; display: flex; flex-direction: column; align-items: center;\">\n";
    htmlFile << "  <label for=\"yAxisRange\" style=\"margin-bottom: 5px;\">Adjust Y-axis maximum value: <span id=\"yAxisRangeValue\">" << maxValue << "</span></label>\n";
    htmlFile << "  <input type=\"range\" id=\"yAxisRange\" min=\"" << minValue + 50 << "\" max=\"" << maxValue + 100 << "\" step=\"10\" value=\"" << maxValue << "\" style=\"width: 50%;\">\n";
    htmlFile << "</div>\n";

    // Start script for chart and slider logic
    htmlFile << "<script>\n";
    htmlFile << "  const ctx = document.getElementById('resistanceWattChart');\n\n";

    // If addTimeSlider, add logic to update chart on slider move
    if (addTimeSlider && !ptabFiles.empty()) {
        htmlFile << "  const timeSlider = document.getElementById('timeSlider');\n";
        htmlFile << "  const timeSliderValue = document.getElementById('timeSliderValue');\n";
        htmlFile << "  function getDatasetFromPTab(ptab) {\n";
        htmlFile << "    const datasets = [];\n";
        htmlFile << "    for (let row = 0; row < ptab.length; row++) {\n";
        htmlFile << "      const hue = (row * 360 / ptab.length) % 360;\n";
        htmlFile << "      const lineColor = `hsl(${hue}, 70%, 50%)`;\n";
        htmlFile << "      const cadence = " << MINIMUM_TABLE_CAD << " + row * " << POWERTABLE_CAD_INCREMENT << ";\n";
        htmlFile << "      const data = [];\n";
        htmlFile << "      for (let col = 0; col < ptab[row].length; col++) {\n";
        htmlFile << "        if (ptab[row][col] !== null) {\n";
        htmlFile << "          data.push({x: col * " << POWERTABLE_WATT_INCREMENT << ", y: ptab[row][col]});\n";
        htmlFile << "        }\n";
        htmlFile << "      }\n";
        htmlFile << "      datasets.push({label: `${cadence} RPM`, data, borderColor: lineColor, backgroundColor: lineColor, tension: 0.3, fill: false});\n";
        htmlFile << "    }\n";
        htmlFile << "    return datasets;\n";
        htmlFile << "  }\n";
        htmlFile << "  let chart = null;\n";
        htmlFile << "  function updateChart(idx) {\n";
        htmlFile << "    const datasets = getDatasetFromPTab(ptabDataArray[idx]);\n";
        htmlFile << "    if (chart) { chart.data.datasets = datasets; chart.update('none'); }\n";
        htmlFile << "    else {\n";
        htmlFile << "      chart = new Chart(ctx, { type: 'line', data: { datasets }, options: { responsive: true, maintainAspectRatio: false, plugins: { title: { display: true, text: 'Resistance vs. Watts by Cadence', font: { size: 18 } }, legend: { position: 'bottom', labels: { usePointStyle: true } } }, scales: { x: { type: 'linear', title: { display: true, text: 'Watts' }, min: 0, max: " << (POWERTABLE_WATT_SIZE * POWERTABLE_WATT_INCREMENT) << " }, y: { title: { display: true, text: 'Resistance' }, min: " << minValue << ", max: " << maxValue << " } } } }); }\n";
        htmlFile << "    timeSliderValue.textContent = ptabTimestamps[idx];\n";
        htmlFile << "  }\n";
        htmlFile << "  timeSlider.addEventListener('input', function() { updateChart(this.value); });\n";
        htmlFile << "  updateChart(0);\n";
    } else {
        // Prepare datasets (one for each cadence) as before
        htmlFile << "  const datasets = [\n";
        for (int row = 0; row < POWERTABLE_CAD_SIZE; row++) {
            int hue = (row * 360 / POWERTABLE_CAD_SIZE) % 360;
            std::string lineColor = "hsl(" + std::to_string(hue) + ", 70%, 50%)";
            int cadence = MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT;
            htmlFile << "    {\n";
            htmlFile << "      label: '" << cadence << " RPM',\n";
            htmlFile << "      data: [";
            bool firstPoint = true;
            for (int col = 0; col < POWERTABLE_WATT_SIZE; col++) {
                int16_t value = ptData.tableRow[row].tableEntry[col].targetPosition;
                if (value != INT16_MIN) {
                    if (!firstPoint) htmlFile << ", ";
                    htmlFile << "{x: " << col * POWERTABLE_WATT_INCREMENT << ", y: " << value << "}";
                    firstPoint = false;
                }
            }
            htmlFile << "],\n";
            htmlFile << "      borderColor: '" << lineColor << "',\n";
            htmlFile << "      backgroundColor: '" << lineColor << "',\n";
            htmlFile << "      tension: 0.3,\n";
            htmlFile << "      fill: false\n";
            htmlFile << "    }";
            if (row < POWERTABLE_CAD_SIZE - 1) {
                htmlFile << ",";
            }
            htmlFile << "\n";
        }
        htmlFile << "  ];\n\n";
        htmlFile << "  let chart = new Chart(ctx, {\n";
        htmlFile << "    type: 'line',\n";
        htmlFile << "    data: { datasets },\n";
        htmlFile << "    options: {\n";
        htmlFile << "      responsive: true,\n";
        htmlFile << "      maintainAspectRatio: false,\n";
        htmlFile << "      plugins: {\n";
        htmlFile << "        title: {\n";
        htmlFile << "          display: true,\n";
        htmlFile << "          text: 'Resistance vs. Watts by Cadence',\n";
        htmlFile << "          font: { size: 18 }\n";
        htmlFile << "        },\n";
        htmlFile << "        legend: {\n";
        htmlFile << "          position: 'bottom',\n";
        htmlFile << "          labels: { usePointStyle: true }\n";
        htmlFile << "        }\n";
        htmlFile << "      },\n";
        htmlFile << "      scales: {\n";
        htmlFile << "        x: {\n";
        htmlFile << "          type: 'linear',\n";
        htmlFile << "          title: {\n";
        htmlFile << "            display: true,\n";
        htmlFile << "            text: 'Watts'\n";
        htmlFile << "          },\n";
        htmlFile << "          min: 0,\n";
        htmlFile << "          max: " << (POWERTABLE_WATT_SIZE * POWERTABLE_WATT_INCREMENT) << "\n";
        htmlFile << "        },\n";
        htmlFile << "        y: {\n";
        htmlFile << "          title: {\n";
        htmlFile << "            display: true,\n";
        htmlFile << "            text: 'Resistance'\n";
        htmlFile << "          },\n";
        htmlFile << "          min: " << minValue << ",\n";
        htmlFile << "          max: " << maxValue << "\n";
        htmlFile << "        }\n";    htmlFile << "      }\n";
        htmlFile << "    }\n";
        htmlFile << "  });\n";
    }

    // Add event listener for range input (y-axis)
    htmlFile << "  const yAxisRange = document.getElementById('yAxisRange');\n";
    htmlFile << "  const yAxisRangeValue = document.getElementById('yAxisRangeValue');\n";
    htmlFile << "  function updateYAxisRange() {\n";
    htmlFile << "    const newMax = parseInt(yAxisRange.value);\n";
    htmlFile << "    yAxisRangeValue.textContent = newMax;\n";
    htmlFile << "    if (chart) { chart.options.scales.y.max = newMax; chart.update('none'); }\n";
    htmlFile << "  }\n\n";
    htmlFile << "  yAxisRange.addEventListener('input', updateYAxisRange);\n";
    htmlFile << "</script>\n";

    // HTML footer
    htmlFile << "</body>\n</html>\n";
    
    htmlFile.close();
    printf("Heatmap visualization saved to: %s\n", outputFilePath.c_str());
}

// Helper function to save PTData to CSV file
static void savePTDataToCSV(const PTData& ptData, const std::string& filePath, bool skipHeatmap = false) {
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
    
    // Generate heatmap visualization in the same folder
    // Create the heatmap output path by replacing .ptab extension with .html
    if (!skipHeatmap) {
        std::string heatmapPath = filePath;
        size_t extPos = heatmapPath.find_last_of('.');
        if (extPos != std::string::npos) {
            heatmapPath.replace(extPos, heatmapPath.length() - extPos, ".html");
        } else {
            heatmapPath += ".html";
        }
        // Create the heatmap using the same data
        createPowerTableHeatmap(filePath, heatmapPath);
        printf("Heatmap visualization automatically created at: %s\n", heatmapPath.c_str());
    }
}

