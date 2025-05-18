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
  } directions[] = {
      // Left: decreasing j, same i
      {index.wattIndex > 0 ? index.wattIndex - 1 : -1, -1, -1, 0, 0, &returnResult.leftNeighbor, [](int16_t pos, int test) { return pos < test || pos == INT16_MIN; }},
      // Right: increasing j, same i
      {index.wattIndex < POWERTABLE_WATT_SIZE - 1 ? index.wattIndex + 1 : POWERTABLE_WATT_SIZE, POWERTABLE_WATT_SIZE, 1, 0, 0, &returnResult.rightNeighbor,
       [](int16_t pos, int test) { return pos > test || pos == INT16_MIN; }},
      // Top: decreasing i, same j
      {index.cadIndex > 0 ? index.cadIndex - 1 : -1, -1, -1, 1, 0, &returnResult.topNeighbor, [](int16_t pos, int test) { return pos > test || pos == INT16_MIN; }},
      // Bottom: increasing i, same j
      {index.cadIndex < POWERTABLE_CAD_SIZE - 1 ? index.cadIndex + 1 : POWERTABLE_CAD_SIZE, POWERTABLE_CAD_SIZE, 1, 1, 0, &returnResult.bottomNeighbor,
       [](int16_t pos, int test) { return pos < test || pos == INT16_MIN; }}};

  // Process each direction
  for (const auto& dir : directions) {
    // Skip if outside bounds
    // More selective bounds checking based on direction type (horizontal vs vertical)
    bool skipDirection = false;
    if (dir.rowChange == 0) {
      // Horizontal direction (left/right) - check against WATT_SIZE
      skipDirection = (dir.startLimit < 0 || dir.startLimit >= POWERTABLE_WATT_SIZE);
    } else {
      // Vertical direction (top/bottom) - check against CAD_SIZE
      skipDirection = (dir.startLimit < 0 || dir.startLimit >= POWERTABLE_CAD_SIZE);
    }

    if (skipDirection) {
      continue;
    }

    // Search for neighbor in this direction
    for (int idx = dir.startLimit; idx != dir.endLimit; idx += dir.step) {
      int row = dir.rowChange ? idx : index.cadIndex;
      int col = dir.rowChange ? index.wattIndex : idx;
      // Only consider neighbors within table bounds
      if (row < 0 || row >= POWERTABLE_CAD_SIZE || col < 0 || col >= POWERTABLE_WATT_SIZE) {
        continue;
      }
      if (ptData.tableRow[row].tableEntry[col].targetPosition != INT16_MIN) {
        dir.neighbor->targetPosition  = ptData.tableRow[row].tableEntry[col].targetPosition;
        dir.neighbor->index.cadIndex  = row;
        dir.neighbor->index.wattIndex = col;
        dir.neighbor->found           = 1;
        break;
      }
    }
    // Test if neighbor passes test condition.
    if (dir.testPredicate(dir.neighbor->targetPosition, testValue)) {
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

// find the number of data points in the table
int PTHelpers::dataPoints(PTData& ptData) {
  int count = 0;
  for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; ++j) {
      if (ptData.tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
        count++;
      }
    }
  }
  return count;
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
  int ptDataSize         = dataPoints(ptData);
  std::pair<std::vector<float>, std::vector<float>> dataPoints;

  bool isCadOutOfTable = cad < MIN_CAD_VAL || cad > MAX_CAD_VAL;
  // Watts < 0 is also out of table, ensure watts is not negative before typical checks
  bool isWattOutOfTable = watts < MIN_WATT_VAL || watts > MAX_WATT_VAL;

  // ---- A. Handle Out-of-Table simple Extrapolation ----
  if (isCadOutOfTable && !isWattOutOfTable) {
    // A.1. Attempt Cadence Extrapolation if cadence is out of bounds
    if (isCadOutOfTable) {
      int targetWattIndex = index.wattIndex;
      // Clamp targetWattIndex to be within table bounds for selecting the column
      if (targetWattIndex < 0) targetWattIndex = 0;
      dataPoints = getColumn(targetWattIndex, ptData);
      if (dataPoints.first.size() >= 2) {
        float extrapolatedVal = linearExtrapolate(dataPoints, dataPoints.first.size(), static_cast<float>(cad));
        if (extrapolatedVal != INT16_MIN && !std::isnan(extrapolatedVal) && !std::isinf(extrapolatedVal)) {
          return static_cast<int32_t>(round(extrapolatedVal)) * TABLE_DIVISOR;
        }
      }
    }

    // A.2. Attempt Watt Extrapolation if watts are out of bounds
    if (isWattOutOfTable && !isCadOutOfTable) {
      int targetCadIndex = index.cadIndex;
      dataPoints.first.clear();
      dataPoints.second.clear();
      // Clamp targetCadIndex to be within table bounds for selecting the row
      if (targetCadIndex < 0) targetCadIndex = 0;

      dataPoints = getRow(targetCadIndex, ptData);

      if (dataPoints.first.size() >= 2) {
        float extrapolatedVal = linearExtrapolate(dataPoints, dataPoints.first.size(), static_cast<float>(watts));
        if (extrapolatedVal != INT16_MIN && !std::isnan(extrapolatedVal) && !std::isinf(extrapolatedVal)) {
          return static_cast<int32_t>(round(extrapolatedVal)) * TABLE_DIVISOR;
        }
      }
    }
    return INT32_MIN;
  }

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
    return static_cast<int32_t>(round(sum / count)) * TABLE_DIVISOR;
  }

  return INT32_MIN;  // All lookup methods failed
}

void PTHelpers::fillEmptyTable(int outerValue, const std::vector<int>& emptyIndices, std::pair<std::vector<float>, std::vector<float>> xy, size_t n, bool horizontal,
                               bool useNaturalSpline, PTData& ptData) {
  int innerSize = horizontal ? POWERTABLE_WATT_SIZE : POWERTABLE_CAD_SIZE;
  ptIndex index;

  if (n == 1) {  // If only one point, fill row with the value
    float singleValue = xy.second[0];
    for (int innerValue : emptyIndices) {
      index.cadIndex                                                             = horizontal ? outerValue : innerValue;
      index.wattIndex                                                            = horizontal ? innerValue : outerValue;
      ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition = static_cast<int>(round(singleValue));
    }
  } else if (n == 2) {  // If two points, do linear interpolation
    for (int innerValue : emptyIndices) {
      index.cadIndex  = horizontal ? outerValue : innerValue;
      index.wattIndex = horizontal ? innerValue : outerValue;

      float interpolated_value = linearExtrapolate(xy, n, innerValue);
      int tempValue            = static_cast<int>(round(interpolated_value));

      if (testNeighbors(index, tempValue, ptData).allNeighborsPassed) {
        ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition = tempValue;
      }
    }
  } else if (n >= 3) {  // If three or more points, use cubic spline interpolation
    bool validForSpline = true;
    for (size_t i = 1; i < n; ++i) {
      if (xy.first[i] <= xy.first[i - 1]) {
        validForSpline = false;
        break;
      }
    }
    if (!validForSpline) {
      SS2K_LOG(PTDATA_LOG_TAG, "Duplicate or non-increasing x-values detected!");
      return;
    }

    // Create and initialize the spline with the desired type (natural or clamped)
    CubicSpline spline;
    spline.set_points(xy, n);

    for (int innerValue : emptyIndices) {
      index.cadIndex  = horizontal ? outerValue : innerValue;
      index.wattIndex = horizontal ? innerValue : outerValue;

      float interpolated_value = spline.interpolate(innerValue);
      float minValue           = *std::min_element(xy.second.begin(), xy.second.end());
      float maxValue           = *std::max_element(xy.second.begin(), xy.second.end());
      interpolated_value       = std::max(minValue, std::min(maxValue, interpolated_value));

      int tempValue = static_cast<int>(round(interpolated_value));

      if (this->testNeighbors(index, tempValue, ptData).allNeighborsPassed) {
        ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition = tempValue;
      }
    }
  } else {
    SS2K_LOG(PTDATA_LOG_TAG, "Error: No unique points found.");
  }
}

void PTHelpers::extrapolateEmptyIndices(int outerIndex, const std::vector<int>& emptyIndices, std::pair<std::vector<float>, std::vector<float>> xy, size_t n, bool horizontal,
                                        bool naturalSpline, PTData& ptData) {
  int innerSize = horizontal ? POWERTABLE_WATT_SIZE : POWERTABLE_CAD_SIZE;
  ptIndex index;
  if (n == 1) {
    int singleValue = static_cast<int>(round(xy.second[0]));
    for (int innerIndex : emptyIndices) {
      index.cadIndex                                                             = horizontal ? outerIndex : innerIndex;
      index.wattIndex                                                            = horizontal ? innerIndex : outerIndex;
      ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition = singleValue;
    }
  } else if (n == 2) {
    for (int innerIndex : emptyIndices) {
      index.cadIndex  = horizontal ? outerIndex : innerIndex;
      index.wattIndex = horizontal ? innerIndex : outerIndex;

      float extrapolated_value = linearExtrapolate(xy, n, innerIndex);
      int tempValue            = static_cast<int>(round(extrapolated_value));

      if (testNeighbors(index, tempValue, ptData).allNeighborsPassed) {
        ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition = tempValue;
      }
    }
  } else if (n >= 3) {
    bool validForSpline = true;
    for (size_t i = 1; i < n; ++i) {
      if (xy.first[i] <= xy.first[i - 1]) {
        validForSpline = false;
        break;
      }
    }
    if (!validForSpline) {
      SS2K_LOG(PTDATA_LOG_TAG, "Duplicate or non-increasing x-values detected!");
      return;
    }

    CubicSpline spline;
    spline.set_points(xy, n);  // Pass pointer-based data

    for (int innerIndex : emptyIndices) {
      index.cadIndex  = horizontal ? outerIndex : innerIndex;
      index.wattIndex = horizontal ? innerIndex : outerIndex;

      float extrapolated_value = spline.extrapolate(innerIndex);
      float minVal             = *std::min_element(xy.second.begin(), xy.second.end());
      float maxVal             = *std::max_element(xy.second.begin(), xy.second.end());
      float range              = maxVal - minVal;
      extrapolated_value       = std::max(minVal - 0.1f * range, std::min(extrapolated_value, maxVal + 0.1f * range));
      int tempValue            = static_cast<int>(round(extrapolated_value));

      if (testNeighbors(index, tempValue, ptData).allNeighborsPassed) {
        ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition = tempValue;
      }
    }
  }
}

void PTHelpers::extrapFillTableDirection(bool horizontal, PTData& ptData) {
  int outerSize = horizontal ? POWERTABLE_CAD_SIZE : POWERTABLE_WATT_SIZE;
  int innerSize = horizontal ? POWERTABLE_WATT_SIZE : POWERTABLE_CAD_SIZE;

  std::vector<std::pair<int, float>> unique_xy;
  std::vector<int> emptyIndices;
  std::vector<float> x, y;

  // Store previous data to reuse
  std::vector<float> prevX, prevY;
  std::vector<int> prevEmptyIndices;
  bool prevSplineValid   = false;
  bool prevNaturalSpline = false;

  for (int outerIndex = 0; outerIndex < outerSize; ++outerIndex) {
    unique_xy.clear();
    emptyIndices.clear();
    x.clear();
    y.clear();

    int rangeStart = std::max(0, innerSize / 2 - 10);
    int rangeEnd   = std::min(innerSize, innerSize / 2 + 10);
    ptIndex index;
    // Collect data points
    for (int innerIndex = rangeStart; innerIndex < rangeEnd; ++innerIndex) {
      index.cadIndex  = horizontal ? outerIndex : innerIndex;
      index.wattIndex = horizontal ? innerIndex : outerIndex;

      if (ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition != INT16_MIN) {
        unique_xy.emplace_back(innerIndex, static_cast<float>(ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition));
      } else {
        emptyIndices.push_back(innerIndex);
      }
    }

    if (unique_xy.size() < 2) continue;  // Skip if not enough data

    std::sort(unique_xy.begin(), unique_xy.end());

    for (const auto& it : unique_xy) {
      x.push_back(it.first);
      y.push_back(it.second);
    }

    if (prevSplineValid && x == prevX && y == prevY && emptyIndices == prevEmptyIndices) {
      continue;
    }

    CubicSpline spline;
    bool useNaturalSpline = spline.shouldUseNaturalSpline(std::make_pair(x, y), x.size());

    prevX             = x;
    prevY             = y;
    prevEmptyIndices  = emptyIndices;
    prevNaturalSpline = useNaturalSpline;
    prevSplineValid   = true;

    // Fill empty table entries using the determined spline type
    extrapolateEmptyIndices(outerIndex, emptyIndices, std::make_pair(x, y), x.size(), horizontal, useNaturalSpline, ptData);
  }
}

void PTHelpers::extrapolateDiagonalEntries(const std::vector<ptIndex>& emptyIndices, std::pair<std::vector<float>, std::vector<float>> xy, size_t n, PTData& ptData) {
  if (n == 1) {
    int singleValue = static_cast<int>(round(xy.second[0]));
    for (const auto& it : emptyIndices) {
      ptData.tableRow[it.cadIndex].tableEntry[it.wattIndex].targetPosition = singleValue;
    }
    return;
  }
  ptIndex index;
  if (n == 2) {
    for (const auto& it : emptyIndices) {
      index = it;

      float extrapolated_value = linearExtrapolate(xy, n, index.cadIndex);
      int tempValue            = static_cast<int>(round(extrapolated_value));

      if (testNeighbors(index, tempValue, ptData).allNeighborsPassed) {
        ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition = tempValue;
      }
    }
    return;
  }

  if (n >= 3) {
    bool validForSpline = true;
    for (size_t k = 1; k < n; ++k) {
      if (xy.first[k] <= xy.first[k - 1]) {
        validForSpline = false;
        break;
      }
    }
    if (!validForSpline) {
      SS2K_LOG(PTDATA_LOG_TAG, "Duplicate or non-increasing x-values detected for diagonal!");
      return;
    }

    CubicSpline spline;
    spline.set_points(xy, n);

    for (const auto& it : emptyIndices) {
      index = it;

      if (index.cadIndex < 0 || index.cadIndex >= POWERTABLE_CAD_SIZE || index.wattIndex < 0 || index.wattIndex >= POWERTABLE_WATT_SIZE) continue;

      float extrapolated_value = spline.extrapolate(index.cadIndex);

      float minVal       = *std::min_element(xy.second.begin(), xy.second.end());
      float maxVal       = *std::max_element(xy.second.begin(), xy.second.end());
      float range        = maxVal - minVal;
      extrapolated_value = std::max(minVal - 0.1f * range, std::min(extrapolated_value, maxVal + 0.1f * range));

      int tempValue = static_cast<int>(round(extrapolated_value));
      if (testNeighbors(index, tempValue, ptData).allNeighborsPassed) {
        ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition = tempValue;
      }
    }
  }
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
    SS2K_LOG(PTDATA_LOG_TAG, "Linear Extrapolation failed, x1 - x0 is 0");
    return INT16_MIN;
  }

  float slope = (y1 - y0) / (x1 - x0);
  return y0 + slope * (j - x0);
}

void PTHelpers::standardFill(PTData& ptData) {
  findTableDirection(true, ptData);   // Horizontal
  findTableDirection(false, ptData);  // Vertical
}

void PTHelpers::findTableDirection(bool horizontal, PTData& ptData) {
  int outerSize = horizontal ? POWERTABLE_CAD_SIZE : POWERTABLE_WATT_SIZE;
  int innerSize = horizontal ? POWERTABLE_WATT_SIZE : POWERTABLE_CAD_SIZE;

  std::vector<std::pair<int, float>> unique_xy;
  std::vector<int> emptyIndices;
  std::vector<float> x, y;

  // Get previous x/y values and empty indices to reuse if we can
  std::vector<float> prevX, prevY;
  std::vector<int> prevEmptyIndices;
  bool prevSplineValid   = false;
  bool prevNaturalSpline = false;

  for (int outerValue = 0; outerValue < outerSize; ++outerValue) {
    unique_xy.clear();
    emptyIndices.clear();
    x.clear();
    y.clear();

    int rangeStart = std::max(0, innerSize / 2 - 5);
    int rangeEnd   = std::min(innerSize, innerSize / 2 + 5);

    for (int innerValue = rangeStart; innerValue < rangeEnd; ++innerValue) {
      int i = horizontal ? outerValue : innerValue;
      int j = horizontal ? innerValue : outerValue;

      int targetPos = ptData.tableRow[i].tableEntry[j].targetPosition;
      if (targetPos != INT16_MIN) {
        unique_xy.emplace_back(innerValue, static_cast<float>(targetPos));
      } else {
        emptyIndices.push_back(innerValue);
      }
    }

    if (unique_xy.size() < 2) continue;

    std::sort(unique_xy.begin(), unique_xy.end());

    for (const auto& it : unique_xy) {
      x.push_back(it.first);
      y.push_back(it.second);
    }

    // Reuse spline if same values
    if (prevSplineValid && x == prevX && y == prevY && emptyIndices == prevEmptyIndices) {
      continue;
    }

    CubicSpline spline;
    bool useNaturalSpline = spline.shouldUseNaturalSpline(std::make_pair(x, y), x.size());

    // Store values
    prevX             = x;
    prevY             = y;
    prevEmptyIndices  = emptyIndices;
    prevNaturalSpline = useNaturalSpline;
    prevSplineValid   = true;

    fillEmptyTable(outerValue, emptyIndices, std::make_pair(x, y), x.size(), horizontal, useNaturalSpline, ptData);
  }
}

// Use the powertable to preform a reverse lookup of the target position to find the watts at a given cadence.
int32_t PTHelpers::lookupWatts(int cad, int32_t targetPosition, PTData& ptData) {
  if (cad < 1) {
    return 0;
  }

  int watts = extrapolateWattsFromCadence(cad, targetPosition/TABLE_DIVISOR, ptData);  // Extrapolate watts for the given cadence and target position
  //SS2K_LOG(PTDATA_LOG_TAG, "LookupWatts computed %dw from pos %d, cad %d", watts, targetPosition, cad);
  if (watts < 0) watts = 0;  // Ensure watts is non-negative
  return watts;
}

int PTHelpers::extrapolateWattsFromCadence(int cad, int32_t targetPosition, PTData& ptData) {
  int watts = 0;
  if (cad < 1) {
    return 0;
  }
  ptIndex index       = calculateIndex(0, cad);  // Ensure the index is calculated for the given cadence
  bool inCadenceRange = index.cadIndex >= 0 && index.cadIndex < POWERTABLE_CAD_SIZE;
  bool inWattRange    = index.wattIndex >= 0 && index.wattIndex < POWERTABLE_WATT_SIZE;
  std::pair<std::vector<float>, std::vector<float>> xyUsed4Offset;

  std::pair<std::vector<float>, std::vector<float>> xy;  // Get the row data for the given cadence

  if (true) {
    float offset = 0.0;
    // if (!inCadenceRange) {
    // std::pair<std::vector<float>, std::vector<float>> newxy;
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
      bool found = false;
      for (int j = 0; j < xyUsed4Offset.second.size(); j++) {
        if (xy.first[i] == xyUsed4Offset.first[j]) {
          offset     = (((xyUsed4Offset.second[j] - xy.second[i]) * cadDelta) + offset) / 2;
          bool found = true;
        }
      }
    }
    for (int i = 0; i < xy.first.size(); i++) {
      if (index.cadIndex < 0) {
        xy.second[i] = xy.second[i] + offset;
      }
      if (index.cadIndex > POWERTABLE_CAD_SIZE - 1) {
        xy.second[i] = xy.second[i] - offset;
      }
    }
    // SS2K_LOG(PTDATA_LOG_TAG, "LookupWatts: offset = %f", offset);
  }
  if (!inCadenceRange) {
    // print everything in xy and xyUsed4Offset
    for (int i = 0; i < xyUsed4Offset.first.size(); i++) {
      // SS2K_LOG(PTDATA_LOG_TAG, "xyUsed4Offset[%d]: %f, %f", i, xyUsed4Offset.first[i], xyUsed4Offset.second[i]);
    }
  }
  if (inCadenceRange) {
    // SS2K_LOG(PTDATA_LOG_TAG, "Cadence was in range %d", index.cadIndex);
    xy = getRow(index.cadIndex, ptData);
  }

   //print everything in xy
   for (int i = 0; i < xy.first.size(); i++) {
    Serial.printf("xy[%d]: %f, %f\n", i, xy.first[i], xy.second[i]);
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

void CubicSpline::set_points(std::pair<std::vector<float>, std::vector<float>> xy, size_t n) {
  if (n < 2) return;  // Ensure sufficient points

  x.assign(xy.first.begin(), xy.first.end());
  y.assign(xy.second.begin(), xy.second.end());

  std::vector<float> h(n - 1), alpha(n, 0.0f);
  c.resize(n, 0.0f);
  b.resize(n - 1, 0.0f);
  d.resize(n - 1, 0.0f);

  for (size_t i = 0; i < n - 1; ++i) {
    h[i] = x[i + 1] - x[i];
    if (h[i] == 0.0f) return;  // Avoid duplicate x values
  }

  for (size_t i = 1; i < n - 1; ++i) {
    alpha[i] = (3.0f / h[i]) * (y[i + 1] - y[i]) - (3.0f / h[i - 1]) * (y[i] - y[i - 1]);
  }

  float l = 1.0f, mu = 0.0f, z = 0.0f, prev_l = 1.0f, prev_z = 0.0f;

  for (size_t i = 1; i < n - 1; ++i) {
    l      = 2.0f * (x[i + 1] - x[i - 1]) - h[i - 1] * mu;
    mu     = h[i] / l;
    z      = (alpha[i] - h[i - 1] * prev_z) / l;
    prev_z = z;
    prev_l = l;
  }

  for (int j = n - 2; j >= 0; --j) {
    c[j] = z - mu * c[j + 1];
    b[j] = (y[j + 1] - y[j]) / h[j] - h[j] * (c[j + 1] + 2.0f * c[j]) / 3.0f;
    d[j] = (c[j + 1] - c[j]) / (3.0f * h[j]);
  }
}

float CubicSpline::interpolate(float x_val) const {
  if (x_val < x.front() || x_val > x.back()) {
    return INT16_MIN;  // Out of range
  }

  int i    = std::upper_bound(x.begin(), x.end(), x_val) - x.begin() - 1;
  float dx = x_val - x[i];
  return y[i] + b[i] * dx + c[i] * dx * dx + d[i] * dx * dx * dx;
}

float CubicSpline::extrapolate(float x_val) const {
  if (x_val < x.front()) {
    float dx = x_val - x[0];
    return y[0] + b[0] * dx + c[0] * dx * dx + d[0] * dx * dx * dx;
  }
  if (x_val > x.back()) {
    int n    = x.size() - 1;
    float dx = x_val - x[n];
    return y[n] + b[n - 1] * dx + c[n - 1] * dx * dx + d[n - 1] * dx * dx * dx;
  }
  return INT16_MIN;  // Out of range
}

void PTHelpers::extrapolateDiagonal(PTData& ptData) {
  std::vector<std::pair<float, float>> unique_xy;
  std::vector<ptIndex> emptyIndices;

  std::vector<float> prevX, prevY;
  std::vector<ptIndex> prevEmptyIndices;
  bool prevSplineValid = false;

  int midCAD  = POWERTABLE_CAD_SIZE / 2;
  int midWATT = POWERTABLE_WATT_SIZE / 2;

  // Iterate over different diagonals (sum of indices is constant)
  for (int sum = 0; sum < POWERTABLE_CAD_SIZE + POWERTABLE_WATT_SIZE - 1; ++sum) {
    unique_xy.clear();
    emptyIndices.clear();

    int rangeStart = std::max(0, sum / 2 - 10);
    int rangeEnd   = std::min(POWERTABLE_CAD_SIZE, sum / 2 + 10);

    // Collect known values for this diagonal
    ptIndex tIndex;
    for (tIndex.cadIndex = rangeStart; tIndex.cadIndex < rangeEnd; ++tIndex.cadIndex) {
      tIndex.wattIndex = sum - tIndex.cadIndex;
      if (tIndex.wattIndex >= 0 && tIndex.wattIndex < POWERTABLE_WATT_SIZE) {
        if (ptData.tableRow[tIndex.cadIndex].tableEntry[tIndex.wattIndex].targetPosition != INT16_MIN) {
          unique_xy.emplace_back(tIndex.cadIndex, static_cast<float>(ptData.tableRow[tIndex.cadIndex].tableEntry[tIndex.wattIndex].targetPosition));
        } else {
          ptIndex newIndex;
          newIndex.cadIndex  = tIndex.cadIndex;
          newIndex.wattIndex = tIndex.wattIndex;
          emptyIndices.push_back(newIndex);
        }
      }
    }

    if (unique_xy.size() < 2) continue;  // Skip if not enough data

    std::sort(unique_xy.begin(), unique_xy.end());

    std::vector<float> x, y;
    for (const auto& point : unique_xy) {
      x.push_back(point.first);
      y.push_back(point.second);
    }

    if (prevSplineValid && x == prevX && emptyIndices == prevEmptyIndices) {
      continue;
    }

    // Store for reuse
    prevX            = x;
    prevY            = y;
    prevEmptyIndices = emptyIndices;
    prevSplineValid  = true;

    extrapolateDiagonalEntries(emptyIndices, std::make_pair(x, y), x.size(), ptData);
  }
}

bool CubicSpline::shouldUseNaturalSpline(std::pair<std::vector<float>, std::vector<float>> xy, size_t n) {
  if (n < 3) return true;  // Default to natural spline for small data sets

  // Compute approximate first derivatives at endpoints
  float startSlope = (xy.second[1] - xy.second[0]) / (xy.first[1] - xy.first[0]);
  float endSlope   = (xy.second[n - 1] - xy.second[n - 2]) / (xy.first[n - 1] - xy.first[n - 2]);

  // Adaptive slope threshold
  float dataRange      = *std::max_element(xy.second.begin(), xy.second.end()) - *std::min_element(xy.second.begin(), xy.second.end());
  float slopeThreshold = 0.1f * dataRange;

  if (abs(startSlope) > slopeThreshold || abs(endSlope) > slopeThreshold) {
    return false;  // Use clamped spline
  }

  if (n < 4) return true;  // Not enough points for second derivative check

  // Compute second derivatives safely
  float h0 = xy.first[1] - xy.first[0], h1 = xy.first[2] - xy.first[1];
  if (h0 == 0.0f || h1 == 0.0f) return true;  // Avoid division by zero

  float secondDerivativeStart = (xy.second[2] - 2 * xy.second[1] + xy.second[0]) / (h0 * h1);

  float hn1 = xy.first[n - 2] - xy.first[n - 3], hn2 = xy.first[n - 1] - xy.first[n - 2];
  if (hn1 == 0.0f || hn2 == 0.0f) return true;  // Avoid division by zero

  float secondDerivativeEnd = (xy.second[n - 1] - 2 * xy.second[n - 2] + xy.second[n - 3]) / (hn1 * hn2);

  float curvatureThreshold = 1.0f;
  return !(abs(secondDerivativeStart) > curvatureThreshold || abs(secondDerivativeEnd) > curvatureThreshold);
}