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
#include <chrono>

std::ofstream outFile("test/output/test_PowerTable_Helpers.txt", std::ios::trunc);

// Stub for millis() function in native environment
unsigned long millis() {
  using namespace std::chrono;
  static auto start = steady_clock::now();
  auto now          = steady_clock::now();
  return duration_cast<milliseconds>(now - start).count();
}

#define SS2K_LOG(tag, format, ...)                                \
  {                                                               \
    char buffer[1024];                                            \
    std::snprintf(buffer, sizeof(buffer), format, ##__VA_ARGS__); \
    outFile << "[" << tag << "] " << buffer << std::endl;         \
  }
#else
#include "SS2KLog.h"
#endif

/////////////////////////Testing Helpers/////////////////////////

// --- Advanced Predictor ---

// Gaussian Elimination Solver for NxN matrix
bool ResistanceModel::solveMatrix(double A[6][6], double B[6], int n) {
  for (int i = 0; i < n; i++) {
    // Pivot
    int maxRow = i;
    for (int k = i + 1; k < n; k++) {
      // CHANGE: std::abs -> std::fabs
      if (std::fabs(A[k][i]) > std::fabs(A[maxRow][i])) maxRow = k;
    }

    // Swap rows
    for (int k = 0; k < n; k++) {
      std::swap(A[i][k], A[maxRow][k]);
    }
    std::swap(B[i], B[maxRow]);

    // Singular matrix check
    // CHANGE: std::abs -> std::fabs
    if (std::fabs(A[i][i]) < 1e-9) return false;

    // Eliminate
    for (int k = i + 1; k < n; k++) {
      double c = -A[k][i] / A[i][i];
      for (int j = i; j < n; j++) {
        if (i == j)
          A[k][j] = 0;
        else
          A[k][j] += c * A[i][j];
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

void ResistanceModel::fit(const PTData& data) {
  // long int timer = millis();
  isValid = false;

  // 1. Collect Valid Points & Find Bounds
  struct Point {
    double w, r, z;
  };
  std::vector<Point> points;
  points.reserve(240);  // Prevent heap fragmentation

  minW = 1e9;
  maxW = -1e9;
  minR = 1e9;
  maxR = -1e9;

  for (int r = 0; r < POWERTABLE_CAD_SIZE; r++) {
    for (int c = 0; c < POWERTABLE_WATT_SIZE; c++) {
      int16_t val = data.tableRow[r].tableEntry[c].targetPosition;
      if (val == INT16_MIN) continue;

      double w   = 0 + (c * POWERTABLE_WATT_INCREMENT);
      double rpm = MINIMUM_TABLE_CAD + (r * POWERTABLE_CAD_INCREMENT);

      points.push_back({w, rpm, (double)val});

      if (w < minW) minW = w;
      if (w > maxW) maxW = w;
      if (rpm < minR) minR = rpm;
      if (rpm > maxR) maxR = rpm;
    }
  }

  int N = points.size();
  if (N < 4) return;

  // CHANGE: std::abs -> std::fabs
  if (std::fabs(maxW - minW) < 1.0) maxW += 1.0;
  if (std::fabs(maxR - minR) < 1.0) maxR += 1.0;

  // 2. Decide Model Complexity
  // Require 9 points for the complex curve to ensure stability
  isQuadratic   = (N >= 9) ? true : false;
  int numCoeffs = isQuadratic ? 6 : 3;

  // 3. Build Matrices (Stack Allocated)
  double A[6][6] = {0};
  double B[6]    = {0};

  for (const auto& p : points) {
    double x = normW(p.w);
    double y = normR(p.r);
    double z = p.z;

    double terms[6];
    terms[0] = 1.0;
    terms[1] = x;
    terms[2] = y;
    if (isQuadratic) {
      terms[3] = x * x;
      terms[4] = y * y;
      terms[5] = x * y;
    }

    for (int i = 0; i < numCoeffs; i++) {
      for (int j = 0; j < numCoeffs; j++) {
        A[i][j] += terms[i] * terms[j];
      }
      B[i] += terms[i] * z;
    }
  }

  // 4. Solve
  isValid = solveMatrix(A, B, numCoeffs);

  // SS2K_LOG(PTDATA_LOG_TAG, "Fit Valid: %d, N: %d, Quad: %d", isValid, N, isQuadratic);
}

int16_t ResistanceModel::predict(double watts, double rpm) {
  if (!isValid) return INT16_MIN;
  //SS2K_LOG(PTDATA_LOG_TAG, "Predicting for Watts: %.2f, RPM: %.2f", watts, rpm);

  // Normalize inputs using the bounds found during training
  double x = normW(watts);
  double y = normR(rpm);

  // If the model is quadratic and 'b[3]' (the x^2 term) is negative,
  // the curve is a downward-facing parabola (concave down).
  // We must ensure we don't predict on the "falling" side of the peak.
  if (isQuadratic && b[3] < -1e-9) {
    // The slope with respect to x is: dZ/dx = b1 + 2*b3*x + b5*y
    // The peak (slope = 0) occurs at: x_vertex = -(b1 + b5*y) / (2*b3)
    double numerator   = -(b[1] + b[5] * y);
    double denominator = 2.0 * b[3];

    // Safety check for div/0 (though b[3] < -1e-9 ensures it's non-zero)
    double x_vertex = numerator / denominator;

    // If the requested x is to the right of the peak, clamp it to the peak.
    if (x > x_vertex) {
      x = x_vertex;
    }
  }

  double res = b[0] + b[1] * x + b[2] * y;

  if (isQuadratic) {
    res += b[3] * x * x + b[4] * y * y + b[5] * x * y;
  }

  // Clamp to int16 range
  if (res > 32767.0) return 32767;
  if (res < -32768.0) return -32768;
  return (int16_t)round(res);
}

/**
 * Inverse prediction: Given a Resistance (TargetPosition) and Cadence,
 * calculate the required Watts.
 */
int ResistanceModel::predictWatts(int32_t resistance, float cadence) {
  if (!isValid) return 0;  // Or appropriate error code

  // 1. Normalize the inputs (Cadence only, we solve for X)
  double y = normR(cadence);
  double Z = (double)resistance;

  double predictedNormWatts = 0.0;

  // 2. Setup Quadratic Equation Coefficients: A*x^2 + B*x + C = 0
  double QA = isQuadratic ? b[3] : 0.0;
  double QB = b[1];
  double QC = b[0] + (b[2] * y) - Z;

  if (isQuadratic) {
    QB += b[5] * y;
    QC += b[4] * y * y;
  }

  // 3. Solve
  if (abs(QA) < 1e-9) {
    // --- Linear Solve ---
    if (abs(QB) < 1e-9) return 0;
    predictedNormWatts = -QC / QB;
  } else {
    // --- Quadratic Solve ---
    double delta = (QB * QB) - (4 * QA * QC);

    if (delta < 0) {
      // No real solution (Resistance likely higher than the peak)
      // Return the vertex (max possible watts before drop-off)
      predictedNormWatts = -QB / (2 * QA);
    } else {
      double sqrtDelta = std::sqrt(delta);
      double x1        = (-QB + sqrtDelta) / (2 * QA);
      double x2        = (-QB - sqrtDelta) / (2 * QA);

      // We have two Watt answers.
      // Physical logic: Resistance increases with Watts.
      // If the curve is concave down (QA < 0), the "rising" edge is the smaller x.
      // If the curve is concave up (QA > 0), the "rising" edge is the larger x.

      bool x1In = (x1 >= 0.0 && x1 <= 1.0);
      bool x2In = (x2 >= 0.0 && x2 <= 1.0);

      if (x1In && !x2In)
        predictedNormWatts = x1;
      else if (!x1In && x2In)
        predictedNormWatts = x2;
      else {
        // Both valid (or both invalid).
        // Use slope logic to break the tie.
        if (QA < 0) {
          // Downward parabola: Rising edge is the Left side (Smaller X)
          predictedNormWatts = (x1 < x2) ? x1 : x2;
        } else {
          // Upward parabola: Rising edge is the Right side (Larger X)
          predictedNormWatts = (x1 > x2) ? x1 : x2;
        }
      }
    }
  }

  // 4. Denormalize
  double finalWatts = predictedNormWatts * (maxW - minW) + minW;

  // 5. Clamp to logical limits
  if (finalWatts < 0) finalWatts = 0;

  return (int)round(finalWatts);
}
///////////////////////////////////////////////END of testing Helpers///////////////////////////////////////////////

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
  if (!resistanceModel.getIsValid()) {
    resistanceModel.fit(ptData);
  }
  return resistanceModel.predict(watts, cad) * TABLE_DIVISOR;
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
int PTHelpers::lookupWatts(int cad, int32_t targetPosition, PTData& ptData) {
  if (!resistanceModel.getIsValid()) {
    resistanceModel.fit(ptData);
  }
  int wattsOut = resistanceModel.predictWatts(targetPosition / TABLE_DIVISOR, static_cast<float>(cad));
  if (wattsOut < cad / 2) {
    wattsOut = cad / 2;  // enforce a minimum wattage based on cadence to avoid absurdly low wattage outputs
  }
  return wattsOut;
}

/**
 * @brief Fills empty cells in each row by using the regression model to predict missing values.
 * @param ptData The main power table data structure.
 */
void PTHelpers::fill(PTData& ptData) {
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      if (ptData.tableRow[i].tableEntry[j].targetPosition == INT16_MIN) {
        int cadence                                     = MINIMUM_TABLE_CAD + (i * POWERTABLE_CAD_INCREMENT);
        int watts                                       = j * POWERTABLE_WATT_INCREMENT;
        int16_t predictedPosition                       = resistanceModel.predict(watts, cadence);
        ptData.tableRow[i].tableEntry[j].targetPosition = predictedPosition;
        //ptData.tableRow[i].tableEntry[j].readings       = 1;  // Mark as inferred with low confidence
      }
    }
  }
}

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
  clean(ptData);

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
    //clean(ptData);
    return;
  }

  entry.targetPosition = pos;  // Update the target position with the new average
  // Increment readings, capping at the max value.
  if (entry.readings < MAX_NEIGHBOR_WEIGHT && !moveTable) {
    entry.readings++;
  }

    // After updating a point, re-process the entire table to enforce global monotonicity.
  resistanceModel.fit(ptData);
  // // After updating a point, re-process the entire table to enforce global monotonicity.
  this->fillGaps(ptData);
  // for (int i = 0; i < 10; i++) {  // Run the PAVA functions multiple times to ensure convergence
  bool caddone  = false;
  bool wattdone = false;
  int loop      = 0;
  while ((!caddone || !wattdone) && loop < 3) {
    loop++;
    if (!caddone) {
      caddone = fillAllCadenceLines(ptData);
    }
    if (!wattdone) {
      wattdone = fillAllWattColumns(ptData);
    }
    SS2K_LOG(PTDATA_LOG_TAG, "PAVA iteration done, still converging: cad %d, watt %d, Loop %d", caddone, wattdone, loop);
  }


  if (resistanceModel.getIsValid()) {
    this->fill(ptData);
  }
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
            ptData.tableRow[k].tableEntry[j].readings       = 0;
            removed++;
          } else if (ptData.tableRow[k].tableEntry[j].readings > ptData.tableRow[i].tableEntry[j].readings) {
            // Current entry has fewer readings, mark it for removal and update current to the better one
            ptData.tableRow[i].tableEntry[j].targetPosition = INT16_MIN;
            ptData.tableRow[i].tableEntry[j].readings       = 0;
            removed++;
            break;  // Move to next i since current is removed
          } else {
            // Readings are equal - keep the one at higher cadence (k), remove the one at i
            ptData.tableRow[i].tableEntry[j].targetPosition = INT16_MIN;
            ptData.tableRow[i].tableEntry[j].readings       = 0;
            removed++;
            break;  // Move to next i since current is removed
          }
        }
      }
    }
  }

  if (removed > 0) {
    SS2K_LOG(PTDATA_LOG_TAG, "Cleaned %d readings", removed);
  }
}
