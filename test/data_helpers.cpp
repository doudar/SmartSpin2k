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
#include <algorithm> // For std::sort

#ifdef _WIN32
  #include <io.h>      // For _findfirst, _findnext, _findclose
  #include <direct.h>  // For _mkdir, _rmdir
#else
  #include <dirent.h>  // For opendir, readdir, closedir
  #include <sys/stat.h> // For mkdir
  #include <unistd.h>  // For rmdir, unlink
#endif

// Helper function to load CSV data
static void loadCSVToPTData(const std::string& filePath, PTData& ptData) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        printf("Failed to open file: %s\n", filePath.c_str());
        return;
    }

    std::string line;
    int rowIndex = 0;

    // Skip metadata lines (starting with #) and the header line
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue; // Skip metadata lines
        }
        if (line.find("Cadence/Power") != std::string::npos) {
            break; // Found and skip the header line
        }
    }

    while (std::getline(file, line) && rowIndex < POWERTABLE_CAD_SIZE) {
        std::istringstream lineStream(line);
        std::string cell;
        int colIndex = 0;

        // Skip the first column (Cadence/Power labels)
        std::getline(lineStream, cell, ',');

        while (std::getline(lineStream, cell, ',') && colIndex < POWERTABLE_WATT_SIZE) {
            if (!cell.empty()) {
                ptData.tableRow[rowIndex].tableEntry[colIndex].targetPosition = std::stoi(cell);
                ptData.tableRow[rowIndex].tableEntry[colIndex].readings = 5;  // Assign weight to loaded data
            } else {
                ptData.tableRow[rowIndex].tableEntry[colIndex].targetPosition = INT16_MIN;
                ptData.tableRow[rowIndex].tableEntry[colIndex].readings = 0;
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
        // Directory scan for .ptab files
        std::string dirPath = "test/output/ridedata/";
#ifdef _WIN32
        struct _finddata_t fileinfo;
        std::string pattern = dirPath + "*.ptab";
        intptr_t hFile = _findfirst(pattern.c_str(), &fileinfo);
        if (hFile != -1) {
            do {
                ptabFiles.push_back(dirPath + fileinfo.name);
            } while (_findnext(hFile, &fileinfo) == 0);
            _findclose(hFile);
        }
#else
        DIR* dir = opendir(dirPath.c_str());
        if (dir != nullptr) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string filename = entry->d_name;
                if (filename.size() > 5 && filename.substr(filename.size() - 5) == ".ptab") {
                    ptabFiles.push_back(dirPath + filename);
                }
            }
            closedir(dir);
        }
#endif
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
    
    if (addTimeSlider && !ptabFiles.empty()) {
        // Add slider UI
        htmlFile << "<div id=\"timeSliderContainer\">\n";
        htmlFile << "  <label for=\"timeSlider\">Time Index: <span id=\"timeSliderValue\"></span></label>\n";
        htmlFile << "  <input type=\"range\" id=\"timeSlider\" min=\"0\" max=\"" << (ptabFiles.size() - 1) << "\" value=\"0\" step=\"1\">\n";
        htmlFile << "</div>\n";
        // Add dynamic table container above the chart
        htmlFile << "<div id=\"dynamicTableContainer\"></div>\n";
    } else {
        // Static (no slider) case: use the same JS-based rendering as the slider case, but with a single data array and no slider
        htmlFile << "<div id=\"dynamicTableContainer\"></div>\n";
        htmlFile << "<script>\n";
        htmlFile << "const ptabDataArray = [\n  [\n";
        // Output the ptData as a single 2D array
        for (int row = 0; row < POWERTABLE_CAD_SIZE; row++) {
            htmlFile << "    [";
            for (int col = 0; col < POWERTABLE_WATT_SIZE; col++) {
                if (col > 0) htmlFile << ", ";
                int16_t value = ptData.tableRow[row].tableEntry[col].targetPosition;
                if (value != INT16_MIN) htmlFile << value;
                else htmlFile << "null";
            }
            htmlFile << "]";
            if (row < POWERTABLE_CAD_SIZE - 1) htmlFile << ",";
            htmlFile << "\n";
        }
        htmlFile << "  ]\n];\n";
        htmlFile << "const ptabTimestamps = ['static'];\n";
        htmlFile << "</script>\n";
    }
    
    // Add horizontal color legend
    htmlFile << "<div class=\"legend-container\">\n";
    htmlFile << "  <div class=\"legend\" style=\"background: linear-gradient(to right, blue, purple, red);\"></div>\n";
    htmlFile << "  <div class=\"legend-labels\">\n";
    htmlFile << "    <div id=\"legendMin\">" << minValue << " (Min)</div>\n";
    htmlFile << "    <div>" << "Resistance" << "</div>\n";
    htmlFile << "    <div id=\"legendMax\">" << maxValue << " (Max)</div>\n";
    htmlFile << "  </div>\n";
    htmlFile << "</div>\n";


    // Add Chart.js library
    htmlFile << "<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>\n";

    // Add canvas for the chart with increased height
    htmlFile << "<div style=\"width: 80%; height: 600px; margin: 40px auto;\">\n";
    htmlFile << "  <canvas id=\"resistanceWattChart\"></canvas>\n";
    htmlFile << "</div>\n";

    // Add Y-axis range control HTML *before* the script that uses it
    htmlFile << "<div style=\"width: 80%; margin: 20px auto; display: flex; flex-direction: column; align-items: center; gap: 8px;\">\n";
    htmlFile << "  <label for=\"yAxisRange\">Adjust Y-axis maximum value: <span id=\"yAxisRangeValue\">" << maxValue << "</span></label>\n";
    htmlFile << "  <input type=\"range\" id=\"yAxisRange\" min=\"" << minValue + 50 << "\" max=\"" << maxValue + 100 << "\" step=\"10\" value=\"" << maxValue << "\" style=\"width: 50%;\">\n";
    htmlFile << "  <label style=\"display: flex; align-items: center; gap: 6px; font-size: 0.9rem;\">\n";
    htmlFile << "    <input type=\"checkbox\" id=\"yAxisLock\"> Lock Y-axis slider\n";
    htmlFile << "  </label>\n";
    htmlFile << "</div>\n";

    // Start script for chart and slider logic
    htmlFile << "<script>\n";
    htmlFile << "  const ctx = document.getElementById('resistanceWattChart');\n";
    htmlFile << "  const yAxisRange = document.getElementById('yAxisRange');\n";
    htmlFile << "  const yAxisRangeValue = document.getElementById('yAxisRangeValue');\n";
    htmlFile << "  const yAxisLock = document.getElementById('yAxisLock');\n";
    htmlFile << "  let yAxisLocked = false;\n\n";

    // If addTimeSlider, add logic to update chart and table on slider move
    if (addTimeSlider && !ptabFiles.empty()) {
        htmlFile << "  const timeSlider = document.getElementById('timeSlider');\n";
        htmlFile << "  const timeSliderValue = document.getElementById('timeSliderValue');\n";
        htmlFile << "  const dynamicTableContainer = document.getElementById('dynamicTableContainer');\n";
        // Color mapping function (same as C++ valueToColor)
        htmlFile << "  function valueToColor(value, minValue, maxValue) {\n";
        htmlFile << "    if (value === null) return 'white';\n";
        htmlFile << "    let normalized = (value - minValue) / (maxValue - minValue);\n";
        htmlFile << "    let r = Math.min(255, Math.round(255 * normalized));\n";
        htmlFile << "    let b = Math.min(255, Math.round(255 * (1.0 - normalized)));\n";
        htmlFile << "    let g = 0;\n";
        htmlFile << "    return `#${r.toString(16).padStart(2,'0').toUpperCase()}${g.toString(16).padStart(2,'0').toUpperCase()}${b.toString(16).padStart(2,'0').toUpperCase()}`;\n";
        htmlFile << "  }\n";
        // Render the table for a given index
        htmlFile << "  function renderTable(idx, minValue, maxValue) {\n";
        htmlFile << "    const ptab = ptabDataArray[idx];\n";
        htmlFile << "    let html = '<table><tr><td class=\"header-cell\">CAD/WATTS</td>';\n";
        htmlFile << "    for (let col = 0; col < ptab[0].length; col++) { html += '<td class=\"header-cell\">' + (col * " << POWERTABLE_WATT_INCREMENT << ") + 'W</td>'; }\n";
        htmlFile << "    html += '</tr>';\n";
        htmlFile << "    for (let row = 0; row < ptab.length; row++) {\n";
        htmlFile << "      html += '<tr><td class=\"header-cell\">' + (" << MINIMUM_TABLE_CAD << " + row * " << POWERTABLE_CAD_INCREMENT << ") + 'RPM</td>';\n";
        htmlFile << "      for (let col = 0; col < ptab[row].length; col++) {\n";
        htmlFile << "        let value = ptab[row][col];\n";
        htmlFile << "        let color = valueToColor(value, minValue, maxValue);\n";
        htmlFile << "        html += '<td style=\"background-color: ' + color + '\"' + (value === null ? ' class=\"empty-cell\"' : '') + '>' + (value !== null ? value : '') + '</td>';\n";
        htmlFile << "      }\n";
        htmlFile << "      html += '</tr>';\n";
        htmlFile << "    }\n";
        htmlFile << "    html += '</table>';\n";
        htmlFile << "    dynamicTableContainer.innerHTML = html;\n";
        htmlFile << "  }\n";
        // Chart dataset logic (unchanged)
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
        htmlFile << "  function updateChartAndTable(idx) {\n";
        htmlFile << "    // Find min/max for this table\n";
        htmlFile << "    let minValue = Infinity, maxValue = -Infinity;\n";
        htmlFile << "    const ptab = ptabDataArray[idx];\n";
        htmlFile << "    for (let row = 0; row < ptab.length; row++) {\n";
        htmlFile << "      for (let col = 0; col < ptab[row].length; col++) {\n";
        htmlFile << "        let v = ptab[row][col];\n";
        htmlFile << "        if (v !== null) { minValue = Math.min(minValue, v); maxValue = Math.max(maxValue, v); }\n";
        htmlFile << "      }\n";
        htmlFile << "    }\n";
        htmlFile << "    // Update legend\n";
        htmlFile << "    document.getElementById('legendMin').textContent = `${minValue} (Min)`;\n";
        htmlFile << "    document.getElementById('legendMax').textContent = `${maxValue} (Max)`;\n";
        htmlFile << "    renderTable(idx, minValue, maxValue);\n";
        htmlFile << "    const datasets = getDatasetFromPTab(ptab);\n";
        htmlFile << "    if (chart) { chart.data.datasets = datasets; chart.update('none'); }\n";
        htmlFile << "    else {\n";
        htmlFile << "      chart = new Chart(ctx, { type: 'line', data: { datasets }, options: { responsive: true, maintainAspectRatio: false, plugins: { title: { display: true, text: 'Resistance vs. Watts by Cadence', font: { size: 18 } }, legend: { position: 'bottom', labels: { usePointStyle: true } } }, scales: { x: { type: 'linear', title: { display: true, text: 'Watts' }, min: 0, max: " << (POWERTABLE_WATT_SIZE * POWERTABLE_WATT_INCREMENT) << " }, y: { title: { display: true, text: 'Resistance' }, min: 0, max: maxValue } } } }); }\n";
        htmlFile << "    timeSliderValue.textContent = ptabTimestamps[idx];\n";
        htmlFile << "    const selectedMax = yAxisLocked ? parseInt(yAxisRange.value) : maxValue;\n";
        htmlFile << "    if (!yAxisLocked) {\n";
        htmlFile << "      yAxisRange.min = 50;\n";
        htmlFile << "      yAxisRange.max = maxValue + 100;\n";
        htmlFile << "      yAxisRange.value = maxValue;\n";
        htmlFile << "    }\n";
        htmlFile << "    yAxisRangeValue.textContent = selectedMax;\n";
        htmlFile << "    if (chart) { chart.options.scales.y.max = selectedMax; chart.options.scales.y.min = 0; chart.update('none'); }\n";
        htmlFile << "  }\n";
        htmlFile << "  timeSlider.addEventListener('input', function() { updateChartAndTable(this.value); });\n";
        htmlFile << "  updateChartAndTable(0);\n";
    } else {
        // Static (no slider) case: render table and chart for the single data array
        htmlFile << "  const dynamicTableContainer = document.getElementById('dynamicTableContainer');\n";
        htmlFile << "  function valueToColor(value, minValue, maxValue) {\n";
        htmlFile << "    if (value === null) return 'white';\n";
        htmlFile << "    let normalized = (value - minValue) / (maxValue - minValue);\n";
        htmlFile << "    let r = Math.min(255, Math.round(255 * normalized));\n";
        htmlFile << "    let b = Math.min(255, Math.round(255 * (1.0 - normalized))); let g = 0;\n";
        htmlFile << "    return `#${r.toString(16).padStart(2,'0').toUpperCase()}${g.toString(16).padStart(2,'0').toUpperCase()}${b.toString(16).padStart(2,'0').toUpperCase()}`;\n";
        htmlFile << "  }\n";
        htmlFile << "  function renderTable(idx, minValue, maxValue) {\n";
        htmlFile << "    const ptab = ptabDataArray[idx];\n";
        htmlFile << "    let html = '<table><tr><td class=\"header-cell\">CAD/WATTS</td>';\n";
        htmlFile << "    for (let col = 0; col < ptab[0].length; col++) { html += '<td class=\"header-cell\">' + (col * " << POWERTABLE_WATT_INCREMENT << ") + 'W</td>'; }\n";
        htmlFile << "    html += '</tr>';\n";
        htmlFile << "    for (let row = 0; row < ptab.length; row++) {\n";
        htmlFile << "      html += '<tr><td class=\"header-cell\">' + (" << MINIMUM_TABLE_CAD << " + row * " << POWERTABLE_CAD_INCREMENT << ") + 'RPM</td>';\n";
        htmlFile << "      for (let col = 0; col < ptab[row].length; col++) {\n";
        htmlFile << "        let value = ptab[row][col];\n";
        htmlFile << "        let color = valueToColor(value, minValue, maxValue);\n";
        htmlFile << "        html += '<td style=\"background-color: ' + color + '\"' + (value === null ? ' class=\"empty-cell\"' : '') + '>' + (value !== null ? value : '') + '</td>';\n";
        htmlFile << "      }\n";
        htmlFile << "      html += '</tr>';\n";
        htmlFile << "    }\n";
        htmlFile << "    html += '</table>';\n";
        htmlFile << "    dynamicTableContainer.innerHTML = html;\n";
        htmlFile << "  }\n";
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
        htmlFile << "  function updateChartAndTable(idx) {\n";
        htmlFile << "    // Find min/max for this table\n";
        htmlFile << "    let minValue = Infinity, maxValue = -Infinity;\n";
        htmlFile << "    const ptab = ptabDataArray[idx];\n";
        htmlFile << "    for (let row = 0; row < ptab.length; row++) {\n";
        htmlFile << "      for (let col = 0; col < ptab[row].length; col++) {\n";
        htmlFile << "        let v = ptab[row][col];\n";
        htmlFile << "        if (v !== null) { minValue = Math.min(minValue, v); maxValue = Math.max(maxValue, v); }\n";
        htmlFile << "      }\n";
        htmlFile << "    }\n";
        htmlFile << "    // Update legend\n";
        htmlFile << "    document.getElementById('legendMin').textContent = `${minValue} (Min)`;\n";
        htmlFile << "    document.getElementById('legendMax').textContent = `${maxValue} (Max)`;\n";
        htmlFile << "    renderTable(idx, minValue, maxValue);\n";
        htmlFile << "    const datasets = getDatasetFromPTab(ptab);\n";
        htmlFile << "    if (chart) { chart.data.datasets = datasets; chart.update('none'); }\n";
        htmlFile << "    else {\n";
        htmlFile << "      chart = new Chart(ctx, { type: 'line', data: { datasets }, options: { responsive: true, maintainAspectRatio: false, plugins: { title: { display: true, text: 'Resistance vs. Watts by Cadence', font: { size: 18 } }, legend: { position: 'bottom', labels: { usePointStyle: true } } }, scales: { x: { type: 'linear', title: { display: true, text: 'Watts' }, min: 0, max: " << (POWERTABLE_WATT_SIZE * POWERTABLE_WATT_INCREMENT) << " }, y: { title: { display: true, text: 'Resistance' }, min: 0, max: maxValue } } } }); }\n";
        htmlFile << "    const selectedMax = yAxisLocked ? parseInt(yAxisRange.value) : maxValue;\n";
        htmlFile << "    if (!yAxisLocked) {\n";
        htmlFile << "      yAxisRange.min = 50;\n";
        htmlFile << "      yAxisRange.max = maxValue + 100;\n";
        htmlFile << "      yAxisRange.value = maxValue;\n";
        htmlFile << "    }\n";
        htmlFile << "    yAxisRangeValue.textContent = selectedMax;\n";
        htmlFile << "    if (chart) { chart.options.scales.y.max = selectedMax; chart.options.scales.y.min = 0; chart.update('none'); }\n";
        htmlFile << "  }\n";
        htmlFile << "  updateChartAndTable(0);\n";
    }

    // Add event listener for range input (y-axis)
    htmlFile << "  function updateYAxisRange() {\n";
    htmlFile << "    if (yAxisLocked) { return; }\n";
    htmlFile << "    const newMax = parseInt(yAxisRange.value);\n";
    htmlFile << "    yAxisRangeValue.textContent = newMax;\n";
    htmlFile << "    if (chart) { chart.options.scales.y.max = newMax; chart.options.scales.y.min = 0; chart.update('none'); }\n";
    htmlFile << "  }\n\n";
    htmlFile << "  yAxisRange.addEventListener('input', updateYAxisRange);\n";
    htmlFile << "  yAxisLock.addEventListener('change', () => {\n";
    htmlFile << "    yAxisLocked = yAxisLock.checked;\n";
    htmlFile << "    yAxisRange.disabled = yAxisLocked;\n";
    htmlFile << "    if (!yAxisLocked) { updateYAxisRange(); }\n";
    htmlFile << "  });\n";
    htmlFile << "</script>\n";

    // HTML footer
    htmlFile << "</body>\n</html>\n";
    
    htmlFile.close();
    printf("Heatmap visualization saved to: %s\n", outputFilePath.c_str());
}

// Helper function to save PTData to CSV file
inline void savePTDataToCSV(const PTData& ptData, const std::string& filePath, bool skipHeatmap = false) {
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
