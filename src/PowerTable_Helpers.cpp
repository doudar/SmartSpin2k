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


/////////////////////////Testing Helpers/////////////////////////

// --- Advanced Predictor ---
class ResistanceModel {
private:
    // Coefficients (Normalizing makes these fit in float/double safely)
    // Model: Z = b0 + b1*x + b2*y + b3*x^2 + b4*y^2 + b5*x*y
    double b[6] = {0}; 
    bool isQuadratic = false;
    bool isValid = false;

    // Normalization bounds (to keep math stable)
    double minW = 0, maxW = 1;
    double minR = 0, maxR = 1;

    // Helper: Normalize a value to 0.0 - 1.0 range
    double normW(double w) { return (w - minW) / (maxW - minW); }
    double normR(double r) { return (r - minR) / (maxR - minR); }

    // Gaussian Elimination Solver for NxN matrix
    bool solveMatrix(std::vector<std::vector<double>> &A, std::vector<double> &B, int n) {
        for (int i = 0; i < n; i++) {
            // Pivot
            int maxRow = i;
            for (int k = i + 1; k < n; k++) {
                if (abs(A[k][i]) > abs(A[maxRow][i])) maxRow = k;
            }
            std::swap(A[i], A[maxRow]);
            std::swap(B[i], B[maxRow]);

            if (abs(A[i][i]) < 1e-9) return false; // Singular matrix

            for (int k = i + 1; k < n; k++) {
                double c = -A[k][i] / A[i][i];
                for (int j = i; j < n; j++) {
                    if (i == j) A[k][j] = 0;
                    else A[k][j] += c * A[i][j];
                }
                B[k] += c * B[i];
            }
        }

        // Back substitution
        for (int i = n - 1; i >= 0; i--) {
            double sum = 0;
            for (int j = i + 1; j < n; j++) sum += A[i][j] * b[j];
            b[i] = (B[i] - sum) / A[i][i];
        }
        return true;
    }

public:
    void fit(const PTData& data) {
        isValid = false;
        
        // 1. Collect Valid Points & Find Bounds
        struct Point { double w, r, z; };
        std::vector<Point> points;
        
        minW = 1e9; maxW = -1e9; minR = 1e9; maxR = -1e9;

        for (int r = 0; r < POWERTABLE_CAD_SIZE; r++) {
            for (int c = 0; c < POWERTABLE_WATT_SIZE; c++) {
                int16_t val = data.tableRow[r].tableEntry[c].targetPosition;
                if (val == INT16_MIN) continue;

                double w = 0 + (c * POWERTABLE_WATT_INCREMENT);
                double rpm = MINIMUM_TABLE_CAD + (r * POWERTABLE_CAD_INCREMENT);
                
                points.push_back({w, rpm, (double)val});
                
                if (w < minW) minW = w; if (w > maxW) maxW = w;
                if (rpm < minR) minR = rpm; if (rpm > maxR) maxR = rpm;
            }
        }

        int N = points.size();
        if (N < 4) return; // Not enough data for anything

        // Avoid division by zero in normalization
        if (abs(maxW - minW) < 1.0) maxW += 1.0;
        if (abs(maxR - minR) < 1.0) maxR += 1.0;

        // 2. Decide Model Complexity
        // We need 6 points for Quadratic (Curve), 3 for Linear (Plane)
        isQuadratic = (N >= 6); 
        int numCoeffs = isQuadratic ? 6 : 3;

        // 3. Build Matrices (Least Squares)
        // System: A * b = B
        std::vector<std::vector<double>> A(numCoeffs, std::vector<double>(numCoeffs, 0.0));
        std::vector<double> B(numCoeffs, 0.0);

        for (const auto& p : points) {
            double x = normW(p.w);
            double y = normR(p.r);
            double z = p.z;
            
            // Terms array: [1, x, y, x^2, y^2, xy]
            double terms[6];
            terms[0] = 1.0;
            terms[1] = x;
            terms[2] = y;
            if (isQuadratic) {
                terms[3] = x * x;
                terms[4] = y * y;
                terms[5] = x * y;
            }

            // Add to Normal Matrix
            for (int i = 0; i < numCoeffs; i++) {
                for (int j = 0; j < numCoeffs; j++) {
                    A[i][j] += terms[i] * terms[j];
                }
                B[i] += terms[i] * z;
            }
        }

        // 4. Solve
        isValid = solveMatrix(A, B, numCoeffs);
    }

    int16_t predict(double watts, double rpm) {
        if (!isValid) return INT16_MIN;

        // Normalize inputs using the bounds found during training
        double x = normW(watts);
        double y = normR(rpm);

        double res = b[0] + b[1]*x + b[2]*y;
        
        if (isQuadratic) {
            res += b[3]*x*x + b[4]*y*y + b[5]*x*y;
        }

        // Clamp to int16 range
        if (res > 32767.0) return 32767;
        if (res < -32768.0) return -32768;
        return (int16_t)round(res);
    }
};
///////////////////////////////////////////////END of testing Helpers///////////////////////////////////////////////

#define SS2K_LOG(tag, format, ...)                                \
  {                                                               \
    char buffer[1024];                                            \
    std::snprintf(buffer, sizeof(buffer), format, ##__VA_ARGS__); \
    outFile << "[" << tag << "] " << buffer << std::endl;         \
  }
#else
#include "SS2KLog.h"
#endif

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
  ResistanceModel resistanceModel;
  resistanceModel.fit(ptData);
  return resistanceModel.predict(watts, cad);

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

  if (resistance != RETURN_ERROR) {
    SS2K_LOG(PTDATA_LOG_TAG, "Extrapolated resistance: %d for watts=%d, cad=%d", resistance, watts, cad);
    // Return early if we found a valid extrapolated value
  } else {
    SS2K_LOG(PTDATA_LOG_TAG, "Extrapolation failed for watts=%d, cad=%d", watts, cad);
  }

  return resistance;
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
  // Prevent all crashes from out-of-bounds access.
  if (n < 2) {
    return INT16_MIN;  // Cannot extrapolate/interpolate with fewer than 2 points.
  }
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
    float totalOffset = 0.0f;
    int pairsFound    = 0;
    for (size_t i = 0; i < xy.first.size(); ++i) {
      for (size_t j = 0; j < xyUsed4Offset.first.size(); ++j) {
        if (xy.first[i] == xyUsed4Offset.first[j]) {
          totalOffset += (xyUsed4Offset.second[j] - xy.second[i]);
          pairsFound++;
          break;  // Found a match for xy.first[i], move to the next i.
        }
      }
    }
    float averageOffset = (pairsFound > 0) ? totalOffset / pairsFound : 0.0f;
    offset              = averageOffset * cadDelta;  // Apply the delta to the average offset.

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
  if (xy.first.size() < 2) {
    // Cannot find two points to interpolate/extrapolate between.
    // Return a sensible default or an error.
    return cad;
  }

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

/**
 * @brief Fills empty cells in each row by interpolating between neighbors.
 * This function turns sparse data lines into dense curves, which is a critical
 * prerequisite for the PAVA functions to work correctly and prevent line crossings.
 * @param ptData The main power table data structure.
 */
void PTHelpers::fillGaps(PTData& ptData) {
  for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {     // For each row
    for (int j = 0; j < POWERTABLE_WATT_SIZE; ++j) {  // For each cell
      if (ptData.tableRow[i].tableEntry[j].targetPosition == INT16_MIN) {
        // This cell is empty. Try to fill it by finding its neighbors.
        int left_idx  = -1;
        int right_idx = -1;

        // Find nearest neighbor to the left
        for (int k = j - 1; k >= 0; --k) {
          if (ptData.tableRow[i].tableEntry[k].targetPosition != INT16_MIN) {
            left_idx = k;
            break;
          }
        }

        // Find nearest neighbor to the right
        for (int k = j + 1; k < POWERTABLE_WATT_SIZE; ++k) {
          if (ptData.tableRow[i].tableEntry[k].targetPosition != INT16_MIN) {
            right_idx = k;
            break;
          }
        }

        // If we found neighbors on both sides, we can interpolate.
        if (left_idx != -1 && right_idx != -1) {
          const TableEntry& left_entry  = ptData.tableRow[i].tableEntry[left_idx];
          const TableEntry& right_entry = ptData.tableRow[i].tableEntry[right_idx];

          float x1 = left_idx;
          float y1 = left_entry.targetPosition;
          float x2 = right_idx;
          float y2 = right_entry.targetPosition;

          // Standard linear interpolation formula: y = y1 + (x - x1) * (y2 - y1) / (x2 - x1)
          float interpolated_pos = y1 + (j - x1) * (y2 - y1) / (x2 - x1);

          ptData.tableRow[i].tableEntry[j].targetPosition = static_cast<int16_t>(interpolated_pos);
          ptData.tableRow[i].tableEntry[j].readings       = 1;  // Mark as inferred with low confidence
        }
      }
    }
  }
}

bool PTHelpers::fillAllWattColumns(PTData& ptData) {
  bool converged = true;
  struct PAVAEntry {
    float position;
    float readings;
  };
  for (int watt_idx = 0; watt_idx < POWERTABLE_WATT_SIZE; ++watt_idx) {
    PAVAEntry correctedCol[POWERTABLE_CAD_SIZE];
    for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
      correctedCol[i] = {(float)ptData.tableRow[i].tableEntry[watt_idx].targetPosition, (float)ptData.tableRow[i].tableEntry[watt_idx].readings};
    }
    for (int i = 1; i < POWERTABLE_CAD_SIZE; ++i) {
      if (correctedCol[i].readings == 0) continue;
      for (int j = i; j > 0; --j) {
        if (correctedCol[j - 1].readings == 0) continue;
        if (correctedCol[j].position > correctedCol[j - 1].position) {
          converged           = false;
          float weightedSum   = (correctedCol[j].position * correctedCol[j].readings) + (correctedCol[j - 1].position * correctedCol[j - 1].readings);
          float totalReadings = correctedCol[j].readings + correctedCol[j - 1].readings;
          if (totalReadings > 0.0f) {
            float newPosition            = weightedSum / totalReadings;
            correctedCol[j].position     = newPosition;
            correctedCol[j].readings     = totalReadings;
            correctedCol[j - 1].position = newPosition;
            correctedCol[j - 1].readings = totalReadings;
          }
        } else {
          break;
        }
      }
    }
    for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
      if (ptData.tableRow[i].tableEntry[watt_idx].readings > 0) {
        ptData.tableRow[i].tableEntry[watt_idx].targetPosition = (int16_t)correctedCol[i].position;
      }
    }
  }
  return converged;
}

/**
 * @brief Enforces monotonicity across all watt rows using a weighted PAVA.
 * This function iterates through each cadence row and ensures that for any given
 * cadence, the targetPosition strictly INCREASES as wattage increases.
 * @param ptData The main power table data structure.
 */
bool PTHelpers::fillAllCadenceLines(PTData& ptData) {
  bool converged = true;
  struct PAVAEntry {
    float position;
    float readings;
  };
  for (int cad_idx = 0; cad_idx < POWERTABLE_CAD_SIZE; ++cad_idx) {
    PAVAEntry correctedRow[POWERTABLE_WATT_SIZE];
    for (int i = 0; i < POWERTABLE_WATT_SIZE; ++i) {
      correctedRow[i] = {(float)ptData.tableRow[cad_idx].tableEntry[i].targetPosition, (float)ptData.tableRow[cad_idx].tableEntry[i].readings};
    }
    for (int i = 1; i < POWERTABLE_WATT_SIZE; ++i) {
      if (correctedRow[i].readings == 0) continue;
      for (int j = i; j > 0; --j) {
        if (correctedRow[j - 1].readings == 0) continue;
        // Check for a violation: current position is LESS than the previous one (enforcing increasing trend).
        if (correctedRow[j].position < correctedRow[j - 1].position) {
          converged           = false;
          float weightedSum   = (correctedRow[j].position * correctedRow[j].readings) + (correctedRow[j - 1].position * correctedRow[j - 1].readings);
          float totalReadings = correctedRow[j].readings + correctedRow[j - 1].readings;
          if (totalReadings > 0.0f) {
            float newPosition            = weightedSum / totalReadings;
            correctedRow[j].position     = newPosition;
            correctedRow[j].readings     = totalReadings;
            correctedRow[j - 1].position = newPosition;
            correctedRow[j - 1].readings = totalReadings;
          }
        } else {
          break;
        }
      }
    }
    for (int i = 0; i < POWERTABLE_WATT_SIZE; ++i) {
      if (ptData.tableRow[cad_idx].tableEntry[i].readings > 0) {
        ptData.tableRow[cad_idx].tableEntry[i].targetPosition = (int16_t)correctedRow[i].position;
      }
    }
  }
  return converged;
}

/**
 * @brief Enters a new data point and then enforces monotonicity on the whole table.
 * * This function first calculates the running average for the given data point.
 * Then, it calls the PAVA helper functions to ensure the entire table remains
 * monotonically increasing across both watts and cadence.
 * * @param ptData The main power table data structure.
 * @param index The watt and cadence index for the new data point.
 * @param pos The measured targetPosition for this data point.
 */
void PTHelpers::enterData(PTData& ptData, ptIndex index, int pos) {
  // Reference to the specific table entry for cleaner code
  TableEntry& entry = ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex];

  int left       = INT16_MIN;
  int down       = INT16_MIN;
  bool moveTable = false;

  // Get the topmost value in the column

  if (entry.readings == 0) {  // if first reading in this entry

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
      left = ptData.tableRow[index.cadIndex].tableEntry[i].targetPosition;
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
      down = ptData.tableRow[j].tableEntry[index.wattIndex].targetPosition;
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
    // int amount = 0;
    // int lShift = (left != INT16_MIN) ? pos - left : 0;
    // int dShift = (down != INT16_MIN) ? pos - down : 0;
    // SS2K_LOG(PTDATA_LOG_TAG, "%d lShift, %d dShift", lShift, dShift);
    // if (abs(lShift) > abs(dShift)) {
    //   SS2K_LOG(PTDATA_LOG_TAG, "lShift was greater, %d lShift, %d dShift", lShift, dShift);
    //   amount = lShift;
    // } else if (down != INT16_MIN) {
    //   SS2K_LOG(PTDATA_LOG_TAG, "dShift was greater, %d lShift, %d dShift", lShift, dShift);
    //   amount = dShift;
    // } else {
    //   clean(ptData);
    //   return;
    // }
    // SS2K_LOG(PTDATA_LOG_TAG, "Moving table to accommodate new entry (%d)(%d)(%d), left(%d), down(%d), amount(%d)", index.cadIndex, index.wattIndex, pos, left, down, amount);
    // // Move the table to accommodate the new entry
    // for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
    //   for (int j = 0; j < POWERTABLE_WATT_SIZE; ++j) {
    //     if (ptData.tableRow[i].tableEntry[j].readings > 0) {
    //       ptData.tableRow[i].tableEntry[j].targetPosition += amount;
    //     }
    //   }
    // }
    clean(ptData);
    return;
  }

  entry.targetPosition = pos;  // Update the target position with the new average
  // Increment readings, capping at the max value.
  if (entry.readings < MAX_NEIGHBOR_WEIGHT && !moveTable) {
    entry.readings++;
  }

  // // After updating a point, re-process the entire table to enforce global monotonicity.
  this->fillGaps(ptData);
  // for (int i = 0; i < 10; i++) {  // Run the PAVA functions multiple times to ensure convergence
  bool caddone  = false;
  bool wattdone = false;
  int loop      = 0;
  while (!caddone || !wattdone) {
    loop++;
    if (!caddone) {
      caddone = fillAllCadenceLines(ptData);
    }
    if (!wattdone) {
      wattdone = fillAllWattColumns(ptData);
    }
    SS2K_LOG(PTDATA_LOG_TAG, "PAVA iteration done, still converging: cad %d, watt %d, Loop %d", caddone, wattdone, loop);
  }

  clean(ptData);
}

void PTHelpers::clean(PTData& ptData) {
  int removed = 0;
  
  // First pass: remove entries with readings < 1 or negative positions
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      if (ptData.tableRow[i].tableEntry[j].readings < 1 || ptData.tableRow[i].tableEntry[j].targetPosition < 0) {
        if (ptData.tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
          removed++;
        }
        ptData.tableRow[i].tableEntry[j].targetPosition = INT16_MIN;
        ptData.tableRow[i].tableEntry[j].readings       = 0;
      }
    }
  }
  
  // Second pass: remove duplicate values in columns (same resistance for multiple cadences)
  // For each column, track all unique values and remove duplicates
  for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
    for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
      if (ptData.tableRow[i].tableEntry[j].targetPosition == INT16_MIN) continue;
      
      int16_t currentValue = ptData.tableRow[i].tableEntry[j].targetPosition;
      
      // Check all other entries in this column for the same value
      for (int k = i + 1; k < POWERTABLE_CAD_SIZE; k++) {
        if (ptData.tableRow[k].tableEntry[j].targetPosition == currentValue) {
          // Found duplicate - remove the one with fewer readings
          // If readings are equal, keep the one at higher cadence (higher index)
          if (ptData.tableRow[k].tableEntry[j].readings < ptData.tableRow[i].tableEntry[j].readings) {
            ptData.tableRow[k].tableEntry[j].targetPosition = INT16_MIN;
            ptData.tableRow[k].tableEntry[j].readings = 0;
            removed++;
          } else if (ptData.tableRow[k].tableEntry[j].readings > ptData.tableRow[i].tableEntry[j].readings) {
            // Current entry has fewer readings, mark it for removal and update current to the better one
            ptData.tableRow[i].tableEntry[j].targetPosition = INT16_MIN;
            ptData.tableRow[i].tableEntry[j].readings = 0;
            removed++;
            break; // Move to next i since current is removed
          } else {
            // Readings are equal - keep the one at higher cadence (k), remove the one at i
            ptData.tableRow[i].tableEntry[j].targetPosition = INT16_MIN;
            ptData.tableRow[i].tableEntry[j].readings = 0;
            removed++;
            break; // Move to next i since current is removed
          }
        }
      }
    }
  }
  
  if (removed > 0) {
    SS2K_LOG(PTDATA_LOG_TAG, "Cleaned %d readings", removed);
  }
}

