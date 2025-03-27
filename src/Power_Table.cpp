/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Power_Table.h"
#include "SS2KLog.h"
#include "BLE_Custom_Characteristic.h"
#include <LittleFS.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <map>
#include <complex>

void PowerBuffer::set(int i) {
  this->powerEntry[i].readings++;
  this->powerEntry[i].watts          = rtConfig->watts.getValue();
  this->powerEntry[i].cad            = rtConfig->cad.getValue();
  this->powerEntry[i].targetPosition = ss2k->getCurrentPosition() / TABLE_DIVISOR;  // dividing by 10 to save memory.
}

void PowerBuffer::reset() {
  SS2K_LOG(POWERTABLE_LOG_TAG, "Power Table Reset");
  for (int i = 0; i < POWER_SAMPLES; i++) {
    this->powerEntry[i].readings       = 0;
    this->powerEntry[i].cad            = 0;
    this->powerEntry[i].watts          = 0;
    this->powerEntry[i].targetPosition = 0;
  }
}

// return the number of entries with readings.
int PowerBuffer::getReadings() {
  int ret = 0;
  for (int i = 0; i < POWER_SAMPLES; i++) {
    if (this->powerEntry[i].readings != 0) {
      ret++;
    }
  }
  return ret;
}

void PowerTable::processPowerValue(PowerBuffer& powerBuffer, int cadence, Measurement watts) {
  if ((cadence >= (MINIMUM_TABLE_CAD - (POWERTABLE_CAD_INCREMENT / 2))) &&
      (cadence <= (MINIMUM_TABLE_CAD + (POWERTABLE_CAD_INCREMENT * POWERTABLE_CAD_SIZE) - (POWERTABLE_CAD_SIZE / 2))) && (watts.getValue() > 10) &&  // adding constraints
      (watts.getValue() < (POWERTABLE_WATT_SIZE * POWERTABLE_WATT_INCREMENT))) {
    if (powerBuffer.powerEntry[0].readings == 0) {  // we need to make sure stepper position is not negative so it only takes positive resistance values
      // Take Initial reading
      powerBuffer.set(0);
      // Check if the current stepper posistion is within a 5% range of the previous stepper position and that the current position is not negative
    }

    int currentPos = ss2k->getCurrentPosition() / TABLE_DIVISOR;
    int targetPos  = powerBuffer.powerEntry[0].targetPosition;
    int range      = PT_READING_RANGE + ERG_SENSITIVITY;

    if (currentPos >= (targetPos - range) && currentPos <= (targetPos + range)) {
      for (int i = 1; i < POWER_SAMPLES; i++) {
        if (powerBuffer.powerEntry[i].readings == 0) {
          SS2K_LOG(POWERTABLE_LOG_TAG, "Success!");
          powerBuffer.set(i);  // Add additional readings to the buffer.
          break;
        }
      }
      if (powerBuffer.powerEntry[POWER_SAMPLES - 1].readings == 1) {  // If buffer is full, create a new table entry and clear the buffer.
        this->newEntry(powerBuffer);
        this->toLog();
        this->_manageSaveState();
        powerBuffer.reset();
      }
    } else {  // Reading was outside the range - clear the buffer and start over.
      powerBuffer.reset();
    }
  }
}

// Set min / max stepper position
void PowerTable::setStepperMinMax() {
  int32_t _return = RETURN_ERROR;

  // if Homing was preformed, skip estimating min_max
  if (rtConfig->getHomed()) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Using detected travel limits during homing");
    return;
  }

  // if the FTMS device reports resistance feedback, skip estimating min_max
  if (rtConfig->resistance.getValue() > 0) {
    rtConfig->setMinStep(-DEFAULT_STEPPER_TRAVEL);
    rtConfig->setMaxStep(DEFAULT_STEPPER_TRAVEL);
    SS2K_LOG(POWERTABLE_LOG_TAG, "Using Resistance Travel Limits");
    return;
  }

  int minBreakWatts = userConfig->getMinWatts();
  if (minBreakWatts > 1) {
    _return = this->lookup(minBreakWatts, NORMAL_CAD);
    if (_return != RETURN_ERROR) {
      // never set less than one shift below current incline.
      if ((_return >= ss2k->getCurrentPosition()) && (rtConfig->watts.getValue() > userConfig->getMinWatts())) {
        _return = ss2k->getCurrentPosition() - userConfig->getShiftStep();
        SS2K_LOG(POWERTABLE_LOG_TAG, "Min Position too close to current incline: %d", _return);
      }
      // never set above max step.
      if (_return >= rtConfig->getMaxStep()) {
        _return = ss2k->getCurrentPosition() - userConfig->getShiftStep() * 2;
        SS2K_LOG(POWERTABLE_LOG_TAG, "Min Position above max!: %d", _return);
      }
      rtConfig->setMinStep(_return);
      SS2K_LOG(POWERTABLE_LOG_TAG, "Min Position Set: %d", _return);
    }
  }

  int maxBreakWatts = userConfig->getMaxWatts();
  if (maxBreakWatts > 1) {
    _return = this->lookup(maxBreakWatts, NORMAL_CAD);
    if (_return != RETURN_ERROR) {
      // never set less than one shift above current incline.
      if ((_return <= ss2k->getCurrentPosition()) && (rtConfig->watts.getValue() < userConfig->getMaxWatts())) {
        _return = ss2k->getCurrentPosition() + userConfig->getShiftStep();
        SS2K_LOG(POWERTABLE_LOG_TAG, "Max Position too close to current incline: %d", _return);
      }
      // never set below min step.
      if (_return <= rtConfig->getMinStep()) {
        _return = ss2k->getCurrentPosition() + userConfig->getShiftStep() * 2;
        SS2K_LOG(POWERTABLE_LOG_TAG, "Max Position below min!: %d", _return);
      }
      rtConfig->setMaxStep(_return);
      SS2K_LOG(POWERTABLE_LOG_TAG, "Max Position Set: %d", _return);
    }
  }
}

int32_t PowerTable::lookup(int watts, int cad) {
  int cadIndex  = round(((float)cad - (float)MINIMUM_TABLE_CAD) / (float)POWERTABLE_CAD_INCREMENT);
  int wattIndex = round((float)watts / (float)POWERTABLE_WATT_INCREMENT);

  // If request is outside table limits, perform linear extrapolation
  if (cad < MINIMUM_TABLE_CAD || cad > (MINIMUM_TABLE_CAD + (POWERTABLE_CAD_SIZE - 1) * POWERTABLE_CAD_INCREMENT) ||
      watts > (POWERTABLE_WATT_SIZE - 1) * POWERTABLE_WATT_INCREMENT) {
    // Perform linear extrapolation based on existing data
    int extrapolatedValue = INT16_MIN;

    // Extrapolation for cadence out of bounds
    if (cad < MINIMUM_TABLE_CAD || cad > (MINIMUM_TABLE_CAD + (POWERTABLE_CAD_SIZE - 1) * POWERTABLE_CAD_INCREMENT)) {
      int extrapRow1 = -1, extrapRow2 = -1;
      for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
        if (this->tableRow[i].tableEntry[wattIndex].targetPosition != INT16_MIN) {
          extrapRow1 = i;
          break;
        }
      }
      for (int i = POWERTABLE_CAD_SIZE - 1; i >= 0; --i) {
        if (this->tableRow[i].tableEntry[wattIndex].targetPosition != INT16_MIN) {
          extrapRow2 = i;
          break;
        }
      }
      if (extrapRow1 != -1 && extrapRow2 != -1) {
        int cad1 = extrapRow1 * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD;
        int cad2 = extrapRow2 * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD;
        int val1 = this->tableRow[extrapRow1].tableEntry[wattIndex].targetPosition;
        int val2 = this->tableRow[extrapRow2].tableEntry[wattIndex].targetPosition;
        // divide by 0 safety for if cad2 = cad1
        if (cad2 == cad1) {
          SS2K_LOG(POWERTABLE_LOG_TAG, "Extrapolate cadence lines were the same");
          return INT32_MIN;
        }
        extrapolatedValue = val1 + (val2 - val1) * (cad - cad1) / (cad2 - cad1);
        SS2K_LOG(POWERTABLE_LOG_TAG, "Lookup Extrapolated %d from %d, %d, for %dw %dcad", extrapolatedValue, val2, val1, watts, cad);
        return extrapolatedValue * TABLE_DIVISOR;
      }
    }

    // Extrapolation for watts out of bounds
    if (watts > (POWERTABLE_WATT_SIZE - 1) * POWERTABLE_WATT_INCREMENT) {
      int extrapCol1 = -1, extrapCol2 = -1;
      for (int j = POWERTABLE_WATT_SIZE - 1; j >= 0; j--) {
        if (this->tableRow[cadIndex].tableEntry[j].targetPosition != INT16_MIN) {
          if (extrapCol2 == -1) {
            extrapCol2 = j;
          } else {
            extrapCol1 = j;
            break;
          }
        }
      }
      if (extrapCol1 != -1 && extrapCol2 != -1) {
        int watts1 = extrapCol1 * POWERTABLE_WATT_INCREMENT;
        int watts2 = extrapCol2 * POWERTABLE_WATT_INCREMENT;
        int val1   = this->tableRow[cadIndex].tableEntry[extrapCol1].targetPosition;
        int val2   = this->tableRow[cadIndex].tableEntry[extrapCol2].targetPosition;
        // divide by 0 safety for if cad2 = cad1
        if (watts2 == watts1) {
          SS2K_LOG(POWERTABLE_LOG_TAG, "Extrapolate watts were the same");
          return INT32_MIN;
        }
        extrapolatedValue = val1 + (val2 - val1) * (watts - watts1) / (watts2 - watts1);
        SS2K_LOG(POWERTABLE_LOG_TAG, "Lookup Extrapolated %d from %d, %d, for %dw %dcad", extrapolatedValue, val2, val1, watts, cad);
        return extrapolatedValue * TABLE_DIVISOR;
      }
    }
    // Not enough data.
    return INT32_MIN;
  }

  // Edge cases out of the way, we should be able to interpolate.
  TestResults neighbors = testNeighbors(cadIndex, wattIndex, INT16_MIN);
  double x1             = neighbors.leftNeighbor.j * POWERTABLE_WATT_INCREMENT;
  double x2             = neighbors.rightNeighbor.j * POWERTABLE_WATT_INCREMENT;
  double y1             = neighbors.topNeighbor.i * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD;
  double y2             = neighbors.bottomNeighbor.i * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD;

  double Q11 = neighbors.leftNeighbor.targetPosition;
  double Q12 = neighbors.rightNeighbor.targetPosition;
  double Q21 = neighbors.topNeighbor.targetPosition;
  double Q22 = neighbors.bottomNeighbor.targetPosition;

  double R1 = INT16_MIN;
  double R2 = INT16_MIN;
  double R3 = INT16_MIN;

  SS2K_LOG(POWERTABLE_LOG_TAG, "Lookup debug %.0f X1 %.0f X2 %.0f Y1 %.0f Y2 %.0f", x1, x2, y1, y2);

  if (neighbors.leftNeighbor.found && neighbors.rightNeighbor.found) {
    // Watt result
    R1 = Q11 + (((watts - x1) / (x2 - x1)) * (Q12 - Q11));
    SS2K_LOG(POWERTABLE_LOG_TAG, "Lookup used neighbors L %.0f R %.0f R1 %.0f", Q11, Q12, R1);
  }
  if (neighbors.topNeighbor.found && neighbors.bottomNeighbor.found) {
    // CAD result
    R2 = Q21 + (((cad - y1) / (y2 - y1)) * (Q22 - Q21));
    SS2K_LOG(POWERTABLE_LOG_TAG, "Lookup used neighbors U %.0f D %.0f R2 %.0f", Q21, Q22, R2);
  }
  // Do we have a position at this entry?
  if (this->tableRow[cadIndex].tableEntry[wattIndex].targetPosition != INT16_MIN) {
    R3 = this->tableRow[cadIndex].tableEntry[wattIndex].targetPosition;
    SS2K_LOG(POWERTABLE_LOG_TAG, "Lookup used actual %d R3 %.0f", this->tableRow[cadIndex].tableEntry[wattIndex].targetPosition, R3);
  }

  int sum   = 0;
  int count = 0;

  if (R1 != INT16_MIN) {
    sum += R1;
    count++;
  }
  if (R2 != INT16_MIN) {
    sum += R2;
    count++;
  }
  if (R3 != INT16_MIN) {
    sum += R3;
    count++;
  }

  if (count == 0) {
    // Handle the case where all values are invalid.
    return INT16_MIN;
  }

  int ret = (sum / count) * TABLE_DIVISOR;
  SS2K_LOG(POWERTABLE_LOG_TAG, "Lookup result: %dw %dcad %d", watts, cad, ret);
  return ret;
}

// returns class of all neighbors that are found and within expected values.
TestResults PowerTable::testNeighbors(int i, int j, int testValue) {
  TestResults returnResult;
  // Get the neighbors
  // Check left neighbor
  if (j > 0) {
    for (int left = j - 1; left >= 0; --left) {
      if (this->tableRow[i].tableEntry[left].targetPosition != INT16_MIN) {
        returnResult.leftNeighbor.targetPosition = this->tableRow[i].tableEntry[left].targetPosition;
        returnResult.leftNeighbor.i              = i;
        returnResult.leftNeighbor.j              = left;
        returnResult.leftNeighbor.found          = 1;
        break;
      }
    }
  }

  if (returnResult.leftNeighbor.targetPosition < testValue || returnResult.leftNeighbor.targetPosition == INT16_MIN) {
    returnResult.leftNeighbor.passedTest = 1;
  }

  // Check right neighbor
  if (j < POWERTABLE_WATT_SIZE - 1) {
    for (int right = j + 1; right < POWERTABLE_WATT_SIZE; ++right) {
      if (this->tableRow[i].tableEntry[right].targetPosition != INT16_MIN) {
        returnResult.rightNeighbor.targetPosition = this->tableRow[i].tableEntry[right].targetPosition;
        returnResult.rightNeighbor.i              = i;
        returnResult.rightNeighbor.j              = right;
        returnResult.rightNeighbor.found          = 1;
        break;
      }
    }
  }

  if (returnResult.rightNeighbor.targetPosition > testValue || returnResult.rightNeighbor.targetPosition == INT16_MIN) {
    returnResult.rightNeighbor.passedTest = 1;
  }

  // Check top neighbor
  if (i > 0) {
    for (int up = i - 1; up >= 0; --up) {
      if (this->tableRow[up].tableEntry[j].targetPosition != INT16_MIN) {
        returnResult.topNeighbor.targetPosition = this->tableRow[up].tableEntry[j].targetPosition;
        returnResult.topNeighbor.i              = up;
        returnResult.topNeighbor.j              = j;
        returnResult.topNeighbor.found          = 1;
        break;
      }
    }
  }

  if (returnResult.topNeighbor.targetPosition > testValue || returnResult.topNeighbor.targetPosition == INT16_MIN) {
    returnResult.topNeighbor.passedTest = 1;
  }

  // Check bottom neighbor
  if (i < POWERTABLE_CAD_SIZE - 1) {
    for (int down = i + 1; down < POWERTABLE_CAD_SIZE; ++down) {
      if (this->tableRow[down].tableEntry[j].targetPosition != INT16_MIN) {
        returnResult.bottomNeighbor.targetPosition = this->tableRow[down].tableEntry[j].targetPosition;
        returnResult.bottomNeighbor.i              = down;
        returnResult.bottomNeighbor.j              = j;
        returnResult.bottomNeighbor.found          = 1;
        break;
      }
    }
  }

  if (returnResult.bottomNeighbor.targetPosition < testValue || returnResult.bottomNeighbor.targetPosition == INT16_MIN) {
    returnResult.bottomNeighbor.passedTest = 1;
  }

  if (returnResult.bottomNeighbor.found && returnResult.topNeighbor.found && returnResult.rightNeighbor.found && returnResult.leftNeighbor.found) {
    returnResult.allNeighborsFound = 1;
  }
  if (returnResult.bottomNeighbor.passedTest && returnResult.topNeighbor.passedTest && returnResult.rightNeighbor.passedTest && returnResult.leftNeighbor.passedTest) {
    returnResult.allNeighborsPassed = 1;
  }
  return returnResult;
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
float PowerTable::linearInterpolate(const float* x, const float* y, size_t n, float j) {
  auto upper = std::upper_bound(x, x + n, j);

  if (upper == x + n) return y[n - 1]; // Extrapolate using last value
  if (upper == x) return y[0];         // Extrapolate using first value

  auto lower = upper - 1;
  float x0 = *lower, x1 = *upper;
  float y0 = y[lower - x], y1 = y[upper - x]; 

  if (x1 - x0 == 0) {
      SS2K_LOG(POWERTABLE_LOG_TAG, "Linear Interpolation failed, x1 - x0 is 0");
      return INT16_MIN;
  }

  return y0 + (y1 - y0) * (j - x0) / (x1 - x0);

}

float PowerTable::linearExtrapolate(const float* x, const float* y, size_t n, float j) {
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

class CubicSpline {
  public:
      void set_points(const float* x_vals, const float* y_vals, size_t n, bool natural = true) {
          if (n < 2) return; // Safe check to make sure we have enough points
  
          size_t last_index = n - 1;
          x.assign(x_vals, x_vals + n);
          y.assign(y_vals, y_vals + n);
  
          h.resize(last_index);
          alpha.resize(n, 0.0f);
          l.resize(n, 0.0f);
          mu.resize(n, 0.0f);
          z.resize(n, 0.0f);
          c.resize(n, 0.0f);
          b.resize(last_index, 0.0f);
          d.resize(last_index, 0.0f);
  
          // Get h values
          for (size_t i = 0; i < last_index; ++i) {
              h[i] = x[i + 1] - x[i];
              if (h[i] == 0.0f) {
                  SS2K_LOG(POWERTABLE_LOG_TAG, "CubicSpline: Duplicate x values detected.");
                  return;
              }
          }
  
          // Get alpha values
          for (size_t i = 1; i < last_index; ++i) {
              alpha[i] = (3.0f / h[i]) * (y[i + 1] - y[i]) - (3.0f / h[i - 1]) * (y[i] - y[i - 1]);
          }
  
          // Check if we are using natural or clamped spline calculations
          if (natural) {
              alpha[0] = alpha[last_index] = 0.0f;
          } else {
              float f_prime_start = (y[1] - y[0]) / h[0];
              float f_prime_end = (y[last_index] - y[last_index - 1]) / h[last_index - 1];
              alpha[0] = 3.0f * (f_prime_start - (y[1] - y[0]) / h[0]);
              alpha[last_index] = 3.0f * ((y[last_index] - y[last_index - 1]) / h[last_index - 1] - f_prime_end);
          }
  
          // Get l, mu, and z
          l[0] = 1.0f;
          mu[0] = z[0] = 0.0f;
  
          for (size_t i = 1; i < last_index; ++i) {
              l[i] = 2.0f * (x[i + 1] - x[i - 1]) - h[i - 1] * mu[i - 1];
              if (l[i] == 0.0f) {
                  SS2K_LOG(POWERTABLE_LOG_TAG, "CubicSpline: Zero denominator detected in l[i].");
                  return;
              }
              mu[i] = h[i] / l[i];
              z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
          }
  
          l[last_index] = 1.0f;
          z[last_index] = c[last_index] = 0.0f;
  
          for (int j = last_index - 1; j >= 0; --j) {
              c[j] = z[j] - mu[j] * c[j + 1];
              b[j] = (y[j + 1] - y[j]) / h[j] - h[j] * (c[j + 1] + 2.0f * c[j]) / 3.0f;
              d[j] = (c[j + 1] - c[j]) / (3.0f * h[j]);
          }
      }
  
      float interpolate(float x_val) const {
          if (x_val < x.front() || x_val > x.back()) {
              return INT16_MIN; // Out of range
          }
  
          int i = std::upper_bound(x.begin(), x.end(), x_val) - x.begin() - 1;
          float dx = x_val - x[i];
          return y[i] + b[i] * dx + c[i] * dx * dx + d[i] * dx * dx * dx;
      }
  
      float extrapolate(float x_val) const {
          if (x_val < x.front()) {
              float dx = x_val - x[0];
              return y[0] + b[0] * dx + c[0] * dx * dx + d[0] * dx * dx * dx;
          }
          if (x_val > x.back()) {
              int n = x.size() - 1;
              float dx = x_val - x[n];
              return y[n] + b[n - 1] * dx + c[n - 1] * dx * dx + d[n - 1] * dx * dx * dx;
          }
          return INT16_MIN; // Out of range
      }
  
  private:
      std::vector<float> x, y, h, alpha, l, mu, z, c, b, d;
  };

  bool shouldUseNaturalSpline(const float* x, const float* y, size_t n) {
    if (n < 3) return true; // Default to natural spline for small data sets

    // Compute approximate first derivatives at endpoints
    float startSlope = (y[1] - y[0]) / (x[1] - x[0]);
    float endSlope = (y[n - 1] - y[n - 2]) / (x[n - 1] - x[n - 2]);

    // Adaptive slope threshold
    float dataRange = *std::max_element(y, y + n) - *std::min_element(y, y + n);
    float slopeThreshold = 0.1f * dataRange;

    if (std::abs(startSlope) > slopeThreshold || std::abs(endSlope) > slopeThreshold) {
        return false; // Use clamped spline
    }

    if (n < 4) return true; // Not enough points for second derivative check

    // Compute second derivatives safely
    float h0 = x[1] - x[0], h1 = x[2] - x[1];
    if (h0 == 0.0f || h1 == 0.0f) return true; // Avoid division by zero

    float secondDerivativeStart = (y[2] - 2 * y[1] + y[0]) / (h0 * h1);

    float hn1 = x[n - 2] - x[n - 3], hn2 = x[n - 1] - x[n - 2];
    if (hn1 == 0.0f || hn2 == 0.0f) return true; // Avoid division by zero

    float secondDerivativeEnd = (y[n - 1] - 2 * y[n - 2] + y[n - 3]) / (hn1 * hn2);

    float curvatureThreshold = 1.0f;
    return !(std::abs(secondDerivativeStart) > curvatureThreshold || std::abs(secondDerivativeEnd) > curvatureThreshold);
}

void PowerTable::fillTable() {
  this->findTableDirection(true);  // Horizontal
  this->findTableDirection(false); // Vertical
}

void PowerTable::findTableDirection(bool horizontal) {
  int outerSize = horizontal ? POWERTABLE_CAD_SIZE : POWERTABLE_WATT_SIZE;
  int innerSize = horizontal ? POWERTABLE_WATT_SIZE : POWERTABLE_CAD_SIZE;

  std::vector<std::pair<int, float>> unique_xy;
  std::vector<int> emptyIndices;
  std::vector<float> x, y;

  // Get previous x/y values and empty indices to reuse if we can
  std::vector<float> prevX, prevY;
  std::vector<int> prevEmptyIndices;
  bool prevSplineValid = false;
  bool prevNaturalSpline = false;

  for (int outerValue = 0; outerValue < outerSize; ++outerValue) {
      unique_xy.clear();
      emptyIndices.clear();
      x.clear();
      y.clear();

      int rangeStart = std::max(0, innerSize / 2 - 5);
      int rangeEnd = std::min(innerSize, innerSize / 2 + 5);

      for (int innerValue = rangeStart; innerValue < rangeEnd; ++innerValue) {
          int i = horizontal ? outerValue : innerValue;
          int j = horizontal ? innerValue : outerValue;

          int targetPos = this->tableRow[i].tableEntry[j].targetPosition;
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

      bool useNaturalSpline = shouldUseNaturalSpline(x.data(), y.data(), x.size());

      // Store values
      prevX = x;
      prevY = y;
      prevEmptyIndices = emptyIndices;
      prevNaturalSpline = useNaturalSpline;
      prevSplineValid = true;

      fillEmptyTable(outerValue, emptyIndices, x.data(), y.data(), x.size(), horizontal, useNaturalSpline);
  }
}

  void PowerTable::fillEmptyTable(int outerValue, const std::vector<int>& emptyIndices, const float* x, const float* y, size_t n, bool horizontal, bool useNaturalSpline) {
    int innerSize = horizontal ? POWERTABLE_WATT_SIZE : POWERTABLE_CAD_SIZE;

    if (n == 1) {  // If only one point, fill row with the value
        float singleValue = y[0];
        for (int innerValue : emptyIndices) {
            int i = horizontal ? outerValue : innerValue;
            int j = horizontal ? innerValue : outerValue;
            this->tableRow[i].tableEntry[j].targetPosition = static_cast<int>(std::round(singleValue));
        }
    } else if (n == 2) {  // If two points, do linear interpolation
        for (int innerValue : emptyIndices) {
            int i = horizontal ? outerValue : innerValue;
            int j = horizontal ? innerValue : outerValue;

            float interpolated_value = linearInterpolate(x, y, n, innerValue);
            int tempValue = static_cast<int>(std::round(interpolated_value));

            if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
                this->tableRow[i].tableEntry[j].targetPosition = tempValue;
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
        spline.set_points(x, y, n, useNaturalSpline);

        for (int innerValue : emptyIndices) {
            int i = horizontal ? outerValue : innerValue;
            int j = horizontal ? innerValue : outerValue;

            float interpolated_value = spline.interpolate(innerValue);

            float minValue = *std::min_element(y, y + n);
            float maxValue = *std::max_element(y, y + n);
            interpolated_value = std::max(minValue, std::min(maxValue, interpolated_value));

            int tempValue = static_cast<int>(std::round(interpolated_value));

            if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
                this->tableRow[i].tableEntry[j].targetPosition = tempValue;
            }
        }
    } else {
        SS2K_LOG(POWERTABLE_LOG_TAG, "Error: No unique points found.");
    }
  }

  void PowerTable::extrapFillTable() {
    extrapFillTableDirection(true);  // Horizontal
    extrapFillTableDirection(false); // Vertical
  }

  void PowerTable::extrapFillTableDirection(bool horizontal) {
    int outerSize = horizontal ? POWERTABLE_CAD_SIZE : POWERTABLE_WATT_SIZE;
    int innerSize = horizontal ? POWERTABLE_WATT_SIZE : POWERTABLE_CAD_SIZE;

    std::vector<std::pair<int, float>> unique_xy;
    std::vector<int> emptyIndices;
    std::vector<float> x, y;

    // Store previous data to reuse
    std::vector<float> prevX, prevY;
    std::vector<int> prevEmptyIndices;
    bool prevSplineValid = false;
    bool prevNaturalSpline = false;

    for (int outerIndex = 0; outerIndex < outerSize; ++outerIndex) {
        unique_xy.clear();
        emptyIndices.clear();
        x.clear();
        y.clear();

        int rangeStart = std::max(0, innerSize / 2 - 10);
        int rangeEnd = std::min(innerSize, innerSize / 2 + 10);

        // Collect data points
        for (int innerIndex = rangeStart; innerIndex < rangeEnd; ++innerIndex) {
            int i = horizontal ? outerIndex : innerIndex;
            int j = horizontal ? innerIndex : outerIndex;

            if (this->tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
                unique_xy.emplace_back(innerIndex, static_cast<float>(this->tableRow[i].tableEntry[j].targetPosition));
            } else {
                emptyIndices.push_back(innerIndex);
            }
        }

        if (unique_xy.size() < 2) continue; // Skip if not enough data

        std::sort(unique_xy.begin(), unique_xy.end());

        for (const auto& it : unique_xy) {
            x.push_back(it.first);
            y.push_back(it.second);
        }

        if (prevSplineValid && x == prevX && y == prevY && emptyIndices == prevEmptyIndices) {
            continue;
        }

        // Determine spline type (natural or clamped)
        bool useNaturalSpline = shouldUseNaturalSpline(x.data(), y.data(), x.size());

        prevX = x;
        prevY = y;
        prevEmptyIndices = emptyIndices;
        prevNaturalSpline = useNaturalSpline;
        prevSplineValid = true;

        // Fill empty table entries using the determined spline type
        extrapolateEmptyIndices(outerIndex, emptyIndices, x.data(), y.data(), x.size(), horizontal, useNaturalSpline);
    }
  }

  void PowerTable::extrapolateEmptyIndices(int outerIndex, const std::vector<int>& emptyIndices, const float* x, const float* y, size_t n, bool horizontal, bool naturalSpline) {
    int innerSize = horizontal ? POWERTABLE_WATT_SIZE : POWERTABLE_CAD_SIZE;

    if (n == 1) {
        int singleValue = static_cast<int>(std::round(y[0]));
        for (int innerIndex : emptyIndices) {
            int i = horizontal ? outerIndex : innerIndex;
            int j = horizontal ? innerIndex : outerIndex;
            this->tableRow[i].tableEntry[j].targetPosition = singleValue;
        }
    } else if (n == 2) {
        for (int innerIndex : emptyIndices) {
            int i = horizontal ? outerIndex : innerIndex;
            int j = horizontal ? innerIndex : outerIndex;

            float extrapolated_value = linearExtrapolate(x, y, n, innerIndex);
            int tempValue = static_cast<int>(std::round(extrapolated_value));

            if (testNeighbors(i, j, tempValue).allNeighborsPassed) {
                this->tableRow[i].tableEntry[j].targetPosition = tempValue;
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
        spline.set_points(x, y, n, naturalSpline); // Pass pointer-based data

        for (int innerIndex : emptyIndices) {
            int i = horizontal ? outerIndex : innerIndex;
            int j = horizontal ? innerIndex : outerIndex;

            float extrapolated_value = spline.extrapolate(innerIndex);
            float minVal = *std::min_element(y, y + n);
            float maxVal = *std::max_element(y, y + n);
            float range = maxVal - minVal;
            extrapolated_value = std::max(minVal - 0.1f * range, std::min(extrapolated_value, maxVal + 0.1f * range));
            int tempValue = static_cast<int>(std::round(extrapolated_value));

            if (testNeighbors(i, j, tempValue).allNeighborsPassed) {
                this->tableRow[i].tableEntry[j].targetPosition = tempValue;
            }
        }
    }
  }

  void PowerTable::extrapolateDiagonal() {
    std::vector<std::pair<float, float>> unique_xy;
    std::vector<std::pair<int, int>> emptyIndices;

    std::vector<float> prevX, prevY;
    std::vector<std::pair<int, int>> prevEmptyIndices;
    bool prevSplineValid = false;

    int midCAD = POWERTABLE_CAD_SIZE / 2;
    int midWATT = POWERTABLE_WATT_SIZE / 2;

    // Iterate over different diagonals (sum of indices is constant)
    for (int sum = 0; sum < POWERTABLE_CAD_SIZE + POWERTABLE_WATT_SIZE - 1; ++sum) {
        unique_xy.clear();
        emptyIndices.clear();

        int rangeStart = std::max(0, sum / 2 - 10);
        int rangeEnd = std::min(POWERTABLE_CAD_SIZE, sum / 2 + 10);

        // Collect known values for this diagonal
        for (int i = rangeStart; i < rangeEnd; ++i) {
            int j = sum - i;
            if (j >= 0 && j < POWERTABLE_WATT_SIZE) {
                if (this->tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
                    unique_xy.emplace_back(i, static_cast<float>(this->tableRow[i].tableEntry[j].targetPosition));
                } else {
                    emptyIndices.emplace_back(i, j);
                }
            }
        }

        if (unique_xy.size() < 2) continue; // Skip if not enough data

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
        prevX = x;
        prevY = y;
        prevEmptyIndices = emptyIndices;
        prevSplineValid = true;

        extrapolateDiagonalEntries(emptyIndices, x.data(), y.data(), x.size());
    }
  }

  void PowerTable::extrapolateDiagonalEntries(const std::vector<std::pair<int, int>>& emptyIndices, const float* x, const float* y, size_t n) {
    if (n == 1) {
        int singleValue = static_cast<int>(std::round(y[0]));
        for (const auto& it : emptyIndices) {
            this->tableRow[it.first].tableEntry[it.second].targetPosition = singleValue;
        }
        return;
    }

    if (n == 2) {
        for (const auto& it : emptyIndices) {
            int i = it.first;
            int j = it.second;

            float extrapolated_value = linearExtrapolate(x, y, n, i);
            int tempValue = static_cast<int>(std::round(extrapolated_value));

            if (testNeighbors(i, j, tempValue).allNeighborsPassed) {
                this->tableRow[i].tableEntry[j].targetPosition = tempValue;
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

            float minVal = *std::min_element(y, y + n);
            float maxVal = *std::max_element(y, y + n);
            float range = maxVal - minVal;
            extrapolated_value = std::max(minVal - 0.1f * range, std::min(extrapolated_value, maxVal + 0.1f * range));

            int tempValue = static_cast<int>(std::round(extrapolated_value));
            if (testNeighbors(i, j, tempValue).allNeighborsPassed) {
                this->tableRow[i].tableEntry[j].targetPosition = tempValue;
            }
        }
    }
  }

int PowerTable::getNumEntries() {
  int ret = 0;
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      if (this->tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
        ret++;
      }
    }
  }
  return ret;
}

void PowerTable::clean() {
  SS2K_LOG(POWERTABLE_LOG_TAG, "Clean Power Table");
  int ret = 0;
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      if (this->tableRow[i].tableEntry[j].readings < 1) {
        this->tableRow[i].tableEntry[j].targetPosition = INT16_MIN;
      }
    }
  }
}

void PowerTable::newEntry(PowerBuffer& powerBuffer) {
  // these are floats so that we make sure division works correctly.
  float watts          = 0;
  float cad            = 0;
  float targetPosition = 0;
  int avgPosition      = 0;

  // First, take the power buffer and average all of the samples together.
  int validEntries = 0;
  for (int i = 0; i < POWER_SAMPLES; i++) {
    if (powerBuffer.powerEntry[i].readings == 0) {
      // Stop when buffer is empty
      break;
    }

    // Accumulate values
    watts += powerBuffer.powerEntry[i].watts;
    cad += powerBuffer.powerEntry[i].cad;
    targetPosition += powerBuffer.powerEntry[i].targetPosition;
    validEntries++;
  }

  // Calculate the average if there are valid entries
  if (validEntries > 0) {
    watts /= validEntries;
    cad /= validEntries;
    targetPosition /= validEntries;
  } else {
    SS2K_LOG(POWERTABLE_LOG_TAG, "No valid entries in the power buffer.");
    return;
  }

  // clean previously extrapolated data so we don't fill with trash.
  this->clean();
  // To start working on the PowerTable, we need to calculate position in the table for the new entry
  int i = round(watts / (float)POWERTABLE_WATT_INCREMENT);
  int k = round((cad - (float)MINIMUM_TABLE_CAD) / (float)POWERTABLE_CAD_INCREMENT);
  SS2K_LOG(POWERTABLE_LOG_TAG, "Averaged Entry: watts=%f, cad=%f, targetPosition=%f, (%d)(%d)", watts, cad, targetPosition, k, i);

  // Ensure k is within valid range
  if ((k < 0) || (k > (POWERTABLE_CAD_SIZE - 1))) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Cad index was out of range %d", k);
    return;
  }
  // Ensure i is within valid range
  if (i < 0 || i > (POWERTABLE_WATT_SIZE - 1)) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Watt index was out of range %d max %d", i, POWERTABLE_WATT_SIZE - 1);
    return;
  }

  targetPosition = this->calculatePosition(watts, cad, targetPosition, k, i); 

  // Downvote out of position neighbors and discard entry if it doesn't match the logic of the table
  TestResults testResults = this->testNeighbors(k, i, targetPosition);
  if (!(testResults.bottomNeighbor.passedTest && testResults.topNeighbor.passedTest && testResults.rightNeighbor.passedTest && testResults.leftNeighbor.passedTest)) {

    // test which bit fields didn't match
    if (!testResults.leftNeighbor.passedTest) {
      avgPosition = (targetPosition + testResults.leftNeighbor.targetPosition) / 2;  // calculate the average

      SS2K_LOG(POWERTABLE_LOG_TAG, "Left failed at: (%d) Target pos: (%f) Avg pos: (%d)", testResults.leftNeighbor.targetPosition, targetPosition, avgPosition);

      if (testResults.leftNeighbor.targetPosition <= targetPosition + (500 * pow(TABLE_DIVISOR, -HORIZONTAL_NEIGHBOR_RANGE)) &&
          (int)targetPosition != testResults.leftNeighbor.targetPosition) {  // check if the cadence is the same and positions are within a set range in this case its 30.

        if (this->testNeighbors(k, i, avgPosition).allNeighborsPassed) {  // checks if the avg position with the current watts and cadence is valid
          SS2K_LOG(POWERTABLE_LOG_TAG, "Avg postion is valid with current cadence and watts! Avg position: %d", avgPosition);

          this->enterData(k, i, avgPosition);
        }

        if (this->testNeighbors(testResults.leftNeighbor.i, testResults.leftNeighbor.j, targetPosition).allNeighborsPassed) {  // check if the current position moved left is valid
          SS2K_LOG(POWERTABLE_LOG_TAG, "Current Position moved left was valid! Current position: %f", targetPosition);

          this->enterData(testResults.leftNeighbor.i, testResults.leftNeighbor.j, targetPosition);  // enter the data
        }

        if (this->testNeighbors(testResults.rightNeighbor.i, testResults.rightNeighbor.j, testResults.leftNeighbor.targetPosition)
                .allNeighborsPassed) {  // checks if the failed nighbor is valid with the right neighbors cadence and watts
          SS2K_LOG(POWERTABLE_LOG_TAG, "Left Neighbors position was valid with Right Neighbors cadence and watts! Left Neighbor Position: %d",
                   testResults.leftNeighbor.targetPosition);

          this->enterData(testResults.rightNeighbor.i, testResults.rightNeighbor.j, testResults.leftNeighbor.targetPosition);
        }

        // still downvote data if all the tests fail
        if (!((this->testNeighbors(testResults.leftNeighbor.i, testResults.leftNeighbor.j, targetPosition).allNeighborsPassed) ||
              (this->testNeighbors(k, i, avgPosition).allNeighborsPassed) ||
              (this->testNeighbors(testResults.rightNeighbor.i, testResults.rightNeighbor.j, testResults.leftNeighbor.targetPosition).allNeighborsPassed))) {
          SS2K_LOG(POWERTABLE_LOG_TAG, "All test failed left (%d)(%d)(%d), readings (%d)", testResults.leftNeighbor.i, testResults.leftNeighbor.j,
                   testResults.leftNeighbor.targetPosition, this->tableRow[testResults.leftNeighbor.i].tableEntry[testResults.leftNeighbor.j].readings);
          this->downVoteData(testResults.leftNeighbor.i, testResults.leftNeighbor.j, targetPosition, testResults.leftNeighbor.targetPosition);
        }
      } else {
        SS2K_LOG(POWERTABLE_LOG_TAG, "Was not in range failed left (%d)(%d)(%d), readings (%d)", testResults.leftNeighbor.i, testResults.leftNeighbor.j,
                 testResults.leftNeighbor.targetPosition, this->tableRow[testResults.leftNeighbor.i].tableEntry[testResults.leftNeighbor.j].readings);
        this->downVoteData(testResults.leftNeighbor.i, testResults.leftNeighbor.j, targetPosition, testResults.leftNeighbor.targetPosition);
      }
    }

    if (!testResults.rightNeighbor.passedTest) {
      avgPosition = (targetPosition + testResults.rightNeighbor.targetPosition) / 2;

      SS2K_LOG(POWERTABLE_LOG_TAG, "Right failed at: (%d) Target pos: (%f) Avg pos: (%d)", testResults.rightNeighbor.targetPosition, targetPosition, avgPosition);

      if (testResults.rightNeighbor.targetPosition >= targetPosition - (500 * pow(TABLE_DIVISOR, -HORIZONTAL_NEIGHBOR_RANGE)) &&
          (int)targetPosition != testResults.rightNeighbor.targetPosition) {
        if (this->testNeighbors(k, i, avgPosition).allNeighborsPassed) {
          SS2K_LOG(POWERTABLE_LOG_TAG, "Avg postion is valid with current cadence and watts! Avg position: %d", avgPosition);

          this->enterData(k, i, avgPosition);
        }

        if (this->testNeighbors(testResults.rightNeighbor.i, testResults.rightNeighbor.j, (float)targetPosition).allNeighborsPassed) {
          SS2K_LOG(POWERTABLE_LOG_TAG, "Current Position moved right was valid! Current position: %f", targetPosition);

          this->enterData(testResults.rightNeighbor.i, testResults.rightNeighbor.j, targetPosition);
        }

        if (this->testNeighbors(testResults.leftNeighbor.i, testResults.leftNeighbor.j, testResults.rightNeighbor.targetPosition).allNeighborsPassed) {
          SS2K_LOG(POWERTABLE_LOG_TAG, "Right Neighbors position was valid with Left Neighbors cadence and watts! Right Neighbor Position: %d",
                   testResults.rightNeighbor.targetPosition);

          this->enterData(testResults.leftNeighbor.i, testResults.leftNeighbor.j, testResults.rightNeighbor.targetPosition);
        }

        // still downvote data if all the tests fail
        if (!((this->testNeighbors(testResults.rightNeighbor.i, testResults.rightNeighbor.j, targetPosition).allNeighborsPassed) ||
              (this->testNeighbors(k, i, avgPosition).allNeighborsPassed) ||
              (this->testNeighbors(testResults.leftNeighbor.i, testResults.leftNeighbor.j, testResults.rightNeighbor.targetPosition).allNeighborsPassed))) {
          SS2K_LOG(POWERTABLE_LOG_TAG, "All test failed right (%d)(%d)(%d), readings (%d)", testResults.rightNeighbor.i, testResults.rightNeighbor.j,
                   testResults.rightNeighbor.targetPosition, this->tableRow[testResults.rightNeighbor.i].tableEntry[testResults.rightNeighbor.j].readings);
          this->downVoteData(testResults.rightNeighbor.i, testResults.rightNeighbor.j, targetPosition, testResults.rightNeighbor.targetPosition);
        }
      } else {
        SS2K_LOG(POWERTABLE_LOG_TAG, "Was not in range failed right (%d)(%d)(%d), readings (%d)", testResults.rightNeighbor.i, testResults.rightNeighbor.j,
                 testResults.rightNeighbor.targetPosition, this->tableRow[testResults.rightNeighbor.i].tableEntry[testResults.rightNeighbor.j].readings);
        this->downVoteData(testResults.rightNeighbor.i, testResults.rightNeighbor.j, targetPosition, testResults.rightNeighbor.targetPosition);
      }
    }

    if (!testResults.topNeighbor.passedTest) {
      avgPosition = (targetPosition + testResults.topNeighbor.targetPosition) / 2;

      SS2K_LOG(POWERTABLE_LOG_TAG, "Top failed at: (%d) Target pos: (%f) Avg pos: (%d)", testResults.topNeighbor.targetPosition, targetPosition, avgPosition);

      if (testResults.topNeighbor.targetPosition >= targetPosition - (500 * pow(TABLE_DIVISOR, -VERTICAL_NEIGHBOR_RANGE)) &&
          (int)targetPosition != testResults.topNeighbor.targetPosition) {
        if (this->testNeighbors(k, i, avgPosition).allNeighborsPassed) {
          SS2K_LOG(POWERTABLE_LOG_TAG, "Avg postion is valid with current cadence and watts! Avg position: %d", avgPosition);

          this->enterData(k, i, avgPosition);
        }

        if (this->testNeighbors(testResults.topNeighbor.i, testResults.topNeighbor.j, targetPosition).allNeighborsPassed) {
          SS2K_LOG(POWERTABLE_LOG_TAG, "Current Position moved up was valid! Current position: %f", targetPosition);

          this->enterData(testResults.topNeighbor.i, testResults.topNeighbor.j, targetPosition);
        }

        if (this->testNeighbors(testResults.bottomNeighbor.i, testResults.bottomNeighbor.j, testResults.topNeighbor.targetPosition).allNeighborsPassed) {
          SS2K_LOG(POWERTABLE_LOG_TAG, "Top Neighbors position was valid with Bottom Neighbors cadence and watts! Top Neighbor Position: %d",
                   testResults.topNeighbor.targetPosition);

          this->enterData(testResults.bottomNeighbor.i, testResults.bottomNeighbor.j, testResults.topNeighbor.targetPosition);
        }

        // still downvote data if all the tests fail
        if (!((this->testNeighbors(testResults.topNeighbor.i, testResults.topNeighbor.j, targetPosition).allNeighborsPassed) ||
              (this->testNeighbors(k, i, avgPosition).allNeighborsPassed) ||
              (this->testNeighbors(testResults.bottomNeighbor.i, testResults.bottomNeighbor.j, testResults.topNeighbor.targetPosition).allNeighborsPassed))) {
          SS2K_LOG(POWERTABLE_LOG_TAG, "All test failed top (%d)(%d)(%d), readings (%d)", testResults.topNeighbor.i, testResults.topNeighbor.j,
                   testResults.topNeighbor.targetPosition, this->tableRow[testResults.topNeighbor.i].tableEntry[testResults.topNeighbor.j].readings);
          this->downVoteData(testResults.topNeighbor.i, testResults.topNeighbor.j, targetPosition, testResults.topNeighbor.targetPosition);
        }
      } else {
        SS2K_LOG(POWERTABLE_LOG_TAG, "Was not in range failed top (%d)(%d)(%d), readings (%d)", testResults.topNeighbor.i, testResults.topNeighbor.j,
                 testResults.topNeighbor.targetPosition, this->tableRow[testResults.topNeighbor.i].tableEntry[testResults.topNeighbor.j].readings);
        this->downVoteData(testResults.topNeighbor.i, testResults.topNeighbor.j, targetPosition, testResults.topNeighbor.targetPosition);
      }
    }

    if (!testResults.bottomNeighbor.passedTest) {
      avgPosition = (targetPosition + testResults.bottomNeighbor.targetPosition) / 2;

      SS2K_LOG(POWERTABLE_LOG_TAG, "Bottom failed at: (%d) Target pos: (%f) Avg pos: (%d)", testResults.bottomNeighbor.targetPosition, targetPosition, avgPosition);

      if (testResults.bottomNeighbor.targetPosition <= targetPosition + (500 * pow(TABLE_DIVISOR, -VERTICAL_NEIGHBOR_RANGE)) &&
          (int)targetPosition != testResults.bottomNeighbor.targetPosition) {
        if (this->testNeighbors(k, i, avgPosition).allNeighborsPassed) {
          SS2K_LOG(POWERTABLE_LOG_TAG, "Avg postion is valid with current cadence and watts! Avg position: %d", avgPosition);

          this->enterData(k, i, avgPosition);
        }

        if (this->testNeighbors(testResults.bottomNeighbor.i, testResults.bottomNeighbor.j, targetPosition).allNeighborsPassed) {
          SS2K_LOG(POWERTABLE_LOG_TAG, "Current Position moved down is valid! Current position: %f", targetPosition);

          this->enterData(testResults.bottomNeighbor.i, testResults.bottomNeighbor.j, targetPosition);
        }

        if (this->testNeighbors(testResults.topNeighbor.i, testResults.topNeighbor.j, testResults.bottomNeighbor.targetPosition).allNeighborsPassed) {
          SS2K_LOG(POWERTABLE_LOG_TAG, "Bottom Neighbors position was valid with Top Neighbors cadence and watts! Bottom Neighbor Position: %d",
                   testResults.topNeighbor.targetPosition);

          this->enterData(testResults.topNeighbor.i, testResults.topNeighbor.j, testResults.bottomNeighbor.targetPosition);
        }

        // still downvote data if all the tests fail
        if (!((this->testNeighbors(testResults.bottomNeighbor.i, testResults.bottomNeighbor.j, targetPosition).allNeighborsPassed) ||
              (this->testNeighbors(k, i, avgPosition).allNeighborsPassed) ||
              (this->testNeighbors(testResults.topNeighbor.i, testResults.topNeighbor.j, testResults.bottomNeighbor.targetPosition).allNeighborsPassed))) {
          SS2K_LOG(POWERTABLE_LOG_TAG, "All test failed bottom (%d)(%d)(%d), readings (%d)", testResults.topNeighbor.i, testResults.topNeighbor.j,
                   testResults.topNeighbor.targetPosition, this->tableRow[testResults.topNeighbor.i].tableEntry[testResults.topNeighbor.j].readings);
          this->downVoteData(testResults.bottomNeighbor.i, testResults.bottomNeighbor.j, targetPosition, testResults.bottomNeighbor.targetPosition);
        }
      } else {
        SS2K_LOG(POWERTABLE_LOG_TAG, "Was not in range failed bottom (%d)(%d)(%d), readings (%d)", testResults.bottomNeighbor.i, testResults.bottomNeighbor.j,
                 testResults.bottomNeighbor.targetPosition, this->tableRow[testResults.bottomNeighbor.i].tableEntry[testResults.bottomNeighbor.j].readings);
        this->downVoteData(testResults.bottomNeighbor.i, testResults.bottomNeighbor.j, targetPosition, testResults.bottomNeighbor.targetPosition);
      }
    }
    return;
  }
  

  this->enterData(k, i, (int)targetPosition);
  BLE_ss2kCustomCharacteristic::notify(0x27, k);
}

/**
 * @brief Updates or enters data into the power table for a specific row and entry.
 * 
 * This function records a new target position or averages the new position with 
 * existing data for a specific table entry. It ensures that the number of readings 
 * does not exceed a defined limit to prevent dilution of recent data. Additionally, 
 * it triggers table filling and extrapolation processes if the number of entries 
 * exceeds a threshold.
 * 
 * @param k The index of the table row to update.
 * @param i The index of the table entry within the row to update.
 * @param pos The new target position to record or average.
 */
void PowerTable::enterData(int k, int i, int pos) {
  if (this->tableRow[k].tableEntry[i].readings <= 0) {  // if first reading in this entry
    this->tableRow[k].tableEntry[i].targetPosition = pos;
    SS2K_LOG(POWERTABLE_LOG_TAG, "New entry recorded (%d)(%d)(%d)", k, i, this->tableRow[k].tableEntry[i].targetPosition);
  } else {  // Average and update the readings.
    this->tableRow[k].tableEntry[i].targetPosition =
        (pos + (this->tableRow[k].tableEntry[i].targetPosition * this->tableRow[k].tableEntry[i].readings)) / (this->tableRow[k].tableEntry[i].readings + 1.0);
    SS2K_LOG(POWERTABLE_LOG_TAG, "Existing entry averaged (%d)(%d)(%d), readings(%d)", k, i, this->tableRow[k].tableEntry[i].targetPosition,
             this->tableRow[k].tableEntry[i].readings);
    if (this->tableRow[k].tableEntry[i].readings > POWER_SAMPLES * 2) {
      this->tableRow[k].tableEntry[i].readings = POWER_SAMPLES * 2;  // keep from diluting recent readings too far.
    }
  }
  this->tableRow[k].tableEntry[i].readings++;

  if (this->getNumEntries() > 4) {
    int entries    = 0;
    int newEntries = 1;
    // loop until we can't calculate any new data
    while (entries < newEntries) {
      entries = newEntries;
      this->fillTable();
      this->extrapFillTable();
      this->extrapolateDiagonal();
      newEntries = getNumEntries();
    }
  }
}

/**
 * @brief Calculates the target position for a given power table entry based on neighboring values.
 *
 * This function determines the new target position for a power table entry by analyzing its neighbors
 * and applying weighted adjustments based on the differences in watts and cadence. If the current entry
 * is invalid or does not pass neighbor tests, the function retains the old target position.
 *
 * @param watts The power in watts for the current entry.
 * @param cad The cadence in RPM for the current entry.
 * @param targetPos The current target position to be adjusted.
 * @param k The cadence index in the power table.
 * @param i The watt index in the power table.
 * @return The calculated target position after applying adjustments based on neighbors.
 */
float PowerTable::calculatePosition(float watts, float cad, float targetPos, int k, int i) {

  TestResults testResults = this->testNeighbors(k, i, targetPos); 

  if(this->tableRow[k].tableEntry[i].targetPosition == INT16_MIN || 
    !(testResults.bottomNeighbor.passedTest || testResults.topNeighbor.passedTest || testResults.rightNeighbor.passedTest || testResults.leftNeighbor.passedTest) ||
    this->tableRow[k].tableEntry[i].targetPosition == targetPos) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Keep old targetPosition: (%f)", targetPos); 
    return targetPos; 
  }

  int wattPosition = POWERTABLE_WATT_INCREMENT * i; 
  int cadPosition = MINIMUM_TABLE_CAD + (POWERTABLE_CAD_INCREMENT * k); 

  float rightValue = 0.0f, leftValue = 0.0f, topValue = 0.0f, bottomValue = 0.0f; 

  int cellValue = this->tableRow[k].tableEntry[i].targetPosition; 
  float wattDelta = float(POWERTABLE_WATT_INCREMENT)/(abs(targetPos - float(cellValue)));
  float cadDelta = float(POWERTABLE_CAD_INCREMENT)/(abs(targetPos - float(cellValue))); 

  SS2K_LOG(POWERTABLE_LOG_TAG, "cellValue: (%d) wattDelta: (%f) cadDelta: (%f) allNeighborsPassed: (%d)", cellValue, wattDelta, cadDelta, testResults.allNeighborsPassed); 

  int count = 0; 

  if(testResults.rightNeighbor.passedTest){
    float x = abs(watts - float(wattPosition + POWERTABLE_WATT_INCREMENT)); 
    rightValue = targetPos - (x/wattDelta); 
    count++; 
  } 
  if(testResults.leftNeighbor.passedTest){
    float x = abs(watts - float(wattPosition - POWERTABLE_WATT_INCREMENT)); 
    leftValue = targetPos - (x/wattDelta); 
    count++; 
  }
  if(testResults.bottomNeighbor.passedTest){
    float x = abs(cad - float(cadPosition + POWERTABLE_CAD_INCREMENT)); 
    bottomValue = targetPos - (x/cadDelta); 
    count++; 
  }
  if(testResults.topNeighbor.passedTest){
    float x = abs(cad - float(cadPosition - POWERTABLE_CAD_INCREMENT)); 
    topValue = targetPos - (x/cadDelta);
    count++;  
  }

  targetPos = (rightValue + leftValue + topValue + bottomValue)/float(count);
  SS2K_LOG(POWERTABLE_LOG_TAG, "rightValue: (%f) leftValue: (%f) bottomValue: (%f) topValue: (%f) New averaged targetPosition: (%f) count: (%d)", 
  rightValue, leftValue, bottomValue, topValue, targetPos, count); 

  return targetPos; 
}

/**
 * @brief Calculates a penalty value for downvoting a neighbor entry in the power table.
 *
 * This function computes a penalty based on the difference between the target value
 * and the neighbor value. The penalty is scaled by a predefined penalty factor and
 * is used to reduce the reliability of a neighbor entry when it fails validation.
 *
 * @param targetValue The target position value being evaluated.
 * @param neighborValue The neighbor position value being compared.
 * @return The calculated penalty value to be applied to the neighbor entry.
 */
int weightedDownVote(int targetValue, int neighborValue) {
  // calculate diff between target and neighbor
  int delta = abs(targetValue - neighborValue);
  int penalty;
  float penaltyFactor = 0.2;

  SS2K_LOG(POWERTABLE_LOG_TAG, "WEIGHTED DOWNVOTING: Target Value: (%d), NeighborValue: (%d)", targetValue, neighborValue);

  penalty = (delta * penaltyFactor);

  SS2K_LOG(POWERTABLE_LOG_TAG, "WEIGHTED DOWNVOTING: Delta: (%d), Penalty: (%d)", delta, penalty);
  return penalty;
}

void PowerTable::downVoteData(int i, int j, float target, int neighbor) {
  // determine penalty amount before applying to failed neighbor
  int penalty = weightedDownVote(target, neighbor);

  if (this->tableRow[i].tableEntry[j].readings < penalty) {
    this->tableRow[i].tableEntry[j].readings = 0;
  } else {
    this->tableRow[i].tableEntry[j].readings -= penalty;
  }
  SS2K_LOG(POWERTABLE_LOG_TAG, "PT failed (%d)(%d)(%d), readings (%d)", i, j, neighbor, this->tableRow[i].tableEntry[j].readings);
}

bool PowerTable::_manageSaveState(bool canSkipReliabilityChecks) {
  // Check if the table has been loaded in this session
  if (!this->_hasBeenLoadedThisSession) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Loading Power Table....");
    File file = LittleFS.open(POWER_TABLE_FILENAME, FILE_READ);
    if (!file) {
      SS2K_LOG(POWERTABLE_LOG_TAG, "Failed to Load Power Table.");
      file.close();
      this->_save();
      return false;
    }

    // Read version and size
    int version;
    file.read((uint8_t*)&version, sizeof(version));
    int savedQuality;
    file.read((uint8_t*)&savedQuality, sizeof(savedQuality));
    bool savedHomed;
    file.read((uint8_t*)&savedHomed, sizeof(savedHomed));

    // If both current and saved tables were created with homing, we can skip position reliability checks
    if (!canSkipReliabilityChecks) {
      canSkipReliabilityChecks = savedHomed && rtConfig->getHomed();
    }

    if (version != TABLE_VERSION) {
      SS2K_LOG(POWERTABLE_LOG_TAG, "Expected power table version %d, found version %d", TABLE_VERSION, version);
      file.close();
      this->_save();
      return false;
    }

    // Is the data we are working with better than the saved file?
    int activeReadings = this->getNumReadings();
    if (activeReadings > savedQuality) {
      SS2K_LOG(POWERTABLE_LOG_TAG, "Active table had a reliability of %d, vs %d for the saved file. Overwriting save.", activeReadings, savedQuality);
      file.close();
      this->_save();
    }

    SS2K_LOG(POWERTABLE_LOG_TAG, "Loading power table version %d, Size %d, Homed %d", version, savedQuality, savedHomed);

    if (!canSkipReliabilityChecks) {
      // Initialize a counter for reliable positions
      int reliablePositions = 0;

      // Check if we have at least 3 reliable positions in the active table in order to determine a reliable offset to load the saved table
      for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
        for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
          int16_t savedTargetPosition = INT16_MIN;
          int8_t savedReadings        = 0;
          file.read((uint8_t*)&savedTargetPosition, sizeof(savedTargetPosition));
          file.read((uint8_t*)&savedReadings, sizeof(savedReadings));
          // Does the saved file have a position that the active session has also recorded?
          // We start comparing at watt position 3 (j>2) because low resistance positions are notoriously unreliable.
          if ((j > 2) && (this->tableRow[i].tableEntry[j].targetPosition != INT16_MIN) && (this->tableRow[i].tableEntry[j].readings > MINIMUM_RELIABLE_POSITIONS) &&
              (savedReadings > 0)) {
            reliablePositions++;
          }
        }
      }
      if (reliablePositions < MINIMUM_RELIABLE_POSITIONS) {  // Do we have enough active data in order to calculate a (good) offset when we load the new table?
        SS2K_LOG(POWERTABLE_LOG_TAG, "Not enough matching positions to load the Power Table. %d of %d needed.", reliablePositions, MINIMUM_RELIABLE_POSITIONS);
        file.close();
        return false;
      }
    }
    file.close();

    // We passed our checks to load, lets load the saved table into active memory
    file = LittleFS.open(POWER_TABLE_FILENAME, FILE_READ);
    if (!file) {
      SS2K_LOG(POWERTABLE_LOG_TAG, "Failed to Load Power Table. Resetting the save.");
      file.close();
      this->_save();
      return false;
    }

    // get these reads done, so that we're in the right position to read the data from the file.
    file.read((uint8_t*)&version, sizeof(version));
    file.read((uint8_t*)&savedQuality, sizeof(savedQuality));
    file.read((uint8_t*)&savedHomed, sizeof(savedHomed));

    float averageOffset = 0;
    if (!canSkipReliabilityChecks) {
      std::vector<float> offsetDifferences;
      int reliablePositions = 0;
      // Read table entries and calculate offsets
      for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
        for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
          int16_t savedTargetPosition = INT16_MIN;
          int8_t savedReadings        = 0;
          file.read((uint8_t*)&savedTargetPosition, sizeof(savedTargetPosition));
          file.read((uint8_t*)&savedReadings, sizeof(savedReadings));
          if ((this->tableRow[i].tableEntry[j].targetPosition != INT16_MIN) && (savedTargetPosition != INT16_MIN) && (savedReadings > 0) &&
              (this->tableRow[i].tableEntry[j].readings > MINIMUM_RELIABLE_POSITIONS)) {
            int offset = this->tableRow[i].tableEntry[j].targetPosition - savedTargetPosition;
            offsetDifferences.push_back(offset);
            SS2K_LOG(POWERTABLE_LOG_TAG, "offset %d", offset);
            reliablePositions++;
          }
          this->tableRow[i].tableEntry[j].targetPosition = savedTargetPosition;
          this->tableRow[i].tableEntry[j].readings       = savedReadings;
        }
      }
      if (!offsetDifferences.empty() && offsetDifferences.size() >= MINIMUM_RELIABLE_POSITIONS) {
        averageOffset = std::accumulate(offsetDifferences.begin(), offsetDifferences.end(), 0.0) / offsetDifferences.size();
      } else {
        // Default value or handle empty case
        averageOffset = 0;
        SS2K_LOG(POWERTABLE_LOG_TAG, "Warning: No valid offset differences found");
      }
    } else {
      // If both tables were created with homing, just load the values directly
      for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
        for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
          int16_t savedTargetPosition = INT16_MIN;
          int8_t savedReadings        = 0;
          file.read((uint8_t*)&savedTargetPosition, sizeof(savedTargetPosition));
          file.read((uint8_t*)&savedReadings, sizeof(savedReadings));
          this->tableRow[i].tableEntry[j].targetPosition = savedTargetPosition;
          this->tableRow[i].tableEntry[j].readings       = savedReadings;
        }
      }
      SS2K_LOG(POWERTABLE_LOG_TAG, "Both tables were created with homing, loaded values directly");
    }

    file.close();

    // Apply the offset if needed
    if (!canSkipReliabilityChecks) {
      for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
        for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
          if (this->tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
            this->tableRow[i].tableEntry[j].targetPosition += averageOffset;
          }
        }
      }
      SS2K_LOG(POWERTABLE_LOG_TAG, "Power Table loaded with an offset of %d.", averageOffset);
    }

    // set the flag so it isn't loaded again this session.
    this->_hasBeenLoadedThisSession = true;
  }

  // Implement saving on a timer
  if ((millis() - lastSaveTime) > POWER_TABLE_SAVE_INTERVAL) {
    this->_save();
    lastSaveTime = millis();
  }
  return true;
}

bool PowerTable::_save() {
  // print littleFS free space and all file sizes on partition
  Serial.printf("LittleFS Total Bytes:%d, Used Bytes:%d", LittleFS.totalBytes(), LittleFS.usedBytes());

  // Count valid readings before saving
  int validReadings = getNumReadings();

  // Only proceed with saving if we have enough data to make the file useful
  if (validReadings < 1) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Not enough valid readings to save power table (%d)", validReadings);
    return false;
  }

  // Delete existing file to avoid appending
  LittleFS.remove(POWER_TABLE_FILENAME);

  // Open file for writing
  SS2K_LOG(POWERTABLE_LOG_TAG, "Writing File: %s", POWER_TABLE_FILENAME);
  File file = LittleFS.open(POWER_TABLE_FILENAME, FILE_WRITE);
  if (!file) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Failed to create file");
    return false;
  }

  // Write version and size
  int version = TABLE_VERSION;
  if (file.write((uint8_t*)&version, sizeof(version)) != sizeof(version)) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Failed to write version");
    file.close();
    return false;
  }

  int size = validReadings;
  if (file.write((uint8_t*)&size, sizeof(size)) != sizeof(size)) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Failed to write size");
    file.close();
    return false;
  }

  // Write homing state
  bool isHomed = rtConfig->getHomed();
  if (file.write((uint8_t*)&isHomed, sizeof(isHomed)) != sizeof(isHomed)) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Failed to write homing state");
    file.close();
    return false;
  }

  // Write table entries
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      // Check write operations for success
      if (file.write((uint8_t*)&this->tableRow[i].tableEntry[j].targetPosition, sizeof(this->tableRow[i].tableEntry[j].targetPosition)) !=
          sizeof(this->tableRow[i].tableEntry[j].targetPosition)) {
        SS2K_LOG(POWERTABLE_LOG_TAG, "Failed to write table entry position at [%d][%d]", i, j);
        file.close();
        return false;
      }

      if (file.write((uint8_t*)&this->tableRow[i].tableEntry[j].readings, sizeof(this->tableRow[i].tableEntry[j].readings)) != sizeof(this->tableRow[i].tableEntry[j].readings)) {
        SS2K_LOG(POWERTABLE_LOG_TAG, "Failed to write table entry readings at [%d][%d]", i, j);
        file.close();
        return false;
      }

      // log the raw data directly to serial
      Serial.printf("%d, %d ", this->tableRow[i].tableEntry[j].targetPosition, this->tableRow[i].tableEntry[j].readings);
    }
    Serial.printf("\n");
  }
  // Close the file
  file.close();
  Serial.printf("file Size %lu\n", file.size());
  lastSaveTime                    = millis();
  this->_hasBeenLoadedThisSession = true;
  SS2K_LOG(POWERTABLE_LOG_TAG, "Power table saved successfully with %d readings", validReadings);
  return true;  // return successful
}

int32_t PowerTable::lookupWatts(int cad, int32_t targetPosition) {
  if(cad<1) {
    return 0;
  }
  // Convert targetPosition from external format (xTABLE_DIVISOR) to internal format
  int16_t internalPosition = targetPosition / TABLE_DIVISOR;

  // Calculate cadence index
  int cadIndex = round(((float)cad - (float)MINIMUM_TABLE_CAD) / (float)POWERTABLE_CAD_INCREMENT);

  // Clamp cadence index to table limits
  if (cadIndex < 0) {
    cadIndex = 0;
  } else if (cadIndex >= POWERTABLE_CAD_SIZE) {
    cadIndex = POWERTABLE_CAD_SIZE - 1;
  }

  // Find closest positions and corresponding watts in the row
  int leftWattIndex  = -1;
  int rightWattIndex = -1;

  // Search for closest positions
  for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
    if (this->tableRow[cadIndex].tableEntry[j].targetPosition != INT16_MIN) {
      if (this->tableRow[cadIndex].tableEntry[j].targetPosition <= internalPosition) {
        leftWattIndex = j;
      } else {
        rightWattIndex = j;
        break;
      }
    }
  }

  // If we found valid positions on both sides, interpolate
  if (leftWattIndex != -1 && rightWattIndex != -1) {
    int leftPos    = this->tableRow[cadIndex].tableEntry[leftWattIndex].targetPosition;
    int rightPos   = this->tableRow[cadIndex].tableEntry[rightWattIndex].targetPosition;
    int leftWatts  = leftWattIndex * POWERTABLE_WATT_INCREMENT;
    int rightWatts = rightWattIndex * POWERTABLE_WATT_INCREMENT;
    // Divide by 0 safety
    if (rightPos == leftPos) {
      return leftWatts;
    }
    // Linear interpolation
    int watts = leftWatts + (rightWatts - leftWatts) * (internalPosition - leftPos) / (rightPos - leftPos);
    SS2K_LOG(POWERTABLE_LOG_TAG, "LookupWatts interpolated %dw from pos %d, cad %d", watts, targetPosition, cad);
    return watts;
  }

  // If we only found positions on one side, extrapolate
  if (leftWattIndex != -1 && leftWattIndex > 0) {
    // Extrapolate using two leftmost points
    int pos1   = this->tableRow[cadIndex].tableEntry[leftWattIndex - 1].targetPosition;
    int pos2   = this->tableRow[cadIndex].tableEntry[leftWattIndex].targetPosition;
    int watts1 = (leftWattIndex - 1) * POWERTABLE_WATT_INCREMENT;
    int watts2 = leftWattIndex * POWERTABLE_WATT_INCREMENT;

    // divide by zero safety for pos1 == pos2
    if (pos1 == pos2) {
      return watts2;
    }

    int watts = watts2 + (watts2 - watts1) * (internalPosition - pos2) / (pos2 - pos1);
    SS2K_LOG(POWERTABLE_LOG_TAG, "LookupWatts extrapolated high %dw from pos %d, cad %d", watts, targetPosition, cad);
    return watts;
  }

  if (rightWattIndex != -1 && rightWattIndex < POWERTABLE_WATT_SIZE - 1) {
    // Extrapolate using two rightmost points
    int pos1   = this->tableRow[cadIndex].tableEntry[rightWattIndex].targetPosition;
    int pos2   = this->tableRow[cadIndex].tableEntry[rightWattIndex + 1].targetPosition;
    int watts1 = rightWattIndex * POWERTABLE_WATT_INCREMENT;
    int watts2 = (rightWattIndex + 1) * POWERTABLE_WATT_INCREMENT;

    // divide by zero safety for pos1 == pos2
    if (pos1 == pos2) {
      return watts2;
    }
    int watts = watts1 + (watts1 - watts2) * (pos1 - internalPosition) / (pos1 - pos2);
    SS2K_LOG(POWERTABLE_LOG_TAG, "LookupWatts extrapolated low %dw from pos %d, cad %d", watts, targetPosition, cad);
    return watts;
  }
  SS2K_LOG(POWERTABLE_LOG_TAG, "LookupWatts failed to find a value for pos %d, cad %d", targetPosition, cad);
  return RETURN_ERROR;
}

// Reset the PowerTable to 0;
bool PowerTable::reset() {
  ss2k->resetPowerTableFlag = false;
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      this->tableRow[i].tableEntry[j].targetPosition = INT16_MIN;
      this->tableRow[i].tableEntry[j].readings       = 0;
    }
  }
  File file = LittleFS.open(POWER_TABLE_FILENAME, FILE_READ);
  if (!file) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Failed to Load Power Table.");
    file.close();
    this->_save();
    return false;
  }
  file.close();
  this->_save();
  return true;
}

void PowerTable::toLog() {
#ifdef DEBUG_POWERTABLE
  int maxLen = 4;
  // Find the longest integer to dynamically size the table
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      if (this->tableRow[i].tableEntry[j].targetPosition == INT16_MIN) {
        continue;
      }
      int len = snprintf(nullptr, 0, "%d", this->tableRow[i].tableEntry[j].targetPosition);
      if (maxLen < len) {
        maxLen = len;
      }
    }
  }

  char buffer[maxLen + 2];  // Buffer for formatting
  // Print header row
  String headerRow = "CAD\\W ";
  for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
    snprintf(buffer, sizeof(buffer), "%*d", maxLen, j * POWERTABLE_WATT_INCREMENT);
    headerRow += String(" | ") + buffer;
  }
  SS2K_LOG(POWERTABLE_LOG_TAG, "%s", headerRow.c_str());

  // Print each row of the table
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    String logString = String(i * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD) + " rpm";
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      int targetPosition = this->tableRow[i].tableEntry[j].targetPosition;
      if (targetPosition == INT16_MIN) {
        snprintf(buffer, sizeof(buffer), "%*s", maxLen, " ");
      } else {
        snprintf(buffer, sizeof(buffer), "%*d", maxLen, targetPosition);
      }
      logString += String(" | ") + buffer;
    }
    SS2K_LOG(POWERTABLE_LOG_TAG, "%s", logString.c_str());
  }
#endif
}

int PowerTable::getNumReadings() {
  int ret = 0;
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      if (this->tableRow[i].tableEntry[j].readings > 0) {
        ret++;
      }
    }
  }
  return ret;
}