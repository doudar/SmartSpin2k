/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Power_Table.h"
#include "SS2KLog.h"
#include "spline.h"
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
        int cad1          = extrapRow1 * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD;
        int cad2          = extrapRow2 * POWERTABLE_CAD_INCREMENT + MINIMUM_TABLE_CAD;
        int val1          = this->tableRow[extrapRow1].tableEntry[wattIndex].targetPosition;
        int val2          = this->tableRow[extrapRow2].tableEntry[wattIndex].targetPosition;
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
        int watts1        = extrapCol1 * POWERTABLE_WATT_INCREMENT;
        int watts2        = extrapCol2 * POWERTABLE_WATT_INCREMENT;
        int val1          = this->tableRow[cadIndex].tableEntry[extrapCol1].targetPosition;
        int val2          = this->tableRow[cadIndex].tableEntry[extrapCol2].targetPosition;
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

void PowerTable::fillTable() {
  // Horizontal Interpolation
  for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
    std::map<double, double> unique_xy;
    std::vector<int> emptyIndices;

    // Collect data points
    for (int j = 0; j < POWERTABLE_WATT_SIZE; ++j) {
        if (this->tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
            unique_xy[j] = static_cast<double>(this->tableRow[i].tableEntry[j].targetPosition);
        } else {
            emptyIndices.push_back(j);
        }
    }

    if (unique_xy.size() < 2) continue; // Skip if not enough data

    std::vector<double> x, y;
    for (const auto& it : unique_xy) {
        x.push_back(it.first);
        y.push_back(it.second);
    }

      if (x.size() == 1) {
          // If we only have one unique point, we fill the row with that value. Good for keeping data
          double singleValue = y.front();
          for (int j : emptyIndices) {
              this->tableRow[i].tableEntry[j].targetPosition = static_cast<int>(std::round(singleValue));
          }
          continue;
      } 
      if (x.size() == 2) {
          // If only 2 unique points, we perform linear interpolation

          for (int j : emptyIndices) {

            if (j < x.front() || j > x.back()) continue; 

            double interpolated_value = y[0] + (y[1] - y[0]) * (j - x[0]) / (x[1] - x[1]); //interpolation formula 

            double minValue = *std::min_element(y.begin(), y.end());
            double maxValue = *std::max_element(y.begin(), y.end()); 

              interpolated_value = std::max(minValue, std::min(maxValue, interpolated_value));

              int tempValue = static_cast<int>(std::round(interpolated_value));

              if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
                  this->tableRow[i].tableEntry[j].targetPosition = tempValue;
              }
          }
          continue;
      } 
      if (x.size() >= 3) {
          // If we have 3 unique points, then we can perform cubic spline
          tk::spline s; // Initialize s for using in cubic spline
          s.set_points(x, y, tk::spline::cspline);
          for (int j : emptyIndices) {

            if (j < x.front() || j > x.back()) continue; 

            double interpolated_value = s(j);
            double minValue = *std::min_element(y.begin(), y.end());
            double maxValue = *std::max_element(y.begin(), y.end()); 

            interpolated_value = std::max(minValue, std::min(maxValue, interpolated_value));
  
            int tempValue = static_cast<int>(std::round(interpolated_value));
  
            if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
                this->tableRow[i].tableEntry[j].targetPosition = tempValue;
            }
        }
      } 

  }

  // Vertical Interpolation
  for (int j = 0; j < POWERTABLE_WATT_SIZE; ++j) {
    std::map<double, double> unique_xy;
    std::vector<int> emptyIndices;

    // Collect data points
    for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
        if (this->tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
            unique_xy[i] = static_cast<double>(this->tableRow[i].tableEntry[j].targetPosition);
        } else {
            emptyIndices.push_back(i);
        }
    }

    if (unique_xy.size() < 2) continue;

    std::vector<double> x, y;
    for (const auto& it : unique_xy) {
        x.push_back(it.first);
        y.push_back(it.second);
    }

      if (x.size() == 1) {
          double singleValue = y.front();
          for (int i : emptyIndices) {
              this->tableRow[i].tableEntry[j].targetPosition = static_cast<int>(std::round(singleValue));
          }
          continue;
      } 
      if (x.size() == 2) {

          for (int i : emptyIndices) {

            if (i < x.front() || i > x.back()) continue; 

            double interpolated_value = y[0] + (y[1] - y[0]) * (i - x[0]) / (x[1] - x[1]); //interpolation formula 

            double minValue = *std::min_element(y.begin(), y.end());
            double maxValue = *std::max_element(y.begin(), y.end()); 

            interpolated_value = std::max(minValue, std::min(maxValue, interpolated_value));

            int tempValue = static_cast<int>(std::round(interpolated_value));

              if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
                  this->tableRow[i].tableEntry[j].targetPosition = tempValue;
              }
          }
          continue;
      } 
      if (x.size() >= 3) {
          tk::spline s; // Initialize s for using in cubic spline
          s.set_points(x, y, tk::spline::cspline);
          for (int i : emptyIndices) {

            if (i < x.front() || i > x.back()) continue; 

            double interpolated_value = s(i);
            double minValue = *std::min_element(y.begin(), y.end());
            double maxValue = *std::max_element(y.begin(), y.end()); 

            interpolated_value = std::max(minValue, std::min(maxValue, interpolated_value));
            int tempValue = static_cast<int>(std::round(interpolated_value));
  
            if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
                this->tableRow[i].tableEntry[j].targetPosition = tempValue;
            }
        }
      }
  }
}


//Old Fill Function 
// void PowerTable::fillTable() {
//   int tempValue = INT16_MIN;

//   // Fill each empty cell by linear interpolation
//   for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
//     // Interpolate horizontally
//     for (int j = 0; j < POWERTABLE_WATT_SIZE; ++j) {
//       if (this->tableRow[i].tableEntry[j].targetPosition == INT16_MIN) {
//         // Find nearest left and right non-empty cells
//         int left = j - 1;
//         while (left >= 0 && this->tableRow[i].tableEntry[left].targetPosition == INT16_MIN) left--;
//         int right = j + 1;
//         while (right < POWERTABLE_WATT_SIZE && this->tableRow[i].tableEntry[right].targetPosition == INT16_MIN) right++;

//         if (left >= 0 && right < POWERTABLE_WATT_SIZE) {
//           // Linear interpolation
//           tempValue = this->tableRow[i].tableEntry[left].targetPosition +
//                       (this->tableRow[i].tableEntry[right].targetPosition - this->tableRow[i].tableEntry[left].targetPosition) * (j - left) / (right - left);
//           if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
//             this->tableRow[i].tableEntry[j].targetPosition = tempValue;
//           }
//         }
//       }
//     }
//   }

//   for (int j = 0; j < POWERTABLE_WATT_SIZE; ++j) {
//     // Interpolate vertically
//     for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
//       if (this->tableRow[i].tableEntry[j].targetPosition == INT16_MIN) {
//         // Find nearest top and bottom non-empty cells
//         int top = i - 1;
//         while (top >= 0 && this->tableRow[top].tableEntry[j].targetPosition == INT16_MIN) top--;
//         int bottom = i + 1;
//         while (bottom < POWERTABLE_CAD_SIZE && this->tableRow[bottom].tableEntry[j].targetPosition == INT16_MIN) bottom++;

//         if (top >= 0 && bottom < POWERTABLE_CAD_SIZE) {
//           // Linear interpolation
//           tempValue = this->tableRow[top].tableEntry[j].targetPosition +
//                       (this->tableRow[bottom].tableEntry[j].targetPosition - this->tableRow[top].tableEntry[j].targetPosition) * (i - top) / (bottom - top);
//           if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
//             this->tableRow[i].tableEntry[j].targetPosition = tempValue;
//           }
//         }
//       }
//     }
//   }
// }

void PowerTable::extrapFillTable() {
  // Find the center of the known data
  int sumRow = 0, sumCol = 0, count = 0;
  for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; ++j) {
      if (this->tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
        sumRow += i;
        sumCol += j;
        count++;
      }
    }
  }

  // prevent divide by zero
  if (count == 0) {
    return;
  }

  int centerRow = sumRow / count;
  int centerCol = sumCol / count;
  int tempValue = INT16_MIN;

  // Function to extrapolate a single cell based on its neighbors
  auto extrapolateCell = [&](int i, int j) {
    // Find nearest left non-empty cell
    int left = j - 1;
    while (left >= 0 && this->tableRow[i].tableEntry[left].targetPosition == INT16_MIN) left--;

    // Find nearest right non-empty cell
    int right = j + 1;
    while (right < POWERTABLE_WATT_SIZE && this->tableRow[i].tableEntry[right].targetPosition == INT16_MIN) right++;

    if (left >= 0 && right < POWERTABLE_WATT_SIZE) {
      // Linear extrapolation
      if (this->tableRow[i].tableEntry[left].targetPosition != INT16_MIN && this->tableRow[i].tableEntry[right].targetPosition != INT16_MIN) {
        if (j < left) {
          // Extrapolate to the left
          tempValue = this->tableRow[i].tableEntry[left].targetPosition -
                      (this->tableRow[i].tableEntry[right].targetPosition - this->tableRow[i].tableEntry[left].targetPosition) / (right - left) * (left - j);
          if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
            this->tableRow[i].tableEntry[j].targetPosition = tempValue;
          }
        } else if (j > right) {
          // Extrapolate to the right
          tempValue = this->tableRow[i].tableEntry[right].targetPosition +
                      (this->tableRow[i].tableEntry[right].targetPosition - this->tableRow[i].tableEntry[left].targetPosition) / (right - left) * (j - right);
          if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
            this->tableRow[i].tableEntry[j].targetPosition = tempValue;
          }
        }
      }
    } else if (left - 1 >= 0) {
      // Only left value available, extrapolate to the right
      if (this->tableRow[i].tableEntry[left].targetPosition != INT16_MIN && this->tableRow[i].tableEntry[left - 1].targetPosition != INT16_MIN) {
        tempValue = this->tableRow[i].tableEntry[left].targetPosition +
                    (j - left) * (left > 0 ? this->tableRow[i].tableEntry[left].targetPosition - this->tableRow[i].tableEntry[left - 1].targetPosition : 1);
        if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
          this->tableRow[i].tableEntry[j].targetPosition = tempValue;
        }
      }
    } else if (right + 1 < POWERTABLE_WATT_SIZE) {
      // Only right value available, extrapolate to the left
      if (this->tableRow[i].tableEntry[right + 1].targetPosition != INT16_MIN && this->tableRow[i].tableEntry[right].targetPosition != INT16_MIN) {
        tempValue =
            this->tableRow[i].tableEntry[right].targetPosition -
            (right - j) * (right < POWERTABLE_WATT_SIZE - 1 ? this->tableRow[i].tableEntry[right + 1].targetPosition - this->tableRow[i].tableEntry[right].targetPosition : 1);
        if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
          this->tableRow[i].tableEntry[j].targetPosition = tempValue;
        }
      }
    }
  };

  // Extrapolate horizontally and vertically starting from the center
  for (int distance = 0; distance <= std::max(centerRow, centerCol); ++distance) {
    for (int i = centerRow - distance; i <= centerRow + distance; ++i) {
      for (int j = centerCol - distance; j <= centerCol + distance; ++j) {
        if (i >= 0 && i < POWERTABLE_CAD_SIZE && j >= 0 && j < POWERTABLE_WATT_SIZE && this->tableRow[i].tableEntry[j].targetPosition == INT16_MIN) {
          extrapolateCell(i, j);
        }
      }
    }
  }
  // Extrapolate each empty cell
  for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
    // Extrapolate horizontally
    for (int j = 0; j < POWERTABLE_WATT_SIZE; ++j) {
      if (this->tableRow[i].tableEntry[j].targetPosition == INT16_MIN) {
        // Find nearest left non-empty cell
        int left = j - 1;
        while (left >= 0 && this->tableRow[i].tableEntry[left].targetPosition == INT16_MIN) left--;

        // Find nearest right non-empty cell
        int right = j + 1;
        while (right < POWERTABLE_WATT_SIZE && this->tableRow[i].tableEntry[right].targetPosition == INT16_MIN) right++;
        if (this->tableRow[i].tableEntry[left].targetPosition != INT16_MIN && this->tableRow[i].tableEntry[right].targetPosition != INT16_MIN) {
          if (left >= 0 && right < POWERTABLE_WATT_SIZE) {
            // Linear extrapolation
            if (j < left) {
              // Extrapolate to the left
              tempValue = this->tableRow[i].tableEntry[left].targetPosition -
                          (this->tableRow[i].tableEntry[right].targetPosition - this->tableRow[i].tableEntry[left].targetPosition) / (right - left) * (left - j);
              if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
                this->tableRow[i].tableEntry[j].targetPosition = tempValue;
              }

            } else if (j > right) {
              // Extrapolate to the right
              tempValue = this->tableRow[i].tableEntry[right].targetPosition +
                          (this->tableRow[i].tableEntry[right].targetPosition - this->tableRow[i].tableEntry[left].targetPosition) / (right - left) * (j - right);
              if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
                this->tableRow[i].tableEntry[j].targetPosition = tempValue;
              }
            }
          } else if (left >= 1) {
            // Only left value available, extrapolate to the right
            if (this->tableRow[i].tableEntry[left].targetPosition != INT16_MIN && this->tableRow[i].tableEntry[left - 1].targetPosition != INT16_MIN) {
              tempValue = this->tableRow[i].tableEntry[left].targetPosition +
                          (j - left) * (left > 0 ? this->tableRow[i].tableEntry[left].targetPosition - this->tableRow[i].tableEntry[left - 1].targetPosition : 1);
              if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
                this->tableRow[i].tableEntry[j].targetPosition = tempValue;
              }
            }
          } else if (right + 1 < POWERTABLE_WATT_SIZE) {
            // Only right value available, extrapolate to the left
            if (this->tableRow[i].tableEntry[right].targetPosition != INT16_MIN && this->tableRow[i].tableEntry[right + 1].targetPosition != INT16_MIN) {
              tempValue = this->tableRow[i].tableEntry[right].targetPosition -
                          (right - j) *
                              (right < POWERTABLE_WATT_SIZE - 1 ? this->tableRow[i].tableEntry[right + 1].targetPosition - this->tableRow[i].tableEntry[right].targetPosition : 1);
              if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
                this->tableRow[i].tableEntry[j].targetPosition = tempValue;
              }
            }
          }
        }
      }
    }
  }

  for (int j = 0; j < POWERTABLE_WATT_SIZE; ++j) {
    // Extrapolate vertically
    for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
      if (this->tableRow[i].tableEntry[j].targetPosition == INT16_MIN) {
        // Find nearest top non-empty cell
        int top = i - 1;
        while (top >= 0 && this->tableRow[top].tableEntry[j].targetPosition == INT16_MIN) top--;

        // Find nearest bottom non-empty cell
        int bottom = i + 1;
        while (bottom < POWERTABLE_CAD_SIZE && this->tableRow[bottom].tableEntry[j].targetPosition == INT16_MIN) bottom++;

        if (top >= 0 && bottom < POWERTABLE_CAD_SIZE) {
          // Linear extrapolation
          if (i < top) {
            // Extrapolate upwards
            tempValue = this->tableRow[top].tableEntry[j].targetPosition -
                        (this->tableRow[bottom].tableEntry[j].targetPosition - this->tableRow[top].tableEntry[j].targetPosition) / (bottom - top) * (top - i);
            if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
              this->tableRow[i].tableEntry[j].targetPosition = tempValue;
            }
          } else if (i > bottom) {
            // Extrapolate downwards
            tempValue = this->tableRow[bottom].tableEntry[j].targetPosition +
                        (this->tableRow[bottom].tableEntry[j].targetPosition - this->tableRow[top].tableEntry[j].targetPosition) / (bottom - top) * (i - bottom);
            if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
              this->tableRow[i].tableEntry[j].targetPosition = tempValue;
            }
          }
        } else if (top >= 1) {
          // Only top value available, extrapolate downwards
          if (this->tableRow[top].tableEntry[j].targetPosition != INT16_MIN && this->tableRow[top - 1].tableEntry[j].targetPosition != INT16_MIN) {
            tempValue = this->tableRow[top].tableEntry[j].targetPosition +
                        (i - top) * (top > 0 ? this->tableRow[top].tableEntry[j].targetPosition - this->tableRow[top - 1].tableEntry[j].targetPosition : 1);
            if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
              this->tableRow[i].tableEntry[j].targetPosition = tempValue;
            } else {
            }
          }
        } else if (bottom + 1 < POWERTABLE_CAD_SIZE) {
          // Only bottom value available, extrapolate upwards
          if (this->tableRow[bottom].tableEntry[j].targetPosition != INT16_MIN && this->tableRow[bottom + 1].tableEntry[j].targetPosition != INT16_MIN) {
            tempValue = this->tableRow[bottom].tableEntry[j].targetPosition -
                        (bottom - i) *
                            (bottom < POWERTABLE_CAD_SIZE - 1 ? this->tableRow[bottom + 1].tableEntry[j].targetPosition - this->tableRow[bottom].tableEntry[j].targetPosition : 1);
            if (this->testNeighbors(i, j, tempValue).allNeighborsPassed) {
              this->tableRow[i].tableEntry[j].targetPosition = tempValue;
            }
          }
        }
      }
    }
  }
}

void PowerTable::extrapolateDiagonal() {
  int tempValue = INT16_MIN;

  for (int i = 0; i < POWERTABLE_CAD_SIZE; ++i) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; ++j) {
      if (this->tableRow[i].tableEntry[j].targetPosition == INT16_MIN) {
        // Find nearest top-left non-empty cell
        int topLeftRow = i - 1, topLeftCol = j - 1;
        while (topLeftRow >= 0 && topLeftCol >= 0 && this->tableRow[topLeftRow].tableEntry[topLeftCol].targetPosition == INT16_MIN) {
          topLeftRow--;
          topLeftCol--;
        }

        // Find nearest bottom-right non-empty cell
        int bottomRightRow = i + 1, bottomRightCol = j + 1;
        while (bottomRightRow < POWERTABLE_CAD_SIZE && bottomRightCol < POWERTABLE_WATT_SIZE &&
               this->tableRow[bottomRightRow].tableEntry[bottomRightCol].targetPosition == INT16_MIN) {
          bottomRightRow++;
          bottomRightCol++;
        }

        // Perform diagonal extrapolation (top-left to bottom-right)
        if (topLeftRow >= 0 && topLeftCol >= 0 && bottomRightRow < POWERTABLE_CAD_SIZE && bottomRightCol < POWERTABLE_WATT_SIZE) {
          tempValue =
              this->tableRow[topLeftRow].tableEntry[topLeftCol].targetPosition +
              ((this->tableRow[bottomRightRow].tableEntry[bottomRightCol].targetPosition - this->tableRow[topLeftRow].tableEntry[topLeftCol].targetPosition) * (j - topLeftCol)) /
                  (bottomRightCol - topLeftCol);

          if (testNeighbors(i, j, tempValue).allNeighborsPassed) {
            this->tableRow[i].tableEntry[j].targetPosition = tempValue;
          }
        }

        // If diagonal top-left to bottom-right is not enough, try top-right to bottom-left
        if (tempValue == INT16_MIN) {
          // Find nearest top-right non-empty cell
          int topRightRow = i - 1, topRightCol = j + 1;
          while (topRightRow >= 0 && topRightCol < POWERTABLE_WATT_SIZE && this->tableRow[topRightRow].tableEntry[topRightCol].targetPosition == INT16_MIN) {
            topRightRow--;
            topRightCol++;
          }

          // Find nearest bottom-left non-empty cell
          int bottomLeftRow = i + 1, bottomLeftCol = j - 1;
          while (bottomLeftRow < POWERTABLE_CAD_SIZE && bottomLeftCol >= 0 && this->tableRow[bottomLeftRow].tableEntry[bottomLeftCol].targetPosition == INT16_MIN) {
            bottomLeftRow++;
            bottomLeftCol--;
          }

          // Perform diagonal extrapolation (top-right to bottom-left)
          if (topRightRow >= 0 && topRightCol < POWERTABLE_WATT_SIZE && bottomLeftRow < POWERTABLE_CAD_SIZE && bottomLeftCol >= 0) {
            tempValue = this->tableRow[topRightRow].tableEntry[topRightCol].targetPosition +
                        ((this->tableRow[bottomLeftRow].tableEntry[bottomLeftCol].targetPosition - this->tableRow[topRightRow].tableEntry[topRightCol].targetPosition) *
                         (j - bottomLeftCol)) /
                            (topRightCol - bottomLeftCol);

            if (testNeighbors(i, j, tempValue).allNeighborsPassed) {
              this->tableRow[i].tableEntry[j].targetPosition = tempValue;
            }
          }
        }
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
          //this->tableRow[testResults.leftNeighbor.i].tableEntry[testResults.leftNeighbor.j].readings--;
          this->downVoteData(testResults.leftNeighbor.i, testResults.leftNeighbor.j, targetPosition, testResults.leftNeighbor.targetPosition);
        }
      } else {
        SS2K_LOG(POWERTABLE_LOG_TAG, "Was not in range failed left (%d)(%d)(%d), readings (%d)", testResults.leftNeighbor.i, testResults.leftNeighbor.j,
                 testResults.leftNeighbor.targetPosition, this->tableRow[testResults.leftNeighbor.i].tableEntry[testResults.leftNeighbor.j].readings);
        //this->tableRow[testResults.leftNeighbor.i].tableEntry[testResults.leftNeighbor.j].readings--;
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
          //this->tableRow[testResults.rightNeighbor.i].tableEntry[testResults.rightNeighbor.j].readings--;
          this->downVoteData(testResults.rightNeighbor.i, testResults.rightNeighbor.j, targetPosition, testResults.rightNeighbor.targetPosition);
        }
      } else {
        SS2K_LOG(POWERTABLE_LOG_TAG, "Was not in range failed right (%d)(%d)(%d), readings (%d)", testResults.rightNeighbor.i, testResults.rightNeighbor.j,
                 testResults.rightNeighbor.targetPosition, this->tableRow[testResults.rightNeighbor.i].tableEntry[testResults.rightNeighbor.j].readings);
       // this->tableRow[testResults.rightNeighbor.i].tableEntry[testResults.rightNeighbor.j].readings--;
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
          //this->tableRow[testResults.topNeighbor.i].tableEntry[testResults.topNeighbor.j].readings--;
          this->downVoteData(testResults.topNeighbor.i, testResults.topNeighbor.j, targetPosition, testResults.topNeighbor.targetPosition);
        }
      } else {
        SS2K_LOG(POWERTABLE_LOG_TAG, "Was not in range failed top (%d)(%d)(%d), readings (%d)", testResults.topNeighbor.i, testResults.topNeighbor.j,
                 testResults.topNeighbor.targetPosition, this->tableRow[testResults.topNeighbor.i].tableEntry[testResults.topNeighbor.j].readings);
        //this->tableRow[testResults.topNeighbor.i].tableEntry[testResults.topNeighbor.j].readings--;
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
          // this->tableRow[testResults.bottomNeighbor.i].tableEntry[testResults.bottomNeighbor.j].readings--;
          this->downVoteData(testResults.bottomNeighbor.i, testResults.bottomNeighbor.j, targetPosition, testResults.bottomNeighbor.targetPosition);
        }
      } else {
        SS2K_LOG(POWERTABLE_LOG_TAG, "Was not in range failed bottom (%d)(%d)(%d), readings (%d)", testResults.bottomNeighbor.i, testResults.bottomNeighbor.j,
                 testResults.bottomNeighbor.targetPosition, this->tableRow[testResults.bottomNeighbor.i].tableEntry[testResults.bottomNeighbor.j].readings);
        // this->tableRow[testResults.bottomNeighbor.i].tableEntry[testResults.bottomNeighbor.j].readings--;
        this->downVoteData(testResults.bottomNeighbor.i, testResults.bottomNeighbor.j, targetPosition, testResults.bottomNeighbor.targetPosition);
      }
    }
    return;
  }

  this->enterData(k, i, (int)targetPosition);

  // if (this->getNumEntries() > 4) {
  //   int entries    = 0;
  //   int newEntries = 1;
  //   // loop until we can't calculate any new data
  //   while (entries < newEntries) {
  //     entries = newEntries;
  //     this->fillTable();
  //     this->extrapFillTable();
  //     this->extrapolateDiagonal();
  //     newEntries = getNumEntries();
  //   }
  // }

  BLE_ss2kCustomCharacteristic::notify(0x27, k);
}

void PowerTable::enterData(int i, int j, int pos) {
  if (this->tableRow[i].tableEntry[j].readings == 0) {  // if first reading in this entry
    this->tableRow[i].tableEntry[j].targetPosition = pos;
    SS2K_LOG(POWERTABLE_LOG_TAG, "New entry recorded (%d)(%d)(%d)", i, j, this->tableRow[i].tableEntry[j].targetPosition);
  } else {  // Average and update the readings.
    this->tableRow[i].tableEntry[j].targetPosition =
        (pos + (this->tableRow[i].tableEntry[j].targetPosition * this->tableRow[i].tableEntry[j].readings)) / (this->tableRow[i].tableEntry[j].readings + 1.0);
    SS2K_LOG(POWERTABLE_LOG_TAG, "Existing entry averaged (%d)(%d)(%d), readings(%d)", i, j, this->tableRow[i].tableEntry[j].targetPosition,
             this->tableRow[i].tableEntry[j].readings);
    if (this->tableRow[i].tableEntry[j].readings > POWER_SAMPLES * 2) {
      this->tableRow[i].tableEntry[j].readings = POWER_SAMPLES * 2;  // keep from diluting recent readings too far.
    }
  }
  this->tableRow[i].tableEntry[j].readings++;

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

// function for weighted downvoting
int weightedDownVote(int targetValue, int neighborValue) {
  // calculate diff between target and neighbor
  int delta = abs(targetValue - neighborValue);
  int penalty;
  float penaltyFactor = 0.2;

  // currently consistently getting a 0 for neighbor value...
  SS2K_LOG(POWERTABLE_LOG_TAG, "WEIGHTED DOWNVOTING: Target Value: (%d), NeighborValue: (%d)", targetValue, neighborValue);

  penalty = (delta * penaltyFactor);

  SS2K_LOG(POWERTABLE_LOG_TAG, "WEIGHTED DOWNVOTING: Delta: (%d), Penalty: (%d)", delta, penalty);
  return penalty;
}

void PowerTable::downVoteData(int i, int j, float target, int neighbor) {
  // determine penalty amount before applying to failed neighbor
  int penalty = weightedDownVote(target, neighbor);

  // make sure downvotes isn't at max
  if (this->tableRow[i].tableEntry[j].readings > MAX_NEIGHBOR_WEIGHT) {
    this->tableRow[i].tableEntry[j].readings = MAX_NEIGHBOR_WEIGHT;
    penalty                                  = 0;
  }
  this->tableRow[i].tableEntry[j].readings -= penalty;
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
      averageOffset = std::accumulate(offsetDifferences.begin(), offsetDifferences.end(), 0.0) / offsetDifferences.size();
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
  file.write((uint8_t*)&version, sizeof(version));

  int size = getNumReadings();
  file.write((uint8_t*)&size, sizeof(size));

  // Write homing state
  bool isHomed = rtConfig->getHomed();
  file.write((uint8_t*)&isHomed, sizeof(isHomed));

  // Write table entries
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      file.write((uint8_t*)&this->tableRow[i].tableEntry[j].targetPosition, sizeof(this->tableRow[i].tableEntry[j].targetPosition));
      file.write((uint8_t*)&this->tableRow[i].tableEntry[j].readings, sizeof(this->tableRow[i].tableEntry[j].readings));
    }
  }

  // Close the file
  file.close();
  lastSaveTime                    = millis();
  this->_hasBeenLoadedThisSession = true;
  return true;  // return successful
}

int32_t PowerTable::lookupWatts(int cad, int32_t targetPosition) {
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