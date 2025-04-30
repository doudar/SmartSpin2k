/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "PowerTable_Helpers.h"

//if building PLATFORMIO_ENV_NATIVE environment define SS2K_LOG as Serial.printf(), else include the SS2KLog.h.
#ifdef PLATFORMIO_ENV_NATIVE
#define SS2K_LOG(tag, format, ...) Serial.printf(format, __VA_ARGS__)
#else
#include "SS2KLog.h"
#endif

TestResults PTHelpers::testNeighbors(int i, int j, int testValue, PTData& ptData) {
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
  } directions[] = {// Left: decreasing j, same i
                    {j > 0 ? j - 1 : -1, -1, -1, 0, 0, &returnResult.leftNeighbor, [](int16_t pos, int test) { return pos < test || pos == INT16_MIN; }},
                    // Right: increasing j, same i
                    {j < POWERTABLE_WATT_SIZE - 1 ? j + 1 : POWERTABLE_WATT_SIZE, POWERTABLE_WATT_SIZE, 1, 0, 0, &returnResult.rightNeighbor,
                     [](int16_t pos, int test) { return pos > test || pos == INT16_MIN; }},
                    // Top: decreasing i, same j
                    {i > 0 ? i - 1 : -1, -1, -1, 1, 0, &returnResult.topNeighbor, [](int16_t pos, int test) { return pos > test || pos == INT16_MIN; }},
                    // Bottom: increasing i, same j
                    {i < POWERTABLE_CAD_SIZE - 1 ? i + 1 : POWERTABLE_CAD_SIZE, POWERTABLE_CAD_SIZE, 1, 1, 0, &returnResult.bottomNeighbor,
                     [](int16_t pos, int test) { return pos < test || pos == INT16_MIN; }}};

  // Process each direction
  for (const auto& dir : directions) {
    // Skip if outside bounds
    if (dir.startLimit == -1 || dir.startLimit == POWERTABLE_WATT_SIZE || dir.startLimit == POWERTABLE_CAD_SIZE) {
      continue;
    }

    // Search for neighbor in this direction
    for (int idx = dir.startLimit; idx != dir.endLimit; idx += dir.step) {
      int row = dir.rowChange ? idx : i;
      int col = dir.rowChange ? j : idx;

      if (ptData.tableRow[row].tableEntry[col].targetPosition != INT16_MIN) {
        dir.neighbor->targetPosition = ptData.tableRow[row].tableEntry[col].targetPosition;
        dir.neighbor->i              = row;
        dir.neighbor->j              = col;
        dir.neighbor->found          = 1;
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

int32_t PTHelpers::lookup(int watts, int cad, PTData& ptData) {
  int cadIndex  = round(((float)cad - (float)MINIMUM_TABLE_CAD) / (float)POWERTABLE_CAD_INCREMENT);
  int wattIndex = round((float)watts / (float)POWERTABLE_WATT_INCREMENT);

  // If request is outside table limits, perform linear extrapolation
  if (cad < MINIMUM_TABLE_CAD || cad > (MINIMUM_TABLE_CAD + (POWERTABLE_CAD_SIZE - 1) * POWERTABLE_CAD_INCREMENT) ||
      watts > (POWERTABLE_WATT_SIZE - 1) * POWERTABLE_WATT_INCREMENT) {
    int extrapolatedValue = INT32_MIN;

    // Cadence extrapolation
    if (cad < MINIMUM_TABLE_CAD || cad > (MINIMUM_TABLE_CAD + (POWERTABLE_CAD_SIZE - 1) * POWERTABLE_CAD_INCREMENT)) {
      std::vector<float> cadValue;       // cadence value
      std::vector<float> positionValue;  // target position value

      for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
        if (ptData.tableRow[i].tableEntry[wattIndex].targetPosition != INT16_MIN) {
          cadValue.push_back(static_cast<float>(i * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD));
          positionValue.push_back(static_cast<float>(ptData.tableRow[i].tableEntry[wattIndex].targetPosition));
        }
      }

      if (cadValue.size() >= 2) {
        extrapolatedValue = static_cast<int>(linearExtrapolate(cadValue.data(), positionValue.data(), cadValue.size(), static_cast<float>(cad)));
        SS2K_LOG(POWERTABLE_LOG_TAG, "Lookup Extrapolated (Cadence) (%d) for (%dw) (%dcad)", extrapolatedValue, watts, cad);
        return extrapolatedValue * TABLE_DIVISOR;
      } else {
        SS2K_LOG(POWERTABLE_LOG_TAG, "Not enough data to extrapolate cadence for (%dw) (%dcad)", watts, cad);
      }
    }

    // Watt extrapolation
    if (watts > (POWERTABLE_WATT_SIZE - 1) * POWERTABLE_WATT_INCREMENT) {
      std::vector<float> wattValue;      // watt value
      std::vector<float> positionValue;  // target position value

      if (cadIndex >= 0 && cadIndex < POWERTABLE_CAD_SIZE) {
        for (int j = 0; j < POWERTABLE_WATT_SIZE; ++j) {
          if (ptData.tableRow[cadIndex].tableEntry[j].targetPosition != INT16_MIN) {
            wattValue.push_back(static_cast<float>(j * POWERTABLE_WATT_INCREMENT));
            positionValue.push_back(static_cast<float>(ptData.tableRow[cadIndex].tableEntry[j].targetPosition));
          }
        }

        if (wattValue.size() >= 2) {
          extrapolatedValue = static_cast<int>(linearExtrapolate(wattValue.data(), positionValue.data(), wattValue.size(), static_cast<float>(watts)));  // watts as float
          SS2K_LOG(POWERTABLE_LOG_TAG, "Lookup Extrapolated (Watts) (%d) for (%dw) (%dcad)", extrapolatedValue, watts, cad);
          return extrapolatedValue * TABLE_DIVISOR;
        } else {
          SS2K_LOG(POWERTABLE_LOG_TAG, "Not enough data to extrapolate watts for (%dw) (%dcad)", watts, cad);
        }
      } else {
        SS2K_LOG(POWERTABLE_LOG_TAG, "Cadence index out of bounds for watt extrapolation at (%dw) (%dcad)", watts, cad);
      }
    }

    return INT32_MIN;  // Not enough data for extrapolation
  }

  // **Interpolation using Nearest Neighbors**
  TestResults neighbors = testNeighbors(cadIndex, wattIndex, INT16_MIN, ptData);

  float xWatt[2];
  float yWatt[2];

  if (neighbors.leftNeighbor.found && neighbors.rightNeighbor.found) {
    xWatt[0] = static_cast<float>(neighbors.leftNeighbor.j * POWERTABLE_WATT_INCREMENT);   // Watts as float
    xWatt[1] = static_cast<float>(neighbors.rightNeighbor.j * POWERTABLE_WATT_INCREMENT);  // Watts as float
    yWatt[0] = static_cast<float>(neighbors.leftNeighbor.targetPosition);                  // targetPosition as float
    yWatt[1] = static_cast<float>(neighbors.rightNeighbor.targetPosition);                 // targetPosition as float
  } else {
    xWatt[0] = xWatt[1] = 0.0f;       // Dummy values
    yWatt[0] = yWatt[1] = INT16_MIN;  // Indicate not found
  }

  float R1 = (neighbors.leftNeighbor.found && neighbors.rightNeighbor.found) ? linearInterpolate(xWatt, yWatt, 2, static_cast<float>(watts))  // watts as float
                                                                             : INT16_MIN;

  float xCad[2];
  float yCad[2];

  if (neighbors.topNeighbor.found && neighbors.bottomNeighbor.found) {
    xCad[0] = static_cast<float>(neighbors.topNeighbor.i * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD);     // Cadence as float
    xCad[1] = static_cast<float>(neighbors.bottomNeighbor.i * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD);  // Cadence as float
    yCad[0] = static_cast<float>(neighbors.topNeighbor.targetPosition);                                       // targetPosition as float
    yCad[1] = static_cast<float>(neighbors.bottomNeighbor.targetPosition);                                    // targetPosition as float
  } else {
    xCad[0] = xCad[1] = 0.0f;       // Dummy values
    yCad[0] = yCad[1] = INT16_MIN;  // Indicate not found
  }

  float R2 = (neighbors.topNeighbor.found && neighbors.bottomNeighbor.found) ? linearInterpolate(xCad, yCad, 2, static_cast<float>(cad))  // cad as float
                                                                             : INT16_MIN;

  float R3 = (cadIndex >= 0 && cadIndex < POWERTABLE_CAD_SIZE && wattIndex >= 0 && wattIndex < POWERTABLE_WATT_SIZE &&
              ptData.tableRow[cadIndex].tableEntry[wattIndex].targetPosition != INT16_MIN)
                 ? static_cast<float>(ptData.tableRow[cadIndex].tableEntry[wattIndex].targetPosition)  // Direct value as float
                 : INT16_MIN;

  float sum = 0;
  int count = 0;
  if (R1 != INT16_MIN) sum += R1, count++;
  if (R2 != INT16_MIN) sum += R2, count++;
  if (R3 != INT16_MIN) sum += R3, count++;

  if (count == 0) return INT16_MIN;

  int ret = static_cast<int>(round(sum / count)) * TABLE_DIVISOR;
  SS2K_LOG(POWERTABLE_LOG_TAG, "Lookup result: (%dw) (%dcad) (%d) (R1:%.2f, R2:%.2f, R3:%.2f)", watts, cad, ret, R1, R2, R3);
  return ret;
}

void PTHelpers::fillEmptyTable(int outerValue, const std::vector<int>& emptyIndices, const float* x, const float* y, size_t n, bool horizontal, bool useNaturalSpline,
                               PTData& ptData) {
  int innerSize = horizontal ? POWERTABLE_WATT_SIZE : POWERTABLE_CAD_SIZE;

  if (n == 1) {  // If only one point, fill row with the value
    float singleValue = y[0];
    for (int innerValue : emptyIndices) {
      int i                                           = horizontal ? outerValue : innerValue;
      int j                                           = horizontal ? innerValue : outerValue;
      ptData.tableRow[i].tableEntry[j].targetPosition = static_cast<int>(std::round(singleValue));
    }
  } else if (n == 2) {  // If two points, do linear interpolation
    for (int innerValue : emptyIndices) {
      int i = horizontal ? outerValue : innerValue;
      int j = horizontal ? innerValue : outerValue;

      float interpolated_value = linearInterpolate(x, y, n, innerValue);
      int tempValue            = static_cast<int>(std::round(interpolated_value));

      if (testNeighbors(i, j, tempValue, ptData).allNeighborsPassed) {
        ptData.tableRow[i].tableEntry[j].targetPosition = tempValue;
      }
    }
  } else if (n >= 3) {  // If three or more points, use cubic spline interpolation
    bool validForSpline = true;
    for (size_t i = 1; i < n; ++i) {
      if (x[i] <= x[i - 1]) {
        validForSpline = false;
        break;
      }
    }
    if (!validForSpline) {
      SS2K_LOG(POWERTABLE_LOG_TAG, "Duplicate or non-increasing x-values detected!");
      return;
    }

    // Create and initialize the spline with the desired type (natural or clamped)
    CubicSpline spline;
    spline.set_points(x, y, n);

    for (int innerValue : emptyIndices) {
      int i = horizontal ? outerValue : innerValue;
      int j = horizontal ? innerValue : outerValue;

      float interpolated_value = spline.interpolate(innerValue);

      float minValue     = *std::min_element(y, y + n);
      float maxValue     = *std::max_element(y, y + n);
      interpolated_value = std::max(minValue, std::min(maxValue, interpolated_value));

      int tempValue = static_cast<int>(std::round(interpolated_value));

      if (this->testNeighbors(i, j, tempValue, ptData).allNeighborsPassed) {
        ptData.tableRow[i].tableEntry[j].targetPosition = tempValue;
      }
    }
  } else {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Error: No unique points found.");
  }
}

void PTHelpers::extrapolateEmptyIndices(int outerIndex, const std::vector<int>& emptyIndices, const float* x, const float* y, size_t n, bool horizontal, bool naturalSpline,
                                        PTData& ptData) {
  int innerSize = horizontal ? POWERTABLE_WATT_SIZE : POWERTABLE_CAD_SIZE;

  if (n == 1) {
    int singleValue = static_cast<int>(std::round(y[0]));
    for (int innerIndex : emptyIndices) {
      int i                                           = horizontal ? outerIndex : innerIndex;
      int j                                           = horizontal ? innerIndex : outerIndex;
      ptData.tableRow[i].tableEntry[j].targetPosition = singleValue;
    }
  } else if (n == 2) {
    for (int innerIndex : emptyIndices) {
      int i = horizontal ? outerIndex : innerIndex;
      int j = horizontal ? innerIndex : outerIndex;

      float extrapolated_value = linearExtrapolate(x, y, n, innerIndex);
      int tempValue            = static_cast<int>(std::round(extrapolated_value));

      if (testNeighbors(i, j, tempValue, ptData).allNeighborsPassed) {
        ptData.tableRow[i].tableEntry[j].targetPosition = tempValue;
      }
    }
  } else if (n >= 3) {
    bool validForSpline = true;
    for (size_t i = 1; i < n; ++i) {
      if (x[i] <= x[i - 1]) {
        validForSpline = false;
        break;
      }
    }
    if (!validForSpline) {
      SS2K_LOG(POWERTABLE_LOG_TAG, "Duplicate or non-increasing x-values detected!");
      return;
    }

    CubicSpline spline;
    spline.set_points(x, y, n);  // Pass pointer-based data

    for (int innerIndex : emptyIndices) {
      int i = horizontal ? outerIndex : innerIndex;
      int j = horizontal ? innerIndex : outerIndex;

      float extrapolated_value = spline.extrapolate(innerIndex);
      float minVal             = *std::min_element(y, y + n);
      float maxVal             = *std::max_element(y, y + n);
      float range              = maxVal - minVal;
      extrapolated_value       = std::max(minVal - 0.1f * range, std::min(extrapolated_value, maxVal + 0.1f * range));
      int tempValue            = static_cast<int>(std::round(extrapolated_value));

      if (testNeighbors(i, j, tempValue, ptData).allNeighborsPassed) {
        ptData.tableRow[i].tableEntry[j].targetPosition = tempValue;
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

    // Collect data points
    for (int innerIndex = rangeStart; innerIndex < rangeEnd; ++innerIndex) {
      int i = horizontal ? outerIndex : innerIndex;
      int j = horizontal ? innerIndex : outerIndex;

      if (ptData.tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
        unique_xy.emplace_back(innerIndex, static_cast<float>(ptData.tableRow[i].tableEntry[j].targetPosition));
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
    bool useNaturalSpline = spline.shouldUseNaturalSpline(x.data(), y.data(), x.size());

    prevX             = x;
    prevY             = y;
    prevEmptyIndices  = emptyIndices;
    prevNaturalSpline = useNaturalSpline;
    prevSplineValid   = true;

    // Fill empty table entries using the determined spline type
    extrapolateEmptyIndices(outerIndex, emptyIndices, x.data(), y.data(), x.size(), horizontal, useNaturalSpline, ptData);
  }
}

void PTHelpers::extrapolateDiagonalEntries(const std::vector<std::pair<int, int>>& emptyIndices, const float* x, const float* y, size_t n, PTData& ptData) {
  if (n == 1) {
    int singleValue = static_cast<int>(std::round(y[0]));
    for (const auto& it : emptyIndices) {
      ptData.tableRow[it.first].tableEntry[it.second].targetPosition = singleValue;
    }
    return;
  }

  if (n == 2) {
    for (const auto& it : emptyIndices) {
      int i = it.first;
      int j = it.second;

      float extrapolated_value = linearExtrapolate(x, y, n, i);
      int tempValue            = static_cast<int>(std::round(extrapolated_value));

      if (testNeighbors(i, j, tempValue, ptData).allNeighborsPassed) {
        ptData.tableRow[i].tableEntry[j].targetPosition = tempValue;
      }
    }
    return;
  }

  if (n >= 3) {
    bool validForSpline = true;
    for (size_t k = 1; k < n; ++k) {
      if (x[k] <= x[k - 1]) {
        validForSpline = false;
        break;
      }
    }
    if (!validForSpline) {
      SS2K_LOG(POWERTABLE_LOG_TAG, "Duplicate or non-increasing x-values detected for diagonal!");
      return;
    }

    CubicSpline spline;
    spline.set_points(x, y, n);

    for (const auto& it : emptyIndices) {
      int i = it.first;
      int j = it.second;

      if (i < 0 || i >= POWERTABLE_CAD_SIZE || j < 0 || j >= POWERTABLE_WATT_SIZE) continue;

      float extrapolated_value = spline.extrapolate(i);

      float minVal       = *std::min_element(y, y + n);
      float maxVal       = *std::max_element(y, y + n);
      float range        = maxVal - minVal;
      extrapolated_value = std::max(minVal - 0.1f * range, std::min(extrapolated_value, maxVal + 0.1f * range));

      int tempValue = static_cast<int>(std::round(extrapolated_value));
      if (testNeighbors(i, j, tempValue, ptData).allNeighborsPassed) {
        ptData.tableRow[i].tableEntry[j].targetPosition = tempValue;
      }
    }
  }
}

float PTHelpers::linearExtrapolate(const float* x, const float* y, size_t n, float j) {
  float x0, x1, y0, y1;

  if (j < x[0]) {
    x0 = x[0], x1 = x[1];
    y0 = y[0], y1 = y[1];
  } else if (j > x[n - 1]) {
    x0 = x[n - 2], x1 = x[n - 1];
    y0 = y[n - 2], y1 = y[n - 1];
  } else {
    auto upper = std::upper_bound(x, x + n, j);
    auto lower = upper - 1;
    x0 = *lower, x1 = *upper;
    y0 = y[lower - x], y1 = y[upper - x];
  }

  if (x1 - x0 == 0) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Linear Extrapolation failed, x1 - x0 is 0");
    return INT16_MIN;
  }

  float slope = (y1 - y0) / (x1 - x0);
  return y0 + slope * (j - x0);
}

void PTHelpers::fillTable(PTData& ptData) {
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
    bool useNaturalSpline = spline.shouldUseNaturalSpline(x.data(), y.data(), x.size());

    // Store values
    prevX             = x;
    prevY             = y;
    prevEmptyIndices  = emptyIndices;
    prevNaturalSpline = useNaturalSpline;
    prevSplineValid   = true;

    fillEmptyTable(outerValue, emptyIndices, x.data(), y.data(), x.size(), horizontal, useNaturalSpline, ptData);
  }
}

int32_t PTHelpers::lookupWatts(int cad, int32_t targetPosition, PTData& ptData) {
  if (cad < 1) {
    return 0;
  }
  // Convert targetPosition from external format (xTABLE_DIVISOR) to internal format
  float internalPosition = targetPosition / TABLE_DIVISOR;

  // Calculate cadence index
  int cadIndex = round(((float)cad - (float)MINIMUM_TABLE_CAD) / (float)POWERTABLE_CAD_INCREMENT);

  // Clamp cadence index to table limits
  if (cadIndex < 0) {
    cadIndex = 0;
  } else if (cadIndex >= POWERTABLE_CAD_SIZE) {
    cadIndex = POWERTABLE_CAD_SIZE - 1;
  }

  // Find closest positions and corresponding watts in the row
  std::vector<float> wattValue;
  std::vector<float> positionValue;

  for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
    if (ptData.tableRow[cadIndex].tableEntry[j].targetPosition != INT16_MIN) {
      wattValue.push_back(static_cast<float>(j * POWERTABLE_WATT_INCREMENT));
      positionValue.push_back(static_cast<float>(ptData.tableRow[cadIndex].tableEntry[j].targetPosition));
    }
  }

  if (wattValue.size() < 2) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "LookupWatts failed - not enough data for cad %d", cad);
    return RETURN_ERROR;
  }

  float result;
  if (internalPosition < positionValue.front()) {
    result = linearExtrapolate(wattValue.data(), positionValue.data(), wattValue.size(), static_cast<float>(internalPosition));
  } else if (internalPosition > positionValue.back()) {
    result = linearExtrapolate(wattValue.data(), positionValue.data(), wattValue.size(), static_cast<float>(internalPosition));
  } else {
    result = linearInterpolate(wattValue.data(), positionValue.data(), wattValue.size(), static_cast<float>(internalPosition));
  }

  int watts = static_cast<int>(result);
  SS2K_LOG(POWERTABLE_LOG_TAG, "LookupWatts computed %dw from pos %d, cad %d", watts, targetPosition, cad);
  return watts;
}

/**
 * @brief Performs linear interpolation to estimate a value `j` based on the given
 *        x and y data points.
 *
 * This function takes two vectors `x` and `y` representing a set of data points
 * and a value `j` for which an interpolated value is to be calculated. If `j` is
 * outside the range of `x`, the function extrapolates using the nearest boundary
 * values. The result is clamped to the range of `y` to ensure it does not exceed
 * the minimum or maximum values of the dataset.
 *
 * @param x A vector of x-coordinates (must be sorted in ascending order).
 * @param y A vector of y-coordinates corresponding to the x-coordinates.
 * @param j The x-coordinate for which the interpolated y-coordinate is to be calculated.
 *
 * @return The interpolated y-coordinate corresponding to `j`. If interpolation fails
 *         due to invalid input (e.g., duplicate x values), the function logs an error
 *         and returns `INT16_MIN`.
 */
float PTHelpers::linearInterpolate(const float* x, const float* y, size_t n, float j) {
  auto upper = std::upper_bound(x, x + n, j);

  if (upper == x + n) return y[n - 1];  // Extrapolate using last value
  if (upper == x) return y[0];          // Extrapolate using first value

  auto lower = upper - 1;
  float x0 = *lower, x1 = *upper;
  float y0 = y[lower - x], y1 = y[upper - x];

  if (x1 - x0 == 0) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Linear Interpolation failed, x1 - x0 is 0");
    return INT16_MIN;
  }

  return y0 + (y1 - y0) * (j - x0) / (x1 - x0);
}

void CubicSpline::set_points(const float* x_vals, const float* y_vals, size_t n) {
  if (n < 2) return;  // Ensure sufficient points

  x.assign(x_vals, x_vals + n);
  y.assign(y_vals, y_vals + n);

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
  std::vector<std::pair<int, int>> emptyIndices;

  std::vector<float> prevX, prevY;
  std::vector<std::pair<int, int>> prevEmptyIndices;
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
    for (int i = rangeStart; i < rangeEnd; ++i) {
      int j = sum - i;
      if (j >= 0 && j < POWERTABLE_WATT_SIZE) {
        if (ptData.tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
          unique_xy.emplace_back(i, static_cast<float>(ptData.tableRow[i].tableEntry[j].targetPosition));
        } else {
          emptyIndices.emplace_back(i, j);
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

    extrapolateDiagonalEntries(emptyIndices, x.data(), y.data(), x.size(), ptData);
  }
}

bool CubicSpline::shouldUseNaturalSpline(const float* x, const float* y, size_t n) {
  if (n < 3) return true;  // Default to natural spline for small data sets

  // Compute approximate first derivatives at endpoints
  float startSlope = (y[1] - y[0]) / (x[1] - x[0]);
  float endSlope   = (y[n - 1] - y[n - 2]) / (x[n - 1] - x[n - 2]);

  // Adaptive slope threshold
  float dataRange      = *std::max_element(y, y + n) - *std::min_element(y, y + n);
  float slopeThreshold = 0.1f * dataRange;

  if (std::abs(startSlope) > slopeThreshold || std::abs(endSlope) > slopeThreshold) {
    return false;  // Use clamped spline
  }

  if (n < 4) return true;  // Not enough points for second derivative check

  // Compute second derivatives safely
  float h0 = x[1] - x[0], h1 = x[2] - x[1];
  if (h0 == 0.0f || h1 == 0.0f) return true;  // Avoid division by zero

  float secondDerivativeStart = (y[2] - 2 * y[1] + y[0]) / (h0 * h1);

  float hn1 = x[n - 2] - x[n - 3], hn2 = x[n - 1] - x[n - 2];
  if (hn1 == 0.0f || hn2 == 0.0f) return true;  // Avoid division by zero

  float secondDerivativeEnd = (y[n - 1] - 2 * y[n - 2] + y[n - 3]) / (hn1 * hn2);

  float curvatureThreshold = 1.0f;
  return !(std::abs(secondDerivativeStart) > curvatureThreshold || std::abs(secondDerivativeEnd) > curvatureThreshold);
}