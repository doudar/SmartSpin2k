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
 * @brief Extrapolates and fills empty indices in a power table row or column using cubic spline interpolation and neighbor blending.
 * returns false if the operation takes too long.
 *
 * This function takes a set of empty indices within a row or column of a power table and attempts to extrapolate their values
 * based on existing (x, y) data points using cubic spline interpolation. The extrapolated value is then blended with nearby
 * valid neighbors to ensure smoothness and avoid abrupt transitions. The function also ensures that the extrapolated values
 * remain within a reasonable range, slightly extended beyond the min/max of the known values. Neighbor blending uses a
 * distance-based decay to weight closer neighbors more heavily.
 *
 * @param outerIndex The index of the row or column being processed, depending on the orientation.
 * @param emptyIndices The indices within the row or column that are empty and need to be extrapolated.
 * @param xy A pair of vectors representing the known (x, y) data points for interpolation.
 * @param n The number of valid (x, y) data points.
 * @param horizontal If true, extrapolation is performed horizontally (across columns); otherwise, vertically (across rows).
 * @param naturalSpline If true, use a natural cubic spline for interpolation (currently unused in this function).
 * @param ptData Reference to the power table data structure to be updated with extrapolated values.
 */
bool PTHelpers::extrapolateEmptyIndices(int outerIndex, const std::vector<int>& emptyIndices, std::pair<std::vector<float>, std::vector<float>> xy, size_t n, bool horizontal,
                                        bool naturalSpline, PTData& ptData) {
  unsigned long timeout = millis() + COMPUTATION_TIMEOUT_MS;  // Set timeout for computation
  ptIndex index;
  if (n >= 3) {
    bool validForSpline = true;
    for (size_t i = 1; i < n; ++i) {
      if (xy.first[i] <= xy.first[i - 1]) {
        validForSpline = false;
        break;
      }
    }
    if (!validForSpline) {
      SS2K_LOG(PTDATA_LOG_TAG, "Duplicate or non-increasing x-values detected!");
      return true;
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
      int tempValue            = (ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition == INT16_MIN)
                                     ? round(extrapolated_value)
                                     : round((extrapolated_value + ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition) / 2.0f);

      if (testNeighbors(index, tempValue, ptData).allNeighborsPassed) {
        // Blend with nearby valid neighbors to ensure smoothness
        float blendedValue = tempValue;
        float totalWeight  = 1.0f;  // Start with original value

        // Check horizontal and vertical neighbors within 2 steps
        const int blendRadius     = 2;
        const float distanceDecay = 0.7f;  // Weight decreases with distance

        for (int di = -blendRadius; di <= blendRadius; di++) {
          for (int dj = -blendRadius; dj <= blendRadius; dj++) {
            // Skip self
            if (di == 0 && dj == 0) continue;

            int ni = index.cadIndex + (horizontal ? 0 : di);
            int nj = index.wattIndex + (horizontal ? di : 0);

            // Check if neighbor is valid
            if (ni >= 0 && ni < POWERTABLE_CAD_SIZE && nj >= 0 && nj < POWERTABLE_WATT_SIZE) {
              int16_t neighborVal = ptData.tableRow[ni].tableEntry[nj].targetPosition;

              if (neighborVal != INT16_MIN) {
                // Calculate distance-based weight
                float distance = std::fabs(di) + std::fabs(dj);  // Manhattan distance
                float weight   = std::pow(distanceDecay, distance);

                // Add to weighted average
                blendedValue += neighborVal * weight;
                totalWeight += weight;
              }
            }
          }
        }

        // Compute final blended value if we found neighbors
        if (totalWeight > 1.0f) {
          // More weight to original spline value (70%) for trend preservation
          float originalWeight = 0.7f;
          float neighborWeight = 1.0f - originalWeight;

          blendedValue = (originalWeight * tempValue + neighborWeight * (blendedValue - tempValue) / (totalWeight - 1.0f));
        }
        ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition = static_cast<int16_t>(round(blendedValue));
        if (millis() > timeout) {
          SS2K_LOG(PTDATA_LOG_TAG, "Spline fill operation timed out!");
          return false;  // Exit if computation takes too long
        }
      }
    }
  }
  return true;
}

/**
 * @brief Fills missing or invalid entries in the power table by estimating values using averages.
 *
 * This function processes the provided PTData structure, which represents a power table with
 * cadence and wattage dimensions. It performs the following steps:
 * 1. Calculates the average target position for each wattage column across all valid cadence rows.
 * 2. Computes the average increase in target position per cadence increment for each wattage column.
 * 3. For each empty or invalid cell, estimates its value based on the center average and the average
 *    increase per cadence step, filling in the missing data accordingly.
 *
 * If a cell was previously filled by another method, the function averages the new estimate with the
 * existing value to provide a smoother result.
 *
 * @param ptData Reference to the PTData structure containing the power table to be filled.
 */
void PTHelpers::fillByAverage(PTData& ptData) {
  std::vector<float> centerAverageRow(POWERTABLE_WATT_SIZE, 0.0f);
  std::vector<int> validCounts(POWERTABLE_WATT_SIZE, 0);
  int mid_cad_index = POWERTABLE_CAD_SIZE / 2;
  float center_cad = static_cast<float>(MINIMUM_TABLE_CAD + mid_cad_index * POWERTABLE_CAD_INCREMENT);

  // 1. Calculate the center average row using improved regression
  for (int j = 0; j < POWERTABLE_WATT_SIZE; ++j) {
    std::vector<std::pair<float, float>> points;
    for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
      if (ptData.tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
        float cad = static_cast<float>(MINIMUM_TABLE_CAD + i * POWERTABLE_CAD_INCREMENT);
        points.push_back({cad, static_cast<float>(ptData.tableRow[i].tableEntry[j].targetPosition)});
      }
    }
    validCounts[j] = points.size();

    if (points.size() >= 2) {
      // Check if data is clustered far from center
      float min_cad = points.front().first;
      float max_cad = points.back().first;
      float cad_range = max_cad - min_cad;
      float distance_from_center = (std::min)((std::abs)(center_cad - min_cad), (std::abs)(center_cad - max_cad));
      
      // If data is too far from center or range is too small, use neighbor interpolation instead
      if (distance_from_center > 20.0f || cad_range < 10.0f) {
        // Try to interpolate from neighboring power levels
        float neighborAvg = 0.0f;
        int neighborCount = 0;
        
        // Check left and right neighbors
        for (int neighbor_j = std::max(0, j-2); neighbor_j <= std::min(POWERTABLE_WATT_SIZE-1, j+2); neighbor_j++) {
          if (neighbor_j != j && validCounts[neighbor_j] > 0) {
            // Get neighbor's data and check if it has data near center
            std::vector<std::pair<float, float>> neighborPoints;
            for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
              if (ptData.tableRow[i].tableEntry[neighbor_j].targetPosition != INT16_MIN) {
                float cad = static_cast<float>(MINIMUM_TABLE_CAD + i * POWERTABLE_CAD_INCREMENT);
                neighborPoints.push_back({cad, static_cast<float>(ptData.tableRow[i].tableEntry[neighbor_j].targetPosition)});
              }
            }
            
            if (neighborPoints.size() >= 2) {
              // Calculate neighbor's center value
              float sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
              for (const auto& p : neighborPoints) {
                sum_x += p.first;
                sum_y += p.second;
                sum_xy += p.first * p.second;
                sum_x2 += p.first * p.first;
              }
              float n = neighborPoints.size();
              float denom = (n * sum_x2 - sum_x * sum_x);
              if (denom != 0) {
                float slope = (n * sum_xy - sum_x * sum_y) / denom;
                float intercept = (sum_y - slope * sum_x) / n;
                float neighborCenterValue = slope * center_cad + intercept;
                
                // Weight by proximity and data quality
                float weight = 1.0f / (1.0f + (std::abs)(neighbor_j - j));
                neighborAvg += neighborCenterValue * weight;
                neighborCount += weight;
              }
            }
          }
        }
        
        if (neighborCount > 0) {
          centerAverageRow[j] = neighborAvg / neighborCount;
        } else {
          // Fallback to simple average of available points
          float sum = 0;
          for (const auto& p : points) sum += p.second;
          centerAverageRow[j] = sum / points.size();
        }
      } else {
        // Use normal linear regression for well-distributed data
        float sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
        for (const auto& p : points) {
          sum_x += p.first;
          sum_y += p.second;
          sum_xy += p.first * p.second;
          sum_x2 += p.first * p.first;
        }
        float n = points.size();
        float denom = (n * sum_x2 - sum_x * sum_x);
        float slope = (denom != 0) ? (n * sum_xy - sum_x * sum_y) / denom : 0;
        float intercept = (sum_y - slope * sum_x) / n;
        centerAverageRow[j] = slope * center_cad + intercept;
      }
    } else if (points.size() == 1) {
      centerAverageRow[j] = points[0].second;
    }
  }
  
  // 2. Calculate the average increase in target position per cadence increment for each column
  std::vector<float> averageIncreasePerCadence(POWERTABLE_WATT_SIZE, 0.0f);
  for (int j = 0; j < POWERTABLE_WATT_SIZE; ++j) {
    float totalIncrease = 0.0f;
    int increaseCount   = 0;
    for (int i = 0; i < POWERTABLE_CAD_SIZE - 1; ++i) {
      if (ptData.tableRow[i].tableEntry[j].targetPosition != INT16_MIN && ptData.tableRow[i + 1].tableEntry[j].targetPosition != INT16_MIN) {
        totalIncrease += (ptData.tableRow[i + 1].tableEntry[j].targetPosition - ptData.tableRow[i].tableEntry[j].targetPosition);
        increaseCount++;
      }
    }
    if (increaseCount > 0) {
      averageIncreasePerCadence[j] = totalIncrease / increaseCount;
    }
  }

  // 3. Fill empty cells based on the average row and average increase
  for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; ++j) {
      // Only fill cells that have not been populated with real data
      if (ptData.tableRow[i].tableEntry[j].readings < 1) {
        if (validCounts[j] > 0) {  // Only fill if there was data in this column to create an average
          int distance      = i - mid_cad_index;
          float newValue    = centerAverageRow[j] + (averageIncreasePerCadence[j] * distance);
          int16_t intNewValue = static_cast<int16_t>(round(newValue));

          // If the cell was already filled by another method, average with the new value
          if (ptData.tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
            ptData.tableRow[i].tableEntry[j].targetPosition = round((ptData.tableRow[i].tableEntry[j].targetPosition + intNewValue) / 2.0f);
          } else {
            ptData.tableRow[i].tableEntry[j].targetPosition = intNewValue;
          }
        }
      }
    }
  }
}

/**
 * @brief Fills missing entries in the power table using cubic spline interpolation.
 *
 * This function processes either the first or second half of the table rows or columns,
 * depending on the `firstHalf` parameter, and interpolates missing values (marked by INT16_MIN)
 * using cubic splines. The interpolation is performed either horizontally (across watt indices)
 * or vertically (across cadence indices) as determined by the `horizontal` parameter.
 * The function ensures overlap between the two halves to provide smooth transitions.
 *
 * @param ptData      Reference to the power table data structure to be filled.
 * @param firstHalf   (optional) If true, processes the first half (with overlap); otherwise, processes the second half. Default is true.
 * @param horizontal  (optional) If true, fills across watt indices (rows); if false, fills across cadence indices (columns). Default is true.
 */
bool PTHelpers::splineFill(PTData& ptData, bool firstHalf /*= true*/, bool horizontal /*= true*/) {
  bool completedWithoutTimeout = true;
  int outerSize                = horizontal ? POWERTABLE_CAD_SIZE : POWERTABLE_WATT_SIZE;
  int innerSize                = horizontal ? POWERTABLE_WATT_SIZE : POWERTABLE_CAD_SIZE;

  std::vector<std::pair<int, float>> unique_xy;
  std::vector<int> emptyIndices;
  std::vector<float> x, y;
  int rangeStart, rangeEnd;

  // Determine the range for inner loop based on firstHalf or secondHalf
  // This logic for 'rangeStart' and 'rangeEnd' applies to the 'innerIndex' loop
  if (firstHalf) {
    rangeStart = 0;
    // Ensure overlap: process a bit more than half, e.g., 2/3 or 3/4
    // The amount of overlap might need tuning. Let's try 2/3 for now.
    rangeEnd = (innerSize * 2) / 3;
    if (rangeEnd > innerSize) rangeEnd = innerSize;  // cap at innerSize
  } else {
    // Start from a point that ensures overlap with the first half
    rangeStart = innerSize / 3;
    rangeEnd   = innerSize;
  }

  int mid_outer   = outerSize / 2;
  int max_k_outer = 0;
  if (outerSize > 0) {
    max_k_outer = std::max(mid_outer, (outerSize - 1) - mid_outer);
  }

  auto processOuterIndex = [&](int currentOuterIndex) {
    unique_xy.clear();
    emptyIndices.clear();
    x.clear();
    y.clear();

    ptIndex index;
    // Collect data points for the currentOuterIndex across the specified inner range
    for (int innerIndex = rangeStart; innerIndex < rangeEnd; ++innerIndex) {
      index.cadIndex  = horizontal ? currentOuterIndex : innerIndex;
      index.wattIndex = horizontal ? innerIndex : currentOuterIndex;

      // Boundary checks for safety, though currentOuterIndex should be valid by loop logic
      if (index.cadIndex < 0 || index.cadIndex >= POWERTABLE_CAD_SIZE || index.wattIndex < 0 || index.wattIndex >= POWERTABLE_WATT_SIZE) {
        continue;
      }

      // Gather valid data points. Exclude points with 0 readings and a valid targetPosition - those are previously interpolated points.
      if (ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings > 0 && ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition != INT16_MIN) {
        unique_xy.emplace_back(innerIndex, static_cast<float>(ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition));
      } else {
        emptyIndices.push_back(innerIndex);
      }
    }

    if (unique_xy.size() < 2) return;  // Skip if not enough data

    std::sort(unique_xy.begin(), unique_xy.end());

    for (const auto& it : unique_xy) {
      x.push_back(it.first);
      y.push_back(it.second);
    }

    CubicSpline spline;
    // The decision to use natural spline might be based on the characteristics of x, y
    // For now, let's assume shouldUseNaturalSpline is available and correctly implemented
    bool useNaturalSpline = spline.shouldUseNaturalSpline(std::make_pair(x, y), x.size());

    // Fill empty table entries using the determined spline type
    completedWithoutTimeout = extrapolateEmptyIndices(currentOuterIndex, emptyIndices, std::make_pair(x, y), x.size(), horizontal, useNaturalSpline, ptData);
  };

  for (int k = 0; k <= max_k_outer; ++k) {
    if (!completedWithoutTimeout) {
      return false;  // Exit if computation takes too long
    }
    int outer_upper = mid_outer + k;
    if (outer_upper < outerSize) {
      processOuterIndex(outer_upper);
    }

    if (k > 0) {
      int outer_lower = mid_outer - k;
      if (outer_lower >= 0) {
        processOuterIndex(outer_lower);
      }
    }
  }
  return completedWithoutTimeout;  // Return true if all operations completed without timeout
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
 * @brief Fills empty entries in the power table using linear interpolation.
 *
 * This function iterates through the table rows, starting from the middle row and expanding outward,
 * and fills empty cells (where targetPosition == INT16_MIN) by estimating resistance using the lookup method.
 * Only fills a cell if all its neighbors pass validation.
 *
 * @param ptData Reference to the PTData object containing the power table.
 */
bool PTHelpers::linearFill(PTData& ptData) {
  int mid   = POWERTABLE_CAD_SIZE / 2;
  int max_k = (POWERTABLE_CAD_SIZE - 1) - mid;

  // Define a lambda for processing a single cell
  auto processCell = [this, &ptData](int currentRowIndex, int currentColIndex) {
    if (ptData.tableRow[currentRowIndex].tableEntry[currentColIndex].readings == 0) {
      int watts      = currentColIndex * POWERTABLE_WATT_INCREMENT;
      int cad        = currentRowIndex * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD;
      int resistance = this->lookup(watts, cad, ptData);
      if (resistance == RETURN_ERROR) {
        // SS2K_LOG(PTDATA_LOG_TAG, "Failed to lookup resistance for watts: %d, cadence: %d", watts, cad);
        return;  // Skip this cell processing
      } else {
        resistance = resistance / TABLE_DIVISOR;
      }
      ptIndex current_pt_idx;
      current_pt_idx.wattIndex = currentColIndex;
      current_pt_idx.cadIndex  = currentRowIndex;
      TestResults results      = this->testNeighbors(current_pt_idx, resistance, ptData);
      if (results.allNeighborsPassed == 1) {
        ptData.tableRow[currentRowIndex].tableEntry[currentColIndex].targetPosition =
            (ptData.tableRow[currentRowIndex].tableEntry[currentColIndex].targetPosition == INT16_MIN)
                ? resistance
                : round((ptData.tableRow[currentRowIndex].tableEntry[currentColIndex].targetPosition + resistance) / 2.0f);
        // SS2K_LOG(PTDATA_LOG_TAG, "Filled position (%d, %d) with resistance: %d", currentRowIndex, currentColIndex, resistance);
      } else {  // log the failure to in insert the value
                // Serial.printf("Failed to fill position (%d, %d) with resistance: %d\n", currentRowIndex, currentColIndex, resistance);
      }
    }
  };

  for (int k = 0; k <= max_k; ++k) {
    // Process row from mid upwards: mid, mid+1, mid+2, ...
    int i_upper = mid + k;
    if (i_upper < POWERTABLE_CAD_SIZE) {  // Ensure i_upper is a valid row index
      for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
        processCell(i_upper, j);
      }
    }

    // Process row from mid downwards: mid-1, mid-2, ..., only if k > 0 to avoid re-processing 'mid'
    if (k > 0) {
      int i_lower = mid - k;
      if (i_lower >= 0) {  // Ensure i_lower is a valid row index
        for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
          processCell(i_lower, j);
        }
      }
    }
  }
  return true;  // Return true if all operations completed without timeout
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