/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "PowerTable_Helpers.h"

#include <cmath>
#include <cstdint>

// if building PLATFORMIO_ENV_NATIVE environment define SS2K_LOG as Serial.printf(), else include the SS2KLog.h.
#ifdef PLATFORMIO_ENV_NATIVE
#define SS2K_LOG(tag, format, ...) do { } while (0)
#else
#include "SS2KLog.h"
#endif

namespace {
constexpr int MAX_MONOTONIC_PASSES = 20;
constexpr int MAX_PAVA_ENTRIES =
    POWERTABLE_WATT_SIZE > POWERTABLE_CAD_SIZE ? POWERTABLE_WATT_SIZE : POWERTABLE_CAD_SIZE;
constexpr int MAX_ESTIMATED_POWER_WATTS = 4000;
constexpr int MIN_EXTRAPOLATION_POSITION_SPAN = 20;
constexpr double MAX_LOCAL_SLOPE_RATIO = 2.0;

int32_t storedPositionToSteps(float storedPosition) {
  const double steps = static_cast<double>(storedPosition) * TABLE_DIVISOR;
  if (!std::isfinite(steps)) return RETURN_ERROR;
  if (steps >= INT32_MAX) return INT32_MAX;
  // Keep INT32_MIN reserved as RETURN_ERROR.
  if (steps <= static_cast<double>(INT32_MIN) + 1.0) return INT32_MIN + 1;
  return static_cast<int32_t>(std::round(steps));
}

bool measuredRowPosition(const TableRow& row, double watts, double& position) {
  int lower = -1;
  int upper = -1;
  for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
    const TableEntry& entry = row.tableEntry[col];
    if (entry.targetPosition == INT16_MIN || entry.readings < 2) continue;
    const int sampleWatts = col * POWERTABLE_WATT_INCREMENT;
    if (sampleWatts <= watts) lower = col;
    if (sampleWatts >= watts && upper < 0) upper = col;
  }
  // Deliberately refuse extrapolation: end segments are where sparse table
  // rows most often stop paralleling their cadence neighbors.
  if (lower < 0 || upper < 0) return false;
  if (lower == upper) {
    position = row.tableEntry[lower].targetPosition;
    return true;
  }
  const double lowerWatts = lower * POWERTABLE_WATT_INCREMENT;
  const double upperWatts = upper * POWERTABLE_WATT_INCREMENT;
  const double fraction   = (watts - lowerWatts) / (upperWatts - lowerWatts);
  position = row.tableEntry[lower].targetPosition +
             fraction * (row.tableEntry[upper].targetPosition - row.tableEntry[lower].targetPosition);
  return std::isfinite(position);
}

bool slopesAgree(double first, double second) {
  if (!std::isfinite(first) || !std::isfinite(second) || first <= 0.0 || second <= 0.0) return false;
  return std::max(first, second) <= std::min(first, second) * MAX_LOCAL_SLOPE_RATIO;
}

bool getObservedWattBounds(int cad, PTData& ptData, int& minimumWatts, int& maximumWatts) {
  minimumWatts = MAX_ESTIMATED_POWER_WATTS;
  maximumWatts = 0;
  bool found   = false;

  for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
    int reliableSamples = 0;
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      const TableEntry& entry = ptData.tableRow[row].tableEntry[col];
      if (entry.targetPosition != INT16_MIN && entry.readings >= 2) ++reliableSamples;
    }
    if (reliableSamples < 2) continue;

    const int sampleCadence = MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT;
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      const TableEntry& entry = ptData.tableRow[row].tableEntry[col];
      if (entry.targetPosition == INT16_MIN || entry.readings < 2) continue;

      const double normalizedWatts = static_cast<double>(col * POWERTABLE_WATT_INCREMENT) * cad / sampleCadence;
      const int boundedWatts = normalizedWatts >= MAX_ESTIMATED_POWER_WATTS
                                   ? MAX_ESTIMATED_POWER_WATTS
                                   : static_cast<int>(std::round(normalizedWatts));
      if (boundedWatts < minimumWatts) minimumWatts = boundedWatts;
      if (boundedWatts > maximumWatts) maximumWatts = boundedWatts;
      found = true;
    }
  }
  return found;
}

// Apply weighted pool-adjacent-violators to only the measured entries in a
// row or column. Empty grid cells are deliberately left empty: lookup owns
// interpolation and extrapolation between the measured calibration points.
bool enforceMonotonicEntries(TableEntry* const entries[], int entryCount, bool increasing) {
  if (entryCount < 2) return false;

  float blockPosition[MAX_PAVA_ENTRIES];
  float blockWeight[MAX_PAVA_ENTRIES];
  int blockStart[MAX_PAVA_ENTRIES];
  int blockEnd[MAX_PAVA_ENTRIES];
  int blockCount = 0;

  for (int i = 0; i < entryCount; ++i) {
    blockPosition[blockCount] = entries[i]->targetPosition;
    blockWeight[blockCount]   = entries[i]->readings;
    blockStart[blockCount]    = i;
    blockEnd[blockCount]      = i;
    ++blockCount;

    while (blockCount >= 2) {
      const int previous = blockCount - 2;
      const int current  = blockCount - 1;
      const bool violation = increasing ? blockPosition[previous] > blockPosition[current]
                                        : blockPosition[previous] < blockPosition[current];
      if (!violation) break;

      const float totalWeight = blockWeight[previous] + blockWeight[current];
      blockPosition[previous] = ((blockPosition[previous] * blockWeight[previous]) +
                                 (blockPosition[current] * blockWeight[current])) /
                                totalWeight;
      blockWeight[previous] = totalWeight;
      blockEnd[previous]    = blockEnd[current];
      --blockCount;
    }
  }

  bool changed = false;
  for (int block = 0; block < blockCount; ++block) {
    const int16_t correctedPosition = static_cast<int16_t>(round(blockPosition[block]));
    for (int i = blockStart[block]; i <= blockEnd[block]; ++i) {
      if (entries[i]->targetPosition != correctedPosition) {
        entries[i]->targetPosition = correctedPosition;
        changed                    = true;
      }
    }
  }
  return changed;
}
}  // namespace


/**
 * @brief Enters a new data point and then enforces monotonicity on the whole table.
 * * This function first calculates the running average for the given data point.
 * Then, it calls the PAVA helper functions to ensure resistance does not fall
 * as power rises or rise as cadence increases.
 * * @param ptData The main power table data structure.
 * @param index The watt and cadence index for the new data point.
 * @param pos The measured targetPosition for this data point.
 */
void PTHelpers::enterData(PTData& ptData, ptIndex index, int pos) {
  // Reference to the specific table entry for cleaner code
  TableEntry& entry = ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex];

  bool moveTable = false;
  clean(ptData);

  // Get the topmost value in the column

  if (entry.readings == 0) {  // if first reading in this entry
    entry.readings = 1;  // The common increment below marks this first measured entry as reliable (2).
    SS2K_LOG(PTDATA_LOG_TAG, "New entry recorded (%d)(%d)(%d)", index.cadIndex, index.wattIndex, pos);
  } else {  // Average and update the readings.
    // Use floating point for accuracy in averaging
    float current_total_pos = (float)entry.targetPosition * entry.readings;
    float new_avg_pos       = (pos + current_total_pos) / (entry.readings + 1.0f);
    pos                     = (int16_t)new_avg_pos;

    SS2K_LOG(PTDATA_LOG_TAG, "Existing entry averaged (%d)(%d)(%d), readings(%d)", index.cadIndex, index.wattIndex, pos, entry.readings);
  }

  // Get the value to the left of the new entry
  for (int i = index.wattIndex - 1; i > 0; i--) {
    if (ptData.tableRow[index.cadIndex].tableEntry[i].targetPosition > pos) {
      SS2K_LOG(PTDATA_LOG_TAG, "Greater Left Found %d, %d, tp%d", index.cadIndex, i, ptData.tableRow[index.cadIndex].tableEntry[i].targetPosition);
      ptData.tableRow[index.cadIndex].tableEntry[i].readings--;
      moveTable = true;
      break;
    }
  }

  // get the value below the new entry
  for (int j = index.cadIndex + 1; j < POWERTABLE_CAD_SIZE - 1; j++) {
    if (ptData.tableRow[j].tableEntry[index.wattIndex].targetPosition > pos) {
      SS2K_LOG(PTDATA_LOG_TAG, "Greater Down Found %d, %d, tp%d", j, index.wattIndex, ptData.tableRow[j].tableEntry[index.wattIndex].targetPosition);
      ptData.tableRow[j].tableEntry[index.wattIndex].readings--;
      moveTable = true;
      break;
    }
  }

  // get the value to the right of the entry
  for (int i = index.wattIndex + 1; i < POWERTABLE_WATT_SIZE - 1; i++) {
    if (ptData.tableRow[index.cadIndex].tableEntry[i].targetPosition != INT16_MIN && ptData.tableRow[index.cadIndex].tableEntry[i].targetPosition < pos) {
      SS2K_LOG(PTDATA_LOG_TAG, "Lower Right Found %d, %d, tp%d", index.cadIndex, i, ptData.tableRow[index.cadIndex].tableEntry[i].targetPosition);
      ptData.tableRow[index.cadIndex].tableEntry[i].readings--;
      moveTable = true;
      break;
    }
  }

  // get the value above the entry
  for (int j = index.cadIndex - 1; j > 0; j--) {
    if (ptData.tableRow[j].tableEntry[index.wattIndex].targetPosition != INT16_MIN && ptData.tableRow[j].tableEntry[index.wattIndex].targetPosition < pos) {
      SS2K_LOG(PTDATA_LOG_TAG, "Lower Up Found %d, %d, tp%d", j, index.wattIndex, ptData.tableRow[j].tableEntry[index.wattIndex].targetPosition);
      ptData.tableRow[j].tableEntry[index.wattIndex].readings--;
      moveTable = true;
      break;
    }
  }

  if (moveTable) {
    return;
  }

  entry.targetPosition = pos;  // Update the target position with the new average
  // Increment readings, capping at the max value.
  if (entry.readings < MAX_NEIGHBOR_WEIGHT && !moveTable) {
    entry.readings++;
  }

  // Row and column constraints can disturb one another, so alternate them
  // until neither changes. The cap protects the firmware from pathological
  // input while being comfortably larger than this 10x30 table requires.
  bool changed = false;
  int pass      = 0;
  do {
    const bool powerChanged   = enforceMonotonicAcrossPower(ptData);
    const bool cadenceChanged = enforceMonotonicAcrossCadence(ptData);
    changed = powerChanged || cadenceChanged;
    ++pass;
    SS2K_LOG(PTDATA_LOG_TAG, "Monotonic pass %d: power %d, cadence %d", pass, powerChanged, cadenceChanged);
  } while (changed && pass < MAX_MONOTONIC_PASSES);

  if (changed) {
    SS2K_LOG(PTDATA_LOG_TAG, "Monotonic constraints reached the %d-pass safety limit", MAX_MONOTONIC_PASSES);
  }
}

// Calculate index in the table for the given watts and cadence
ptIndex PTHelpers::calculateIndex(int watts, int cad) {
  ptIndex index;
  index.wattIndex = round((float)watts / (float)POWERTABLE_WATT_INCREMENT);
  index.cadIndex  = round(((float)cad - (float)MINIMUM_TABLE_CAD) / (float)POWERTABLE_CAD_INCREMENT);

  // SS2K_LOG(PTDATA_LOG_TAG, "Calculated indices: wattIndex=%d, CadIndex=%d (cad=%d, watts=%d)", index.wattIndex, index.cadIndex, cad, watts);
  return index;
}

bool PTHelpers::cadenceIsWithinTable(int cad) {
  const ptIndex index = calculateIndex(0, cad);
  return index.cadIndex >= 0 && index.cadIndex < POWERTABLE_CAD_SIZE;
}


int32_t PTHelpers::lookup(int watts, int cad, PTData& ptData) {
  if (cad <= 0 || watts < 0) return RETURN_ERROR;

  float rowPosition[POWERTABLE_CAD_SIZE];
  int rowCadence[POWERTABLE_CAD_SIZE];
  int validRows = 0;

  // Determine the requested position independently in each cadence row. Use
  // the surrounding reliable watt samples for interpolation, or the nearest
  // two reliable samples for extrapolation beyond a row's measured range.
  for (int row = 0; row < POWERTABLE_CAD_SIZE; row++) {
    int reliableSamples = 0;
    for (int col = 0; col < POWERTABLE_WATT_SIZE; col++) {
      const TableEntry& entry = ptData.tableRow[row].tableEntry[col];
      if (entry.targetPosition != INT16_MIN && entry.readings >= 2) reliableSamples++;
    }
    // A lone point establishes no watt-to-position slope and can introduce a
    // discontinuity when mixed with complete neighboring rows.
    if (reliableSamples < 2) continue;

    const int sampleCadence = MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT;
    // Compare equal torque operating points between cadence rows. At a fixed
    // resistance, power changes approximately in proportion to cadence.
    const float rowTargetWatts = static_cast<float>(watts) * sampleCadence / cad;
    int below = -1;
    int above = -1;
    int first = -1;
    int second = -1;
    int penultimate = -1;
    int last = -1;

    for (int col = 0; col < POWERTABLE_WATT_SIZE; col++) {
      const TableEntry& entry = ptData.tableRow[row].tableEntry[col];
      if (entry.targetPosition == INT16_MIN || entry.readings < 2) continue;

      if (first < 0) {
        first = col;
      } else if (second < 0) {
        second = col;
      }
      penultimate = last;
      last = col;

      const int sampleWatts = col * POWERTABLE_WATT_INCREMENT;
      if (sampleWatts <= rowTargetWatts) below = col;
      if (sampleWatts >= rowTargetWatts && above < 0) above = col;
    }

    int lowerCol;
    int upperCol;
    if (below >= 0 && above >= 0 && below != above) {
      lowerCol = below;
      upperCol = above;
    } else if (below >= 0 && above >= 0) {
      rowCadence[validRows] = sampleCadence;
      rowPosition[validRows++] = ptData.tableRow[row].tableEntry[below].targetPosition;
      continue;
    } else if (below < 0 && first >= 0 && second >= 0) {
      lowerCol = first;
      upperCol = second;
      // Avoid extrapolating a long distance from an almost-flat segment. Use
      // the nearest sample that establishes a meaningful position span, or
      // the farthest available sample when the whole end of the row is flat.
      const int16_t firstPosition = ptData.tableRow[row].tableEntry[first].targetPosition;
      for (int col = second; col < POWERTABLE_WATT_SIZE; ++col) {
        const TableEntry& candidate = ptData.tableRow[row].tableEntry[col];
        if (candidate.targetPosition == INT16_MIN || candidate.readings < 2) continue;
        upperCol = col;
        if (candidate.targetPosition - firstPosition >= MIN_EXTRAPOLATION_POSITION_SPAN) break;
      }
    } else if (above < 0 && penultimate >= 0 && last >= 0) {
      lowerCol = penultimate;
      upperCol = last;
      const int16_t lastPosition = ptData.tableRow[row].tableEntry[last].targetPosition;
      for (int col = penultimate; col >= 0; --col) {
        const TableEntry& candidate = ptData.tableRow[row].tableEntry[col];
        if (candidate.targetPosition == INT16_MIN || candidate.readings < 2) continue;
        lowerCol = col;
        if (lastPosition - candidate.targetPosition >= MIN_EXTRAPOLATION_POSITION_SPAN) break;
      }
    } else {
      continue;
    }

    const float lowerWatts = lowerCol * POWERTABLE_WATT_INCREMENT;
    const float upperWatts = upperCol * POWERTABLE_WATT_INCREMENT;
    const float lowerPosition = ptData.tableRow[row].tableEntry[lowerCol].targetPosition;
    const float upperPosition = ptData.tableRow[row].tableEntry[upperCol].targetPosition;
    rowCadence[validRows] = sampleCadence;
    rowPosition[validRows++] = lowerPosition + (rowTargetWatts - lowerWatts) * (upperPosition - lowerPosition) / (upperWatts - lowerWatts);
  }

  if (validRows == 0) return RETURN_ERROR;
  if (validRows == 1) return storedPositionToSteps(rowPosition[0]);

  // Beyond the measured cadence range, torque scaling above already maps the
  // request onto the nearest real row. Do not amplify row-to-row calibration
  // noise by extrapolating position across cadence as well.
  if (cad <= rowCadence[0]) return storedPositionToSteps(rowPosition[0]);
  if (cad >= rowCadence[validRows - 1]) return storedPositionToSteps(rowPosition[validRows - 1]);

  int lowerRow = 0;
  int upperRow = 1;
  if (cad > rowCadence[0]) {
    for (int i = 1; i < validRows; i++) {
      if (cad <= rowCadence[i]) {
        lowerRow = i - 1;
        upperRow = i;
        break;
      }
    }
  }

  // Blend the two estimates at equal torque for an in-range cadence.
  const float cadenceFraction = static_cast<float>(cad - rowCadence[lowerRow]) / (rowCadence[upperRow] - rowCadence[lowerRow]);
  const float position = rowPosition[lowerRow] + cadenceFraction * (rowPosition[upperRow] - rowPosition[lowerRow]);
  return storedPositionToSteps(position);
}

bool PTHelpers::lookupSlope(int watts, int cad, double& stepsPerWatt, PTData& ptData) {
  stepsPerWatt = 0.0;
  const int sampleSpan = POWERTABLE_WATT_INCREMENT;
  if (cad <= 0 || watts < sampleSpan) return false;

  int validRow[POWERTABLE_CAD_SIZE];
  int validCadence[POWERTABLE_CAD_SIZE];
  int validRows = 0;
  for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
    int reliableSamples = 0;
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      const TableEntry& entry = ptData.tableRow[row].tableEntry[col];
      if (entry.targetPosition != INT16_MIN && entry.readings >= 2) ++reliableSamples;
    }
    if (reliableSamples < 2) continue;
    validRow[validRows]     = row;
    validCadence[validRows] = MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT;
    ++validRows;
  }

  // One row can define a lookup, but cannot prove that its end segment agrees
  // with the surface at a neighboring cadence.
  if (validRows < 2) return false;

  int lowerRow = 0;
  int upperRow = 1;
  if (cad >= validCadence[validRows - 1]) {
    lowerRow = validRows - 2;
    upperRow = validRows - 1;
  } else if (cad > validCadence[0]) {
    for (int row = 1; row < validRows; ++row) {
      if (cad <= validCadence[row]) {
        lowerRow = row - 1;
        upperRow = row;
        break;
      }
    }
  }

  double rowSlope[2];
  const int supportRows[2] = {lowerRow, upperRow};
  for (int i = 0; i < 2; ++i) {
    const int rowCadence = validCadence[supportRows[i]];
    const double rowLowerWatts = static_cast<double>(watts - sampleSpan) * rowCadence / cad;
    const double rowUpperWatts = static_cast<double>(watts + sampleSpan) * rowCadence / cad;
    double lowerPosition;
    double upperPosition;
    if (!measuredRowPosition(ptData.tableRow[validRow[supportRows[i]]], rowLowerWatts, lowerPosition) ||
        !measuredRowPosition(ptData.tableRow[validRow[supportRows[i]]], rowUpperWatts, upperPosition)) {
      return false;
    }
    rowSlope[i] = (upperPosition - lowerPosition) * TABLE_DIVISOR / (2.0 * sampleSpan);
  }
  if (!slopesAgree(rowSlope[0], rowSlope[1])) return false;

  // Also reject sharp curvature within the cadence-blended surface. A
  // monotonic table can still contain a locally abrupt, noisy change in gain.
  const int32_t lowerPosition = lookup(watts - sampleSpan, cad, ptData);
  const int32_t centerPosition = lookup(watts, cad, ptData);
  const int32_t upperPosition = lookup(watts + sampleSpan, cad, ptData);
  if (lowerPosition == RETURN_ERROR || centerPosition == RETURN_ERROR || upperPosition == RETURN_ERROR) return false;

  const double leftSlope  = static_cast<double>(centerPosition - lowerPosition) / sampleSpan;
  const double rightSlope = static_cast<double>(upperPosition - centerPosition) / sampleSpan;
  if (!slopesAgree(leftSlope, rightSlope)) return false;

  stepsPerWatt = static_cast<double>(upperPosition - lowerPosition) / (2.0 * sampleSpan);
  return std::isfinite(stepsPerWatt) && stepsPerWatt > 0.0;
}

// returns the total number of readings in the power table
int PTHelpers::getTotalReadings(PTData& ptData) {
  int totalReadings = 0;
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      totalReadings += ptData.tableRow[i].tableEntry[j].readings;
    }
  }
  return totalReadings;
}

// Invert the cadence-blended forward surface. Inverting each cadence row
// independently makes nearly-flat row segments explode during extrapolation
// and creates discontinuities as cadence crosses a row boundary.
int32_t PTHelpers::invertForwardSurface(int cad, int32_t targetPosition, PTData& ptData) {
  if (cad <= 0) return 0;

  int observedMinimumWatts;
  int observedMaximumWatts;
  if (!getObservedWattBounds(cad, ptData, observedMinimumWatts, observedMaximumWatts)) return 0;

  const int32_t zeroPosition = lookup(0, cad, ptData);
  if (zeroPosition == RETURN_ERROR) return 0;

  int searchHigh = observedMaximumWatts > 0 ? observedMaximumWatts : POWERTABLE_WATT_INCREMENT;
  int32_t highPosition = lookup(searchHigh, cad, ptData);
  if (highPosition == RETURN_ERROR || highPosition < zeroPosition) return 0;

  // A completely flat measured surface has no unique inverse. Return the
  // midpoint of its observed watt interval only when the position matches;
  // otherwise choose the nearest safe edge.
  if (highPosition == zeroPosition) {
    if (targetPosition < zeroPosition) return 0;
    if (targetPosition > zeroPosition) return observedMaximumWatts;
    return observedMinimumWatts + (observedMaximumWatts - observedMinimumWatts) / 2;
  }

  if (targetPosition < zeroPosition) return 0;

  while (highPosition < targetPosition && searchHigh < MAX_ESTIMATED_POWER_WATTS) {
    const int expandedHigh = searchHigh > MAX_ESTIMATED_POWER_WATTS / 2 ? MAX_ESTIMATED_POWER_WATTS : searchHigh * 2;
    if (expandedHigh == searchHigh) break;
    searchHigh  = expandedHigh;
    highPosition = lookup(searchHigh, cad, ptData);
    if (highPosition == RETURN_ERROR) return 0;
  }
  if (highPosition < targetPosition) return MAX_ESTIMATED_POWER_WATTS;

  // Find the first watt whose predicted position reaches the target.
  int lowerBound = 0;
  int upperBound = searchHigh;
  while (lowerBound < upperBound) {
    const int middle = lowerBound + (upperBound - lowerBound) / 2;
    const int32_t middlePosition = lookup(middle, cad, ptData);
    if (middlePosition == RETURN_ERROR) return 0;
    if (middlePosition < targetPosition) {
      lowerBound = middle + 1;
    } else {
      upperBound = middle;
    }
  }

  const int firstWatt = lowerBound;
  const int32_t firstPosition = lookup(firstWatt, cad, ptData);
  if (firstPosition == RETURN_ERROR) return 0;

  if (firstPosition == targetPosition) {
    // Resolve a non-unique inverse deterministically at the middle of the
    // plateau, but do not extend an observed plateau into extrapolated space.
    int plateauLower = firstWatt;
    int plateauUpper = firstWatt <= observedMaximumWatts ? observedMaximumWatts : searchHigh;
    while (plateauLower < plateauUpper) {
      const int middle = plateauLower + (plateauUpper - plateauLower + 1) / 2;
      const int32_t middlePosition = lookup(middle, cad, ptData);
      if (middlePosition == RETURN_ERROR) return 0;
      if (middlePosition <= targetPosition) {
        plateauLower = middle;
      } else {
        plateauUpper = middle - 1;
      }
    }
    return firstWatt + (plateauLower - firstWatt) / 2;
  }

  if (firstWatt == 0) return 0;
  const int previousWatt = firstWatt - 1;
  const int32_t previousPosition = lookup(previousWatt, cad, ptData);
  if (previousPosition == RETURN_ERROR || firstPosition <= previousPosition) return previousWatt;

  const double fraction = static_cast<double>(targetPosition - previousPosition) / (firstPosition - previousPosition);
  const double watts = previousWatt + fraction;
  if (!std::isfinite(watts) || watts <= 0.0) return 0;
  if (watts >= MAX_ESTIMATED_POWER_WATTS) return MAX_ESTIMATED_POWER_WATTS;
  return static_cast<int32_t>(std::round(watts));
}

int32_t PTHelpers::lookupWatts(int cad, int32_t targetPosition, PTData& ptData) {
  if (cad <= 0) return 0;

  // Invert at the table cadence knots, then apply a cumulative monotonic
  // envelope before interpolating. Sparse rows can otherwise imply that the
  // same resistance produces less power at a slightly higher cadence.
  int32_t knotWatts[POWERTABLE_CAD_SIZE];
  for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
    const int knotCadence = MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT;
    knotWatts[row] = invertForwardSurface(knotCadence, targetPosition, ptData);
  }

  // Apply the same cumulative cadence envelope at every resistance. Switching
  // to a separate path only when targetPosition exactly matched a measured
  // cell made watts discontinuous across resistance at those cell boundaries.
  // Each inverted knot is already monotonic in resistance; cumulative maxima
  // preserve that property while also preventing watts from falling as
  // cadence rises.
  for (int row = 1; row < POWERTABLE_CAD_SIZE; ++row) {
    if (knotWatts[row] < knotWatts[row - 1]) knotWatts[row] = knotWatts[row - 1];
  }

  if (cad <= MINIMUM_TABLE_CAD) {
    const double watts = static_cast<double>(knotWatts[0]) * cad / MINIMUM_TABLE_CAD;
    return watts > 0.0 ? static_cast<int32_t>(std::round(watts)) : 0;
  }

  const int maximumTableCadence = MINIMUM_TABLE_CAD + (POWERTABLE_CAD_SIZE - 1) * POWERTABLE_CAD_INCREMENT;
  if (cad >= maximumTableCadence) {
    const double watts = static_cast<double>(knotWatts[POWERTABLE_CAD_SIZE - 1]) * cad / maximumTableCadence;
    if (!std::isfinite(watts) || watts >= MAX_ESTIMATED_POWER_WATTS) return MAX_ESTIMATED_POWER_WATTS;
    return watts > 0.0 ? static_cast<int32_t>(std::round(watts)) : 0;
  }

  const int lowerRow = (cad - MINIMUM_TABLE_CAD) / POWERTABLE_CAD_INCREMENT;
  const int upperRow = lowerRow + 1;
  const int lowerCadence = MINIMUM_TABLE_CAD + lowerRow * POWERTABLE_CAD_INCREMENT;
  const double fraction = static_cast<double>(cad - lowerCadence) / POWERTABLE_CAD_INCREMENT;
  const double watts = knotWatts[lowerRow] + fraction * (knotWatts[upperRow] - knotWatts[lowerRow]);
  if (!std::isfinite(watts) || watts >= MAX_ESTIMATED_POWER_WATTS) return MAX_ESTIMATED_POWER_WATTS;
  return watts > 0.0 ? static_cast<int32_t>(std::round(watts)) : 0;
}

/**
 * @brief Enforces non-increasing resistance as cadence rises at fixed power.
 */
bool PTHelpers::enforceMonotonicAcrossCadence(PTData& ptData) {
  bool changed = false;
  for (int watt_idx = 0; watt_idx < POWERTABLE_WATT_SIZE; ++watt_idx) {
    TableEntry* measuredEntries[POWERTABLE_CAD_SIZE];
    int measuredCount = 0;
    for (int cad_idx = 0; cad_idx < POWERTABLE_CAD_SIZE; ++cad_idx) {
      TableEntry& entry = ptData.tableRow[cad_idx].tableEntry[watt_idx];
      if (entry.targetPosition != INT16_MIN && entry.readings >= 2) {
        measuredEntries[measuredCount++] = &entry;
      }
    }
    changed = enforceMonotonicEntries(measuredEntries, measuredCount, false) || changed;
  }
  return changed;
}

/**
 * @brief Enforces non-decreasing resistance as power rises at fixed cadence.
 */
bool PTHelpers::enforceMonotonicAcrossPower(PTData& ptData) {
  bool changed = false;
  for (int cad_idx = 0; cad_idx < POWERTABLE_CAD_SIZE; ++cad_idx) {
    TableEntry* measuredEntries[POWERTABLE_WATT_SIZE];
    int measuredCount = 0;
    for (int watt_idx = 0; watt_idx < POWERTABLE_WATT_SIZE; ++watt_idx) {
      TableEntry& entry = ptData.tableRow[cad_idx].tableEntry[watt_idx];
      if (entry.targetPosition != INT16_MIN && entry.readings >= 2) {
        measuredEntries[measuredCount++] = &entry;
      }
    }
    changed = enforceMonotonicEntries(measuredEntries, measuredCount, true) || changed;
  }
  return changed;
}

void PTHelpers::clean(PTData& ptData) {
  int removed = 0;

  // Remove inferred/invalid entries and negative positions.
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      //human readings are 2+
      if (ptData.tableRow[i].tableEntry[j].readings < 2 || ptData.tableRow[i].tableEntry[j].targetPosition < 0) {
        if (ptData.tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
          removed++;
        }
        ptData.tableRow[i].tableEntry[j].targetPosition = INT16_MIN;
        ptData.tableRow[i].tableEntry[j].readings       = 0;
      }
    }
  }

  if (removed > 0) {
    SS2K_LOG(PTDATA_LOG_TAG, "Cleaned %d readings", removed);
  }
}
