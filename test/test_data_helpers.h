/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "PowerTable_Helpers.h"

static const char ACTIVE_RIDE_LOG_PATH[] = "test/data/active_ride_log.txt";
static const char ACTIVE_POWER_TABLE_OUTPUT_PATH[] = "test/output/active_power_table.ptab";
static const char ACTIVE_POWER_TABLE_VIEWER_PATH[] = "test/output/active_power_table.svg";
static const char ACTIVE_STATUS_POWER_TABLE_OUTPUT_PATH[] = "test/output/active_status_power_table.ptab";
static const char ACTIVE_STATUS_POWER_TABLE_VIEWER_PATH[] = "test/output/active_status_power_table.svg";
static const char ACTIVE_STATUS_POWER_TABLE_AUDIT_PATH[] = "test/output/active_status_power_table_audit.txt";
static const char ACTIVE_POWER_TABLE_PREDICTION_AUDIT_PATH[] = "test/output/active_power_table_prediction_audit.txt";
static const int STATUS_SETTLED_POSITION_TOLERANCE_STEPS = 50;

struct RideReplaySummary {
  int lines;
  int entries;
  int invalidEntries;
};

struct StatusReplaySummary {
  int lines;
  int statusSamples;
  int invalidSamples;
  int acceptedSamples;
  int highCadenceSamples;
  int rejectedDisconnected;
  int rejectedOutOfRange;
  int rejectedMoving;
};

struct StatusPowerSample {
  int timestamp;
  int watts;
  int cadence;
  int currentPosition;
  int targetPosition;
};

struct PowerTableValidationSummary {
  int measuredCells;
  int trustedRoundTrips;
  int maximumRoundTripError;
  int worstCadence;
  int worstExpectedWatts;
  int worstActualWatts;
};

inline bool replayRideLog(const std::string& filePath, PTData& ptData, RideReplaySummary& summary) {
  summary = {0, 0, 0};
  std::ifstream rideLog(filePath);
  if (!rideLog.is_open()) return false;

  const std::regex entryPattern(
      R"(\[[0-9]+\]\[E\]\(PTable\): Averaged Entry: watts=([0-9.\-]+), cad=([0-9.\-]+), targetPosition=([0-9.\-]+), \(([0-9]+)\)\(([0-9]+)\))");
  PTHelpers helpers;
  std::string line;
  std::smatch match;
  while (std::getline(rideLog, line)) {
    ++summary.lines;
    if (!std::regex_search(line, match, entryPattern)) {
      if (line.find("(PTable): Averaged Entry:") != std::string::npos) ++summary.invalidEntries;
      continue;
    }

    const double watts = std::stod(match[1].str());
    const double cadence = std::stod(match[2].str());
    const double targetPosition = std::stod(match[3].str());
    const int cadenceIndex = std::stoi(match[4].str());
    const int wattIndex = std::stoi(match[5].str());
    const ptIndex calculatedIndex = helpers.calculateIndex(static_cast<int>(watts), static_cast<int>(cadence));
    if (!std::isfinite(watts) || !std::isfinite(cadence) || !std::isfinite(targetPosition) || cadence <= 0.0 || watts < 0.0 ||
        cadenceIndex < 0 || cadenceIndex >= POWERTABLE_CAD_SIZE || wattIndex < 0 || wattIndex >= POWERTABLE_WATT_SIZE ||
        targetPosition <= INT16_MIN || targetPosition > INT16_MAX || calculatedIndex.cadIndex != cadenceIndex || calculatedIndex.wattIndex != wattIndex) {
      ++summary.invalidEntries;
      continue;
    }

    ptIndex index;
    index.cadIndex = cadenceIndex;
    index.wattIndex = wattIndex;
    helpers.enterData(ptData, index, static_cast<int>(targetPosition));
    ++summary.entries;
  }
  return true;
}

inline bool replayActiveRideLog(PTData& ptData, RideReplaySummary& summary) {
  return replayRideLog(ACTIVE_RIDE_LOG_PATH, ptData, summary);
}

inline bool replayStatusLog(const std::string& filePath, PTData& ptData, StatusReplaySummary& summary,
                            std::vector<StatusPowerSample>* acceptedSamples = nullptr) {
  summary = {0, 0, 0, 0, 0, 0, 0, 0};
  std::ifstream rideLog(filePath);
  if (!rideLog.is_open()) return false;

  const std::regex compactStatusPattern(
      R"(\[([0-9]+)\]\[E\]\(Main\): W=(-?[0-9]+) C=(-?[0-9]+) H=-?[0-9]+ G=-?[0-9]+ R=-?[0-9]+ P=(-?[0-9]+)->(-?[0-9]+))");
  const std::regex devicePattern(R"(\[[0-9]+\]\[E\]\(Main\): DEV PM=([0-9]+) CAD=([0-9]+) HRM=[0-9]+)");
  const std::regex legacyStatusPattern(
      R"(\[([0-9]+)\]\[E\]\(Main\): PM Con ([0-9]+), CAD con ([0-9]+), HRM Con [0-9]+, W (-?[0-9]+), Cad (-?[0-9]+), HR -?[0-9]+, Gear -?[0-9]+, Res -?[0-9]+, Current Pos (-?[0-9]+), Target Pos (-?[0-9]+))");
  PTHelpers helpers;
  std::string line;
  std::smatch match;
  bool powerConnected = false;
  bool cadenceConnected = false;
  while (std::getline(rideLog, line)) {
    ++summary.lines;
    if (std::regex_search(line, match, devicePattern)) {
      powerConnected = std::stoi(match[1].str()) != 0;
      cadenceConnected = std::stoi(match[2].str()) != 0;
      continue;
    }

    int timestamp;
    int watts;
    int cadence;
    int currentPosition;
    int targetPosition;
    if (std::regex_search(line, match, compactStatusPattern)) {
      timestamp = std::stoi(match[1].str());
      watts = std::stoi(match[2].str());
      cadence = std::stoi(match[3].str());
      currentPosition = std::stoi(match[4].str());
      targetPosition = std::stoi(match[5].str());
    } else if (std::regex_search(line, match, legacyStatusPattern)) {
      // Preserve replay support for logs captured before DEV became a separate periodic record.
      timestamp = std::stoi(match[1].str());
      powerConnected = std::stoi(match[2].str()) != 0;
      cadenceConnected = std::stoi(match[3].str()) != 0;
      watts = std::stoi(match[4].str());
      cadence = std::stoi(match[5].str());
      currentPosition = std::stoi(match[6].str());
      targetPosition = std::stoi(match[7].str());
    } else {
      if (line.find("(Main): W=") != std::string::npos || line.find("(Main): PM Con ") != std::string::npos) ++summary.invalidSamples;
      continue;
    }

    ++summary.statusSamples;

    if (!powerConnected || !cadenceConnected) {
      ++summary.rejectedDisconnected;
      continue;
    }
    const ptIndex index = helpers.calculateIndex(watts, cadence);
    if (watts <= 10 || watts >= POWERTABLE_WATT_SIZE * POWERTABLE_WATT_INCREMENT ||
        !helpers.cadenceIsWithinTable(cadence) || index.wattIndex < 0 || index.wattIndex >= POWERTABLE_WATT_SIZE) {
      ++summary.rejectedOutOfRange;
      continue;
    }
    if (std::abs(currentPosition - targetPosition) > STATUS_SETTLED_POSITION_TOLERANCE_STEPS) {
      ++summary.rejectedMoving;
      continue;
    }

    helpers.enterData(ptData, index, currentPosition / TABLE_DIVISOR);
    ++summary.acceptedSamples;
    if (index.cadIndex == POWERTABLE_CAD_SIZE - 1) ++summary.highCadenceSamples;
    if (acceptedSamples != nullptr) acceptedSamples->push_back({timestamp, watts, cadence, currentPosition, targetPosition});
  }
  return true;
}

inline bool replayActiveStatusLog(PTData& ptData, StatusReplaySummary& summary) {
  return replayStatusLog(ACTIVE_RIDE_LOG_PATH, ptData, summary);
}

inline bool validatePowerTableSurface(PTData& ptData, PowerTableValidationSummary& summary, std::string& failure) {
  summary = {0, 0, 0, 0, 0, 0};
  PTHelpers helpers;

  for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
    int16_t previousPosition = INT16_MIN;
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      const TableEntry& entry = ptData.tableRow[row].tableEntry[col];
      if (entry.targetPosition == INT16_MIN) continue;
      ++summary.measuredCells;
      if (entry.readings <= 0) {
        failure = "measured table cell has no supporting readings";
        return false;
      }
      if (entry.targetPosition < previousPosition) {
        std::ostringstream message;
        message << "stored table decreased with watts: cadence=" << MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT
                << " watts=" << col * POWERTABLE_WATT_INCREMENT << " previous_position=" << previousPosition
                << " position=" << entry.targetPosition;
        failure = message.str();
        return false;
      }
      previousPosition = entry.targetPosition;
    }
  }
  if (summary.measuredCells == 0) {
    failure = "power table contains no measured cells";
    return false;
  }

  for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
    int16_t previousPosition = INT16_MAX;
    for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
      const TableEntry& entry = ptData.tableRow[row].tableEntry[col];
      if (entry.targetPosition == INT16_MIN) continue;
      if (entry.targetPosition > previousPosition) {
        std::ostringstream message;
        message << "stored table increased with cadence: watts=" << col * POWERTABLE_WATT_INCREMENT
                << " cadence=" << MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT
                << " previous_position=" << previousPosition << " position=" << entry.targetPosition;
        failure = message.str();
        return false;
      }
      previousPosition = entry.targetPosition;
    }
  }

  for (int cadence = 40; cadence <= 130; ++cadence) {
    int32_t previousPosition = INT32_MIN;
    for (int watts = 0; watts <= 1200; ++watts) {
      const int32_t position = helpers.lookup(watts, cadence, ptData);
      if (position == RETURN_ERROR || (previousPosition != INT32_MIN && position < previousPosition)) {
        std::ostringstream message;
        message << "invalid forward surface: cadence=" << cadence << " watts=" << watts
                << " previous_position=" << previousPosition << " position=" << position;
        failure = message.str();
        return false;
      }
      previousPosition = position;
    }
  }

  for (int cadence = 40; cadence <= 130; ++cadence) {
    for (int expectedWatts = 30; expectedWatts <= 900; expectedWatts += 15) {
      double localStepsPerWatt;
      if (!helpers.lookupSlope(expectedWatts, cadence, localStepsPerWatt, ptData)) continue;
      const int32_t position = helpers.lookup(expectedWatts, cadence, ptData);
      const int actualWatts = helpers.lookupWatts(cadence, position, ptData);
      const int error = std::abs(actualWatts - expectedWatts);
      ++summary.trustedRoundTrips;
      if (error > summary.maximumRoundTripError) {
        summary.maximumRoundTripError = error;
        summary.worstCadence = cadence;
        summary.worstExpectedWatts = expectedWatts;
        summary.worstActualWatts = actualWatts;
      }
    }
  }
  if (summary.trustedRoundTrips == 0) {
    failure = "power table produced no locally trustworthy round-trip samples";
    return false;
  }
  if (summary.maximumRoundTripError > POWERTABLE_WATT_INCREMENT) {
    std::ostringstream message;
    message << "trusted forward/reverse error exceeded one watt column: cadence=" << summary.worstCadence
            << " expected=" << summary.worstExpectedWatts << "W actual=" << summary.worstActualWatts
            << "W error=" << summary.maximumRoundTripError << "W";
    failure = message.str();
    return false;
  }

  static const int RESISTANCE_STEP = 200;
  static const int MAX_RESISTANCE = 24000;
  int previousCadenceWatts[MAX_RESISTANCE / RESISTANCE_STEP + 1];
  std::fill(previousCadenceWatts, previousCadenceWatts + MAX_RESISTANCE / RESISTANCE_STEP + 1, -1);
  for (int cadence = 1; cadence <= 130; ++cadence) {
    int previousWatts = -1;
    int resistanceIndex = 0;
    for (int resistance = 0; resistance <= MAX_RESISTANCE; resistance += RESISTANCE_STEP, ++resistanceIndex) {
      const int watts = helpers.lookupWatts(cadence, resistance, ptData);
      if (watts < 0 || watts > 4000 || (previousWatts >= 0 && watts < previousWatts) ||
          (previousCadenceWatts[resistanceIndex] >= 0 && watts < previousCadenceWatts[resistanceIndex])) {
        std::ostringstream message;
        message << "invalid reverse surface: cadence=" << cadence << " resistance=" << resistance
                << " watts=" << watts << " previous_resistance_watts=" << previousWatts
                << " previous_cadence_watts=" << previousCadenceWatts[resistanceIndex];
        failure = message.str();
        return false;
      }
      previousWatts = watts;
      previousCadenceWatts[resistanceIndex] = watts;
    }
  }
  return true;
}

inline bool loadCSVToPTData(const std::string& filePath, PTData& ptData) {
  std::ifstream file(filePath);
  if (!file.is_open()) return false;

  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line[0] != '#' && line.find("Cadence/Power") != std::string::npos) break;
  }

  int row = 0;
  while (row < POWERTABLE_CAD_SIZE && std::getline(file, line)) {
    std::istringstream cells(line);
    std::string cell;
    std::getline(cells, cell, ',');
    int col = 0;
    while (col < POWERTABLE_WATT_SIZE && std::getline(cells, cell, ',')) {
      if (!cell.empty()) {
        ptData.tableRow[row].tableEntry[col].targetPosition = static_cast<int16_t>(std::stoi(cell));
        ptData.tableRow[row].tableEntry[col].readings = 5;
      }
      ++col;
    }
    ++row;
  }
  return row == POWERTABLE_CAD_SIZE;
}

inline bool savePTDataToCSV(const PTData& ptData, const std::string& filePath) {
  std::ofstream file(filePath, std::ios::trunc);
  if (!file.is_open()) return false;

  file << "Cadence/Power";
  for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) file << ',' << col * POWERTABLE_WATT_INCREMENT << 'W';
  file << '\n';
  for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
    file << MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT << "RPM";
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      file << ',';
      const TableEntry& entry = ptData.tableRow[row].tableEntry[col];
      if (entry.targetPosition != INT16_MIN) file << entry.targetPosition;
    }
    file << '\n';
  }
  return file.good();
}

inline bool powerTablePositionsMatch(const PTData& expected, const PTData& actual, std::string& failure) {
  for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      const int16_t expectedPosition = expected.tableRow[row].tableEntry[col].targetPosition;
      const int16_t actualPosition = actual.tableRow[row].tableEntry[col].targetPosition;
      if (expectedPosition == actualPosition) continue;
      std::ostringstream message;
      message << "power-table position changed during CSV round trip: cadence="
              << MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT << " watts=" << col * POWERTABLE_WATT_INCREMENT
              << " expected_position=" << expectedPosition << " actual_position=" << actualPosition;
      failure = message.str();
      return false;
    }
  }
  return true;
}

inline bool savePowerTableViewer(const PTData& ptData, const std::string& filePath,
                                 const std::string& title = "SmartSpin2k active power table",
                                 const std::string& sourceDescription = "Final table replayed from active_ride_log.txt") {
  int minimumPosition = INT16_MAX;
  int maximumPosition = INT16_MIN;
  int measuredCells = 0;
  for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      const int16_t position = ptData.tableRow[row].tableEntry[col].targetPosition;
      if (position == INT16_MIN) continue;
      minimumPosition = std::min(minimumPosition, static_cast<int>(position));
      maximumPosition = std::max(maximumPosition, static_cast<int>(position));
      ++measuredCells;
    }
  }
  if (measuredCells == 0) return false;

  std::ofstream file(filePath, std::ios::trunc);
  if (!file.is_open()) return false;

  static const char* colors[POWERTABLE_CAD_SIZE] = {
      "#2563eb", "#7c3aed", "#db2777", "#e11d48", "#ea580c",
      "#ca8a04", "#16a34a", "#0d9488", "#0891b2", "#4f46e5"};
  const int width = 1500;
  const int height = 1210;
  const int plotLeft = 105;
  const int plotTop = 150;
  const int plotWidth = 1340;
  const int plotHeight = 385;
  const int wattMaximum = (POWERTABLE_WATT_SIZE - 1) * POWERTABLE_WATT_INCREMENT;
  const int positionPadding = std::max(30, (maximumPosition - minimumPosition) / 16);
  const int yMinimum = minimumPosition - positionPadding;
  const int yMaximum = maximumPosition + positionPadding;
  const int heatLeft = 145;
  const int heatTop = 800;
  const int cellWidth = 43;
  const int cellHeight = 34;
  const auto x = [&](int watts) { return plotLeft + static_cast<double>(watts) * plotWidth / wattMaximum; };
  const auto y = [&](int position) { return plotTop + static_cast<double>(yMaximum - position) * plotHeight / (yMaximum - yMinimum); };

  file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width << "\" height=\"" << height
       << "\" viewBox=\"0 0 " << width << ' ' << height << "\" role=\"img\" aria-labelledby=\"title description\">\n"
       << "<title id=\"title\">" << title << "</title>\n"
       << "<desc id=\"description\">Cadence curves and measured-cell heatmap generated from active_ride_log.txt.</desc>\n"
       << R"SVG(<style>
.background{fill:#f7f9fc}.panel{fill:#fff;stroke:#d6dce8}.text{fill:#172033;font-family:system-ui,-apple-system,"Segoe UI",sans-serif}.muted{fill:#667085}.grid{stroke:#e5eaf2;stroke-width:1}.frame{fill:none;stroke:#b9c2d3}.curve{fill:none;stroke-width:2.5;stroke-linecap:round;stroke-linejoin:round}.point{stroke:#fff;stroke-width:1.5}.empty{fill:#eef1f6;stroke:#d9deea}.cell{stroke:#fff;stroke-width:2}
@media(prefers-color-scheme:dark){.background{fill:#10131a}.panel{fill:#181d27;stroke:#343c4d}.text{fill:#eef2ff}.muted{fill:#a2acc0}.grid{stroke:#293142}.frame{stroke:#465066}.point{stroke:#181d27}.empty{fill:#222936;stroke:#343c4d}}
</style>)SVG"
       << "<rect class=\"background\" width=\"" << width << "\" height=\"" << height << "\"/>\n"
       << "<text class=\"text\" x=\"48\" y=\"48\" font-size=\"30\" font-weight=\"650\">" << title << "</text>\n"
       << "<text class=\"muted text\" x=\"48\" y=\"76\" font-size=\"14\">" << sourceDescription << " · "
       << measuredCells << " / " << POWERTABLE_CAD_SIZE * POWERTABLE_WATT_SIZE << " measured cells · positions "
       << minimumPosition << "–" << maximumPosition << "</text>\n"
       << "<rect class=\"panel\" x=\"34\" y=\"92\" width=\"1432\" height=\"574\" rx=\"12\"/>\n"
       << "<text class=\"text\" x=\"55\" y=\"124\" font-size=\"18\" font-weight=\"600\">Resistance position by power and cadence</text>\n";

  for (int watts = 0; watts <= wattMaximum; watts += 90) {
    file << "<line class=\"grid\" x1=\"" << x(watts) << "\" y1=\"" << plotTop << "\" x2=\"" << x(watts) << "\" y2=\"" << plotTop + plotHeight << "\"/>\n"
         << "<text class=\"muted text\" x=\"" << x(watts) << "\" y=\"558\" font-size=\"12\" text-anchor=\"middle\">" << watts << "</text>\n";
  }
  for (int tick = 0; tick <= 6; ++tick) {
    const int position = yMinimum + (yMaximum - yMinimum) * tick / 6;
    file << "<line class=\"grid\" x1=\"" << plotLeft << "\" y1=\"" << y(position) << "\" x2=\"" << plotLeft + plotWidth << "\" y2=\"" << y(position) << "\"/>\n"
         << "<text class=\"muted text\" x=\"94\" y=\"" << y(position) + 4 << "\" font-size=\"12\" text-anchor=\"end\">" << position << "</text>\n";
  }
  file << "<rect class=\"frame\" x=\"" << plotLeft << "\" y=\"" << plotTop << "\" width=\"" << plotWidth << "\" height=\"" << plotHeight << "\"/>\n"
       << "<text class=\"text\" x=\"775\" y=\"585\" font-size=\"13\" text-anchor=\"middle\">Power (watts)</text>\n"
       << "<text class=\"text\" x=\"24\" y=\"342\" font-size=\"13\" text-anchor=\"middle\" transform=\"rotate(-90 24 342)\">Stored position (&#215;10 steps)</text>\n";

  for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
    std::ostringstream points;
    bool first = true;
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      const int16_t position = ptData.tableRow[row].tableEntry[col].targetPosition;
      if (position == INT16_MIN) continue;
      if (!first) points << ' ';
      points << x(col * POWERTABLE_WATT_INCREMENT) << ',' << y(position);
      first = false;
    }
    if (first) continue;
    const int cadence = MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT;
    file << "<polyline class=\"curve\" stroke=\"" << colors[row] << "\" points=\"" << points.str() << "\"><title>" << cadence << " RPM cadence curve</title></polyline>\n";
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      const int16_t position = ptData.tableRow[row].tableEntry[col].targetPosition;
      if (position == INT16_MIN) continue;
      file << "<circle class=\"point\" fill=\"" << colors[row] << "\" cx=\"" << x(col * POWERTABLE_WATT_INCREMENT) << "\" cy=\"" << y(position)
           << "\" r=\"4\"><title>" << cadence << " RPM · " << col * POWERTABLE_WATT_INCREMENT << " W · position " << position
           << " (" << static_cast<int>(std::round(position * TABLE_DIVISOR)) << " steps)</title></circle>\n";
    }
    const int legendX = 135 + (row % 5) * 260;
    const int legendY = 620 + (row / 5) * 26;
    file << "<line x1=\"" << legendX << "\" y1=\"" << legendY - 4 << "\" x2=\"" << legendX + 28 << "\" y2=\"" << legendY - 4
         << "\" stroke=\"" << colors[row] << "\" stroke-width=\"3\"/><text class=\"text\" x=\"" << legendX + 36 << "\" y=\"" << legendY << "\" font-size=\"13\">" << cadence << " RPM</text>\n";
  }

  file << "<rect class=\"panel\" x=\"34\" y=\"682\" width=\"1432\" height=\"500\" rx=\"12\"/>\n"
       << "<text class=\"text\" x=\"55\" y=\"716\" font-size=\"18\" font-weight=\"600\">Measured-cell heatmap</text>\n"
       << "<text class=\"muted text\" x=\"55\" y=\"738\" font-size=\"12\">Cell values are stored positions; blank cells were not learned from the ride.</text>\n";
  for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
    const int headerX = heatLeft + col * cellWidth + cellWidth / 2;
    file << "<text class=\"muted text\" x=\"" << headerX << "\" y=\"792\" font-size=\"10\" text-anchor=\"start\" transform=\"rotate(-55 " << headerX << " 792)\">"
         << col * POWERTABLE_WATT_INCREMENT << " W</text>\n";
  }
  for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
    const int cellY = heatTop + row * cellHeight;
    const int cadence = MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT;
    file << "<text class=\"text\" x=\"130\" y=\"" << cellY + 22 << "\" font-size=\"12\" text-anchor=\"end\">" << cadence << " RPM</text>\n";
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      const int cellX = heatLeft + col * cellWidth;
      const int16_t position = ptData.tableRow[row].tableEntry[col].targetPosition;
      if (position == INT16_MIN) {
        file << "<rect class=\"empty\" x=\"" << cellX << "\" y=\"" << cellY << "\" width=\"41\" height=\"32\"><title>" << cadence << " RPM · "
             << col * POWERTABLE_WATT_INCREMENT << " W · no measurement</title></rect>\n";
        continue;
      }
      const double fraction = static_cast<double>(position - minimumPosition) / std::max(1, maximumPosition - minimumPosition);
      const int hue = static_cast<int>(std::round(220.0 - 210.0 * fraction));
      const int lightness = static_cast<int>(std::round(78.0 - 40.0 * fraction));
      const char* textColor = lightness < 55 ? "#ffffff" : "#101827";
      file << "<rect class=\"cell\" fill=\"hsl(" << hue << " 72% " << lightness << "%)\" x=\"" << cellX << "\" y=\"" << cellY << "\" width=\"41\" height=\"32\"><title>"
           << cadence << " RPM · " << col * POWERTABLE_WATT_INCREMENT << " W · position " << position << "</title></rect>\n"
           << "<text x=\"" << cellX + 20 << "\" y=\"" << cellY + 21 << "\" fill=\"" << textColor << "\" font-family=\"system-ui,sans-serif\" font-size=\"10\" font-weight=\"600\" text-anchor=\"middle\">" << position << "</text>\n";
    }
  }
  file << "</svg>\n";
  return file.good();
}
