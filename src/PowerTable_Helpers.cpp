/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "PowerTable_Helpers.h"

// if building PLATFORMIO_ENV_NATIVE environment define SS2K_LOG as Serial.printf(), else include the SS2KLog.h.
#ifdef PLATFORMIO_ENV_NATIVE
#include <fstream>
#include <sstream>
#include <cmath>
#include <stdint.h>
#include <algorithm>
#include <iterator>
#include <map>
#include <cstdint>
std::ofstream outFile("test/output/test_PowerTable_Helpers.txt", std::ios::trunc);

#define SS2K_LOG(tag, format, ...)                                \
  {                                                               \
    char buffer[1024];                                            \
    std::snprintf(buffer, sizeof(buffer), format, ##__VA_ARGS__); \
    outFile << "[" << tag << "] " << buffer << std::endl;         \
  }
#else
#include "SS2KLog.h"
#endif

// uses testValue in targetPosition/TABLE_DIVISOR
TestResults PTHelpers::testNeighbors(ptIndex index, int testValue, PTData& ptData) {
  TestResults returnResult;

  // Define direction parameters (start limit, end limit, step, row change, column change)
  const struct {
    int startLimit;
    int endLimit;
    int step;
    int rowChange;
    int colChange;
    TestResults::Neighbor* neighbor;
    bool (*testPredicate)(int16_t, int);
  } directions[] = {// Left: decreasing j, same i, lower target position
                    {index.wattIndex > 0 ? index.wattIndex - 1 : -1, -1, -1, 0, 0, &returnResult.leftNeighbor,
                     [](int16_t pos, int test) {
                       // SS2K_LOG(PTDATA_LOG_TAG, "Testing left neighbor: pos=%d, test=%d", pos, test);
                       return pos < test || pos == INT16_MIN;
                     }},
                    // Right: increasing j, same i, higher target position
                    {index.wattIndex < POWERTABLE_WATT_SIZE - 1 ? index.wattIndex + 1 : POWERTABLE_WATT_SIZE, POWERTABLE_WATT_SIZE, 1, 0, 0, &returnResult.rightNeighbor,
                     [](int16_t pos, int test) {
                       // SS2K_LOG(PTDATA_LOG_TAG, "Testing right neighbor: pos=%d, test=%d", pos, test);
                       return pos > test || pos == INT16_MIN;
                     }},
                    // Top: decreasing i, same j, lower target position
                    {index.cadIndex > 0 ? index.cadIndex - 1 : -1, -1, -1, 1, 0, &returnResult.topNeighbor,
                     [](int16_t pos, int test) {
                       // SS2K_LOG(PTDATA_LOG_TAG, "Testing top neighbor: pos=%d, test=%d", pos, test);
                       return pos > test || pos == INT16_MIN;
                     }},
                    // Bottom: increasing i, same j, lower target position
                    {index.cadIndex < POWERTABLE_CAD_SIZE - 1 ? index.cadIndex + 1 : POWERTABLE_CAD_SIZE, POWERTABLE_CAD_SIZE, 1, 1, 0, &returnResult.bottomNeighbor,
                     [](int16_t pos, int test) { return pos < test || pos == INT16_MIN; }}};

  // Process each direction
  for (const auto& dir : directions) {
    bool skipDirection = false;
    if (dir.rowChange == 0) {
      skipDirection = (dir.startLimit < 0 || dir.startLimit >= POWERTABLE_WATT_SIZE);
    } else {
      skipDirection = (dir.startLimit < 0 || dir.startLimit >= POWERTABLE_CAD_SIZE);
    }

    if (skipDirection) {
      // If we are at the edge and can't search, treat as passing the test (neighbor not found)
      dir.neighbor->passedTest = 1;
      continue;
    }

    bool found = false;
    for (int idx = dir.startLimit; idx != dir.endLimit; idx += dir.step) {
      int row = dir.rowChange ? idx : index.cadIndex;
      int col = dir.rowChange ? index.wattIndex : idx;
      if (row < 0 || row >= POWERTABLE_CAD_SIZE || col < 0 || col >= POWERTABLE_WATT_SIZE) {
        continue;
      }
      if (ptData.tableRow[row].tableEntry[col].targetPosition != INT16_MIN) {
        dir.neighbor->targetPosition  = ptData.tableRow[row].tableEntry[col].targetPosition;
        dir.neighbor->index.cadIndex  = row;
        dir.neighbor->index.wattIndex = col;
        dir.neighbor->found           = 1;
        found                         = true;
        break;
      }
    }
    if (found) {
      if (dir.testPredicate(dir.neighbor->targetPosition, testValue)) {
        dir.neighbor->passedTest = 1;
      }
    } else {
      // If we reached the table limit and found no neighbor, pass the test in this direction
      dir.neighbor->passedTest = 1;
    }
  }
  // Check if all neighbors were found.
  if (returnResult.bottomNeighbor.found && returnResult.topNeighbor.found && returnResult.rightNeighbor.found && returnResult.leftNeighbor.found) {
    returnResult.allNeighborsFound = 1;
  }
  // Check if all neighbors passed tests.
  if (returnResult.bottomNeighbor.passedTest && returnResult.topNeighbor.passedTest && returnResult.rightNeighbor.passedTest && returnResult.leftNeighbor.passedTest) {
    returnResult.allNeighborsPassed = 1;
  }

  return returnResult;
}

// Calculate index in the table for the given watts and cadence
ptIndex PTHelpers::calculateIndex(int watts, int cad) {
  ptIndex index;
  index.wattIndex = round((float)watts / (float)POWERTABLE_WATT_INCREMENT);
  index.cadIndex  = round(((float)cad - (float)MINIMUM_TABLE_CAD) / (float)POWERTABLE_CAD_INCREMENT);

  // SS2K_LOG(PTDATA_LOG_TAG, "Calculated indices: wattIndex=%d, CadIndex=%d (cad=%d, watts=%d)", index.wattIndex, index.cadIndex, cad, watts);
  return index;
}

/**
 * @brief Retrieves the x and y values from a specific row of the power table.
 *
 * This function extracts the x and y values from the specified row of the power table
 * in the provided PTData object. It skips entries where the target position is set
 * to INT16_MIN, which indicates an invalid or uninitialized entry.
 *
 * @param row The index of the row to retrieve data from.
 * @param ptData Reference to the PTData object containing the power table data.
 * @return A pair of vectors:
 *         - The first vector contains the x values (watt increments).
 *         - The second vector contains the y values (target positions).
 */
std::pair<std::vector<float>, std::vector<float>> PTHelpers::getRow(int row, PTData& ptData) {
  std::vector<float> xValues;
  std::vector<float> yValues;
  // clamp row to be within table bounds
  if (row < 0) row = 0;
  if (row >= POWERTABLE_CAD_SIZE) row = POWERTABLE_CAD_SIZE - 1;
  for (int j = 0; j < POWERTABLE_WATT_SIZE; ++j) {
    if (ptData.tableRow[row].tableEntry[j].targetPosition != INT16_MIN) {
      xValues.push_back(static_cast<float>(j * POWERTABLE_WATT_INCREMENT));
      yValues.push_back(static_cast<float>(ptData.tableRow[row].tableEntry[j].targetPosition));
    }
  }
  return {xValues, yValues};
}

/**
 * @brief Extracts the x and y values for a specific column from the power table data.
 *
 * This function iterates through the rows of the power table data and retrieves
 * the x and y values for the specified column. The x values are calculated based
 * on the cadence index, and the y values are extracted from the target position
 * of the table entries. Only valid entries (where the target position is not
 * INT16_MIN) are included in the result.
 *
 * @param column The index of the column to extract data from.
 * @param ptData Reference to the PTData structure containing the power table data.
 * @return A pair of vectors, where the first vector contains the x values and the
 *         second vector contains the corresponding y values.
 */
std::pair<std::vector<float>, std::vector<float>> PTHelpers::getColumn(int column, PTData& ptData) {
  std::vector<float> xValues;
  std::vector<float> yValues;
  // clamp column to be within table bounds
  if (column < 0) column = 0;
  if (column >= POWERTABLE_WATT_SIZE) column = POWERTABLE_WATT_SIZE - 1;
  for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
    if (ptData.tableRow[i].tableEntry[column].targetPosition != INT16_MIN) {
      xValues.push_back(static_cast<float>(MINIMUM_TABLE_CAD + i * POWERTABLE_CAD_INCREMENT));
      yValues.push_back(static_cast<float>(ptData.tableRow[i].tableEntry[column].targetPosition));
    }
  }
  return {xValues, yValues};
}

int32_t PTHelpers::lookup(int watts, int cad, PTData& ptData) {
  ptIndex index = calculateIndex(watts, cad);

  // Define table boundaries
  const int MIN_CAD_VAL  = MINIMUM_TABLE_CAD;
  const int MAX_CAD_VAL  = MINIMUM_TABLE_CAD + (POWERTABLE_CAD_SIZE - 1) * POWERTABLE_CAD_INCREMENT;
  const int MIN_WATT_VAL = 0;  // Assuming watts index start from 0
  const int MAX_WATT_VAL = (POWERTABLE_WATT_SIZE - 1) * POWERTABLE_WATT_INCREMENT;
  int32_t resistance     = RETURN_ERROR;
  std::pair<std::vector<float>, std::vector<float>> dataPoints;

  bool isCadOutOfTable = cad < MIN_CAD_VAL || cad > MAX_CAD_VAL;
  // Watts < 0 is also out of table, ensure watts is not negative before typical checks
  bool isWattOutOfTable = watts < MIN_WATT_VAL || watts > MAX_WATT_VAL;

  // ---- A. Handle Out-of-Table simple Extrapolation ----
  if (isCadOutOfTable && !isWattOutOfTable) {
    int targetWattIndex = index.wattIndex;
    // Clamp targetWattIndex to be within table bounds for selecting the column
    if (targetWattIndex < 0) targetWattIndex = 0;
    dataPoints = getColumn(targetWattIndex, ptData);
    if (dataPoints.first.size() >= 2) {
      float extrapolatedVal = linearExtrapolate(dataPoints, dataPoints.first.size(), static_cast<float>(cad));
      if (extrapolatedVal != INT16_MIN && !std::isnan(extrapolatedVal) && !std::isinf(extrapolatedVal)) {
        resistance = static_cast<int32_t>(round(extrapolatedVal)) * TABLE_DIVISOR;
      }
    }
  }

  // A.2. Attempt Watt Extrapolation if watts are out of bounds
  if (!isCadOutOfTable) {
    int targetCadIndex = index.cadIndex;
    dataPoints.first.clear();
    dataPoints.second.clear();
    // Clamp targetCadIndex to be within table bounds for selecting the row
    if (targetCadIndex < 0) targetCadIndex = 0;

    dataPoints = getRow(targetCadIndex, ptData);
    if (dataPoints.first.size() >= 2) {
      float extrapolatedVal = linearExtrapolate(dataPoints, dataPoints.first.size(), static_cast<float>(watts));
      if (extrapolatedVal != INT16_MIN && !std::isnan(extrapolatedVal) && !std::isinf(extrapolatedVal)) {
        resistance = static_cast<int32_t>(round(extrapolatedVal)) * TABLE_DIVISOR;
      }
    }
  }
  return resistance;  // Return early if we found a valid extrapolated value
  // ---- B. Handle In-Table Lookup (Direct, Interpolation) ----
  // At this point, both 'watts' and 'cad' are considered within the conceptual table boundaries.
  TestResults neighbors = testNeighbors(index, 0, ptData);  // testValue for testNeighbors is not critical here.

  float R1 = INT16_MIN, R2 = INT16_MIN, R3 = INT16_MIN;

  // R3: Value at the calculated grid cell (index.cadIndex, index.wattIndex)
  if (index.cadIndex >= 0 && index.cadIndex < POWERTABLE_CAD_SIZE && index.wattIndex >= 0 && index.wattIndex < POWERTABLE_WATT_SIZE &&
      ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition != INT16_MIN) {
    R3 = static_cast<float>(ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition);
  }

  // R1: Interpolation along WATT axis (horizontal) using neighbors
  if (neighbors.leftNeighbor.found && neighbors.rightNeighbor.found && neighbors.leftNeighbor.targetPosition != INT16_MIN && neighbors.rightNeighbor.targetPosition != INT16_MIN) {
    float xWattPts[2] = {static_cast<float>(neighbors.leftNeighbor.index.wattIndex * POWERTABLE_WATT_INCREMENT),
                         static_cast<float>(neighbors.rightNeighbor.index.wattIndex * POWERTABLE_WATT_INCREMENT)};
    float yWattPts[2] = {static_cast<float>(neighbors.leftNeighbor.targetPosition), static_cast<float>(neighbors.rightNeighbor.targetPosition)};
    if (xWattPts[0] != xWattPts[1]) {  // Avoid division by zero
      R1 = linearExtrapolate(std::make_pair(std::vector<float>(xWattPts, xWattPts + 2), std::vector<float>(yWattPts, yWattPts + 2)), 2, static_cast<float>(watts));
    } else if (fabs(xWattPts[0] - static_cast<float>(watts)) < 1e-3) {  // If points are same and at target watts
      R1 = yWattPts[0];
    }
  }

  // R2: Interpolation along CADENCE axis (vertical) using neighbors
  if (neighbors.topNeighbor.found && neighbors.bottomNeighbor.found && neighbors.topNeighbor.targetPosition != INT16_MIN && neighbors.bottomNeighbor.targetPosition != INT16_MIN) {
    float xCadPts[2] = {static_cast<float>(neighbors.topNeighbor.index.cadIndex * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD),
                        static_cast<float>(neighbors.bottomNeighbor.index.cadIndex * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD)};
    float yCadPts[2] = {static_cast<float>(neighbors.topNeighbor.targetPosition), static_cast<float>(neighbors.bottomNeighbor.targetPosition)};
    if (xCadPts[0] != xCadPts[1]) {  // Avoid division by zero
      R2 = linearExtrapolate(std::make_pair(std::vector<float>(xCadPts, xCadPts + 2), std::vector<float>(yCadPts, yCadPts + 2)), 2, static_cast<float>(cad));
    } else if (fabs(xCadPts[0] - static_cast<float>(cad)) < 1e-3) {  // If points are same and at target cadence
      R2 = yCadPts[0];
    }
  }

  // Combine R1, R2, R3
  float sum   = 0;
  float count = 0;

  if (R1 != INT16_MIN && !std::isnan(R1) && !std::isinf(R1)) {
    sum += R1;
    count++;
  }
  if (R2 != INT16_MIN && !std::isnan(R2) && !std::isinf(R2)) {
    sum += R2;
    count++;
  }
  if (R3 != INT16_MIN && !std::isnan(R3) && !std::isinf(R3)) {
    sum += R3;
    count++;
  }

  if (count > 0) {
    resistance = static_cast<int32_t>(round(sum / count)) * TABLE_DIVISOR;
    // SS2K_LOG(PTDATA_LOG_TAG, "Lookup result: watts=%d, cad=%d, resistance=%d", watts, cad, resistance);
    //  LOG R1, R2, R3 values for debugging
    // SS2K_LOG(PTDATA_LOG_TAG, "R1: %f, R2: %f, R3: %f", R1, R2, R3);
  }

  return resistance;  // All lookup methods failed
}

/**
 * @brief Estimates the y-coordinate corresponding to a given x-coordinate `j`
 *        using linear extrapolation or interpolation based on provided data points.
 *
 * This function takes a pair of vectors `xy` representing the x-coordinates and
 * y-coordinates of data points, respectively, and estimates the y-coordinate
 * corresponding to the given x-coordinate `j`. If `j` is outside the range of `x`,
 * the function extrapolates using the nearest two points. If `j` is within the
 * range of `x`, the function interpolates using the two nearest points.
 *
 * @param xy A pair of vectors where the first vector contains x-coordinates
 *           (must be sorted in ascending order) and the second vector contains
 *           the corresponding y-coordinates.
 * @param n The number of elements in the `xy` pair.
 * @param j The x-coordinate for which the y-coordinate is to be estimated.
 * @return The estimated y-coordinate corresponding to `j`. If the calculation fails
 *         (e.g., due to division by zero), the function returns `INT16_MIN`.
 */
float PTHelpers::linearExtrapolate(std::pair<std::vector<float>, std::vector<float>> xy, size_t n, float j) {
  float x0, x1, y0, y1;

  x0 = xy.first[0], x1 = xy.first[n - 1];
  y0 = xy.second[0], y1 = xy.second[n - 1];

  if (x1 - x0 == 0) {
    // SS2K_LOG(PTDATA_LOG_TAG, "Linear Extrapolation failed, x1 - x0 is 0. x0=%f, x1=%f, y0=%f, y1=%f, n=%zu", x0, x1, y0, y1, n);
    for (size_t i = 0; i < n; ++i) {
      // SS2K_LOG(PTDATA_LOG_TAG, "xy[%zu]: x=%f, y=%f", i, xy.first[i], xy.second[i]);
    }
    return INT16_MIN;
  }

  float slope = (y1 - y0) / (x1 - x0);
  return y0 + slope * (j - x0);
}

/**
 * @brief Counts the number of valid entries in the power table.
 *
 * This function iterates through the power table and counts entries that are considered valid.
 * If minReadings is 0 (the default), an entry is valid if its targetPosition is not INT16_MIN.
 * Otherwise, an entry is valid if its readings count is greater than or equal to minReadings.
 *
 * @param ptData Reference to the PTData structure containing the power table.
 * @param minReadings (optional) Minimum number of readings required for an entry to be considered valid. Default is 0.
 * @return int The number of valid entries in the power table.
 */
int PTHelpers::getNumEntries(PTData& ptData, int minReadings /*= 0*/) {
  int ret = 0;
  if (minReadings == 0) {
    for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
      for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
        if (ptData.tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
          ret++;
        }
      }
    }
  } else {
    for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
      for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
        if (ptData.tableRow[i].tableEntry[j].readings >= minReadings) {
          ret++;
        }
      }
    }
  }
  return ret;
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

// Use the powertable to preform a reverse lookup of the target position to find the watts at a given cadence.
int32_t PTHelpers::lookupWatts(int cad, int32_t targetPosition, PTData& ptData) {
  if (cad < POWERTABLE_CAD_INCREMENT * 2) {
    return 0;
  }

  // Calculate center point value
  int centerWatts = extrapolateWattsFromCadence(cad, targetPosition, ptData);

  // Sample additional points for Gaussian smoothing
  int cadLower      = cad - POWERTABLE_CAD_INCREMENT;
  int cadHigher     = cad + POWERTABLE_CAD_INCREMENT;
  int32_t posLower  = targetPosition - 500;
  int32_t posHigher = targetPosition + 500;

  // Calculate watts for the additional points
  int wattsLowerCad  = extrapolateWattsFromCadence(cadLower, targetPosition, ptData);
  int wattsHigherCad = extrapolateWattsFromCadence(cadHigher, targetPosition, ptData);
  int wattsLowerPos  = extrapolateWattsFromCadence(cad, posLower, ptData);
  int wattsHigherPos = extrapolateWattsFromCadence(cad, posHigher, ptData);

  // Apply Gaussian weights (center point has highest weight)
  const float centerWeight   = 0.5f;
  const float adjacentWeight = 0.125f;  // Each adjacent point gets 1/8 weight

  // Calculate weighted average
  float weightedSum =
      centerWatts * centerWeight + wattsLowerCad * adjacentWeight + wattsHigherCad * adjacentWeight + wattsLowerPos * adjacentWeight + wattsHigherPos * adjacentWeight;

  int smoothedWatts = static_cast<int>(round(weightedSum));

  // Ensure non-negative value
  if (smoothedWatts < 0) smoothedWatts = 0;

  return smoothedWatts;
}

int PTHelpers::extrapolateWattsFromCadence(int cad, int32_t targetPosition, PTData& ptData) {
  int watts      = 0;
  targetPosition = targetPosition / TABLE_DIVISOR;
  if (cad < 1) {
    return 0;
  }
  ptIndex index       = calculateIndex(0, cad);  // Ensure the index is calculated for the given cadence
  bool inCadenceRange = index.cadIndex >= 0 && index.cadIndex < POWERTABLE_CAD_SIZE;
  std::pair<std::vector<float>, std::vector<float>> xyUsed4Offset;

  std::pair<std::vector<float>, std::vector<float>> xy;  // Get the row data for the given cadence
  std::pair<std::vector<float>, std::vector<float>> newxy;

  if (!inCadenceRange) {
    float offset = 0.0;
    // if (!inCadenceRange) {
    int cadDelta = (index.cadIndex < 0) ? index.cadIndex : index.cadIndex - (POWERTABLE_CAD_SIZE - 1);
    if (index.cadIndex < 0) {
      xy            = getRow(0, ptData);
      xyUsed4Offset = getRow(1, ptData);
      // SS2K_LOG(PTDATA_LOG_TAG, "LookupWatts: index.cadIndex == %d, xy %f, xyused4Offset %f, cadDelta %d", index.cadIndex, xy.second[0], xyUsed4Offset.second[0], cadDelta);
    }
    if (index.cadIndex > POWERTABLE_CAD_SIZE - 1) {
      xy            = getRow(POWERTABLE_CAD_SIZE - 1, ptData);
      xyUsed4Offset = getRow(POWERTABLE_CAD_SIZE - 2, ptData);
      // SS2K_LOG(PTDATA_LOG_TAG, "LookupWatts: index.cadIndex == %d, xy %f, xyused4Offset %f, cadDelta %d", index.cadIndex, xy.second[0], xyUsed4Offset.second[0], cadDelta);
    }
    for (int i = 0; i < xy.second.size(); i++) {
      for (int j = 0; j < xyUsed4Offset.second.size(); j++) {
        if (xy.first[i] == xyUsed4Offset.first[j]) {
          offset = (((xyUsed4Offset.second[j] - xy.second[i]) * cadDelta) + offset) / 2;
        }
      }
    }
    for (int i = 0; i < xy.first.size(); i++) {
      newxy.first.push_back(xy.first[i]);
      if (index.cadIndex < 0) {
        newxy.second.push_back(xy.second[i] + offset);
      }
      if (index.cadIndex > POWERTABLE_CAD_SIZE - 1) {
        newxy.second.push_back(xy.second[i] - offset);
      }
    }
    xy = newxy;
    // SS2K_LOG(PTDATA_LOG_TAG, "LookupWatts: offset = %f", offset);
  } else {
    // SS2K_LOG(PTDATA_LOG_TAG, "Cadence was in range %d", index.cadIndex);
    xy = getRow(index.cadIndex, ptData);
  }

  // print everything in xy
  for (int i = 0; i < xy.first.size(); i++) {
    // Serial.printf("xy[%d]: %f, %f\n", i, xy.first[i], xy.second[i]);
  }

  // because this is a reverse lookup, we need to swap the pair
  std::swap(xy.first, xy.second);

  // Most accurate method if we have data in the table
  // Use lower_bound and upper_bound to find the closest points while keeping them associated in the vector
  auto lower = std::lower_bound(xy.first.begin(), xy.first.end(), static_cast<float>(targetPosition));
  auto upper = std::upper_bound(xy.first.begin(), xy.first.end(), static_cast<float>(targetPosition));

  // Ensure the iterators are within bounds and retrieve the associated values from xy.second
  float lowerX = (lower != xy.first.begin()) ? *(lower - 1) : *(lower + 1);
  float upperX = (upper != xy.first.end()) ? *upper : *(upper - 2);

  float lowerY = (lower != xy.first.begin()) ? xy.second[std::distance(xy.first.begin(), lower - 1)] : xy.second[std::distance(xy.first.begin(), lower + 1)];
  float upperY = (upper != xy.first.end()) ? xy.second[std::distance(xy.first.begin(), upper)] : xy.second[std::distance(xy.first.begin(), upper - 2)];

  // log lowerX, upperX, lowerY, upperY
  // SS2K_LOG(PTDATA_LOG_TAG, "LookupWatts: lowerX %f, upperX %f, lowerY %f, upperY %f", lowerX, upperX, lowerY, upperY);
  watts = linearExtrapolate(std::make_pair(std::vector<float>{lowerX, upperX}, std::vector<float>{lowerY, upperY}), 2, static_cast<float>(targetPosition));

  // minor change for when cadence changes within cadence increment. We will assume 1watt per rpm from the center of the cadence increment
  watts += cad - (index.cadIndex * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD);

  if (watts < 0) watts = 0;
  if (watts > 1500) watts = 1500;

  return watts;
}

void PTHelpers::fillAllCadenceLines(ptIndex index, PTData& ptData, bool addReading = false) {
  int16_t targetCalculation = 0;
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    targetCalculation = ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition - (i - index.cadIndex) * (index.wattIndex + 10);
    // Create positions for all cadence lines if they are not set.
    // This gives us a monotonic table with a linear progression of target positions.
    if (ptData.tableRow[i].tableEntry[index.wattIndex].readings <= 1) {
      ptData.tableRow[i].tableEntry[index.wattIndex].targetPosition = targetCalculation;
      // add a reading
      if (addReading) {
        ptData.tableRow[i].tableEntry[index.wattIndex].readings = 1;
      }
    }
    // Positions with a lower row (lower cadence) should have higher targetPosition, so enforce monotonicity
    if (i < index.cadIndex) {
      ptData.tableRow[i].tableEntry[index.wattIndex].targetPosition = std::max(ptData.tableRow[i].tableEntry[index.wattIndex].targetPosition, targetCalculation);
    } else if (i > index.cadIndex) {
      ptData.tableRow[i].tableEntry[index.wattIndex].targetPosition = std::min(ptData.tableRow[i].tableEntry[index.wattIndex].targetPosition, targetCalculation);
    }
  }
}

void PTHelpers::fillAllWattColumns(ptIndex index, PTData& ptData) {
  int16_t targetCalculation = 0;
  for (int i = 0; i < POWERTABLE_WATT_SIZE; i++) {
    targetCalculation = ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition - (index.wattIndex - i) * (index.cadIndex + 10);
    // Create positions for all watt columns if they are not set.
    // This gives us a monotonic table with a linear progression of target positions.
    if (ptData.tableRow[index.cadIndex].tableEntry[i].readings <= 1) {
      ptData.tableRow[index.cadIndex].tableEntry[i].targetPosition = targetCalculation;
    }
    // Each column to the right should have a higher targetPosition, so enforce monotonicity
    if (i > index.wattIndex) {
      ptData.tableRow[index.cadIndex].tableEntry[i].targetPosition = std::max(ptData.tableRow[index.cadIndex].tableEntry[i].targetPosition, targetCalculation);
    } else if (i < index.wattIndex) {
      ptData.tableRow[index.cadIndex].tableEntry[i].targetPosition = std::min(ptData.tableRow[index.cadIndex].tableEntry[i].targetPosition, targetCalculation);
    }
    ptIndex newIndex;
    newIndex.cadIndex  = index.cadIndex;
    newIndex.wattIndex = i;
    fillAllCadenceLines(newIndex, ptData, false);
  }
}

/**
 * @brief Updates or enters data into the power table for a specific row and entry.
 *
 * This function records a new target position or averages the new position with
 * existing data for a specific table entry. It ensures that the number of readings
 * does not exceed a defined limit to prevent dilution of recent data. Additionally,
 * it triggers table filling and extrapolation processes if the number of entries
 * exceeds a threshold.
 * @param index The index of the table entry
 * @param pos The new target position to record or average.
 */
void PTHelpers::enterData(PTData& ptData, ptIndex index, int pos) {
  if (ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings == 0) {  // if first reading in this entry
    ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition = pos;
    SS2K_LOG(PTDATA_LOG_TAG, "New entry recorded (%d)(%d)(%d)", index.cadIndex, index.wattIndex, ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition);
    ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings++;  // for initial spot on readings, give 2 (one below as well)
  } else {                                                                   // Average and update the readings.
    ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition =
        (pos + (ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition * ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings)) /
        (ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings + 1.0f);
    SS2K_LOG(PTDATA_LOG_TAG, "Existing entry averaged (%d)(%d)(%d), readings(%d)", index.cadIndex, index.wattIndex,
             ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition, ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings);
    if (ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings > MAX_NEIGHBOR_WEIGHT) {
      ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings = MAX_NEIGHBOR_WEIGHT;
    }
  }
  ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings++;
  // because of monotonicity, we can make some assumptions in order to fill the table.
  fillAllCadenceLines(index, ptData, true);
  fillAllWattColumns(index, ptData);
}
