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
  SS2K_LOG(POWERTABLE_LOG_TAG, "Power Buffer Reset");
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
    int range      = (userConfig->getShiftStep() * 2) / TABLE_DIVISOR;

    if (currentPos >= (targetPos - range) && currentPos <= (targetPos + range)) {
      for (int i = 1; i < POWER_SAMPLES; i++) {
        if (powerBuffer.powerEntry[i].readings == 0) {
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
      SS2K_LOG(POWERTABLE_LOG_TAG, "Entry into buffer was outside the range. Clearing buffer.");
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

void PowerTable::clean() {
  SS2K_LOG(POWERTABLE_LOG_TAG, "Clean Power Table");
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      if (this->ptData.tableRow[i].tableEntry[j].readings < 1) {
        this->ptData.tableRow[i].tableEntry[j].targetPosition = INT16_MIN;
      }
    }
  }
}

void PowerTable::newEntry(PowerBuffer& powerBuffer) {
  // these are floats so that we make sure division works correctly.
  float watts          = 0;
  float cad            = 0;
  float targetPosition = 0;

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
  // this->clean();
  // To start working on the PowerTable, we need to calculate position in the table for the new entry

  ptIndex index = ptHelpers.calculateIndex(watts, cad);
  SS2K_LOG(POWERTABLE_LOG_TAG, "Averaged Entry: watts=%f, cad=%f, targetPosition=%f, (%d)(%d)", watts, cad, targetPosition, index.cadIndex, index.wattIndex);

  if ((index.cadIndex < 0) || (index.cadIndex > (POWERTABLE_CAD_SIZE - 1))) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Cad index was out of range %d", index.cadIndex);
    return;
  }

  if (index.wattIndex < 0 || index.wattIndex > (POWERTABLE_WATT_SIZE - 1)) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Watt index was out of range %d max %d", index.wattIndex, POWERTABLE_WATT_SIZE - 1);
    return;
  }

  targetPosition = this->calculatePosition(watts, cad, targetPosition, index);

  // Downvote out of position neighbors and discard entry if it doesn't match the logic of the table
  TestResults testResults = ptHelpers.testNeighbors(index, targetPosition, ptData);

  auto handleNeighborFailure = [&](const char* direction, const TestResults::Neighbor& neighbor, const TestResults::Neighbor& oppositeNeighbor, float rangeFactor) {
    if (!neighbor.passedTest) {
      SS2K_LOG(POWERTABLE_LOG_TAG, "%s neighbor position (%d) failed with watts=%f, cad=%f, targetPosition=%f, (%d)(%d)", direction, neighbor.targetPosition, watts, cad,
               targetPosition, index.cadIndex, index.wattIndex);
      this->processNeighbor(index, targetPosition, neighbor.index, neighbor.targetPosition, oppositeNeighbor.index, oppositeNeighbor.targetPosition, rangeFactor);
    }
  };

  handleNeighborFailure("Left", testResults.leftNeighbor, testResults.rightNeighbor, HORIZONTAL_NEIGHBOR_RANGE);
  handleNeighborFailure("Right", testResults.rightNeighbor, testResults.leftNeighbor, HORIZONTAL_NEIGHBOR_RANGE);
  handleNeighborFailure("Top", testResults.topNeighbor, testResults.bottomNeighbor, VERTICAL_NEIGHBOR_RANGE);
  handleNeighborFailure("Bottom", testResults.bottomNeighbor, testResults.topNeighbor, VERTICAL_NEIGHBOR_RANGE);

  if (!(testResults.bottomNeighbor.passedTest && testResults.topNeighbor.passedTest && testResults.rightNeighbor.passedTest && testResults.leftNeighbor.passedTest)) {
    return;
  }

  this->enterData(index, (int)targetPosition);
  fillTableFlag = true;  // set flag to fill table
  BLE_ss2kCustomCharacteristic::notify(0x27, index.cadIndex);
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
void PowerTable::enterData(ptIndex index, int pos) {
  if (this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings <= 0) {  // if first reading in this entry
    this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition = pos;
    SS2K_LOG(POWERTABLE_LOG_TAG, "New entry recorded (%d)(%d)(%d)", index.cadIndex, index.wattIndex,
             this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition);
  } else {  // Average and update the readings.
    this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition =
        (pos + (this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition * this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings)) /
        (this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings + 1.0);
    SS2K_LOG(POWERTABLE_LOG_TAG, "Existing entry averaged (%d)(%d)(%d), readings(%d)", index.cadIndex, index.wattIndex,
             this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition, this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings);
    if (this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings > POWER_SAMPLES * 2) {
      this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings = POWER_SAMPLES * 2;  // keep from diluting recent readings too far.
    }
  }
  this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings++;
}

void PowerTable::fillTable() {
  static int entries     = 0;
  static int newEntries  = 0;
  static int8_t step     = 0;
  static int prevEntries = 0;
  bool completed         = true;

  // Abort if the fillTableFlag is not set.
  if (!fillTableFlag) {
    entries     = 0;
    newEntries  = 0;
    prevEntries = 0;
    step        = 0;
    return;
  }

  if (ptHelpers.getNumEntries(ptData) > 4) {
    // set flag to stop execution after we can't add any more entries.
    if (esp_get_free_heap_size() < FREE_HEAP_FOR_COMPLEX_MATH) {
      // SS2K_LOG(POWERTABLE_LOG_TAG, "%d Heap too low for step %d.", esp_get_free_heap_size(), step);
      return;
    }
    entries = ptHelpers.getNumEntries(ptData);
    if (step == 0) {
      SS2K_LOG(POWERTABLE_LOG_TAG, "Fill start with %d entries", entries);
      prevEntries = entries;
      completed   = ptHelpers.splineFill(ptData, true);
    } else if (step == 1) {
      completed = ptHelpers.splineFill(ptData, false);
    } else if (step == 2) {
      completed = ptHelpers.linearFill(ptData);
    }else if (step == 3) {
      ptHelpers.fillByAverage(ptData);
      completed = true;  // this step is always completed.
    }
    newEntries = ptHelpers.getNumEntries(ptData);
    SS2K_LOG(POWERTABLE_LOG_TAG, "Fill step %d added %d new entries", step, newEntries - prevEntries);
    if (newEntries > prevEntries) {
      prevEntries = newEntries;
    } else if (step == 3) {
      SS2K_LOG(POWERTABLE_LOG_TAG, "No more entries can be added, stopping fill.");
      fillTableFlag = false;
      step          = 0;
      return;
    }
    if (completed) step = (step + 1) % 4;
  }
}

/**
 * @brief Processes a neighbor entry in the power table.
 *
 * This function checks if the target position of a neighbor entry is within a certain range
 * of the current entry's target position. If so, it attempts to enter or update data for the
 * neighbor entry based on various conditions. If all tests fail, it downvotes the neighbor entry.
 *
 * @param index The index of the current entry.
 * @param targetPosition The target position of the current entry.
 * @param neighborIndex The index of the neighbor entry.
 * @param neighbor_targetPosition The target position of the neighbor entry.
 * @param oppositeNeighborIndex The index of the opposite neighbor entry.
 * @param oppositeNeighbor_targetPosition The target position of the opposite neighbor entry.
 * @param rangeFactor A factor used to determine the range for comparison.
 */
void PowerTable::processNeighbor(ptIndex index, float targetPosition, ptIndex neighborIndex, int neighbor_targetPosition, ptIndex oppositeNeighborIndex,
                                 int oppositeNeighbor_targetPosition, float rangeFactor) {
  float avgPosition       = (targetPosition + neighbor_targetPosition) / 2;
  float positionThreshold = 500 * pow(TABLE_DIVISOR, -rangeFactor);

  auto handleNeighbor = [&](ptIndex testIndex, float testPosition, const char* logMessage) {
    if (ptHelpers.testNeighbors(testIndex, testPosition, ptData).allNeighborsPassed) {
      SS2K_LOG(POWERTABLE_LOG_TAG, logMessage, testPosition);
      this->enterData(testIndex, testPosition);
      return true;
    }
    return false;
  };

  if (std::abs(neighbor_targetPosition - targetPosition) <= positionThreshold && static_cast<int>(targetPosition) != neighbor_targetPosition) {
    bool anyPassed = false;

    anyPassed |= handleNeighbor(index, avgPosition, "Avg position is valid with current cadence and watts! Avg position: %f");
    anyPassed |= handleNeighbor(neighborIndex, targetPosition, "Current Position was valid! Current position: %f");
    anyPassed |= handleNeighbor(oppositeNeighborIndex, neighbor_targetPosition, "Neighbor position was moved from! Neighbor Position: %d");

    if (!anyPassed) {
      SS2K_LOG(POWERTABLE_LOG_TAG, "All tests failed at (%d)(%d)(%d), readings (%d)", oppositeNeighborIndex.cadIndex, oppositeNeighborIndex.wattIndex,
               oppositeNeighbor_targetPosition, this->ptData.tableRow[oppositeNeighborIndex.cadIndex].tableEntry[oppositeNeighborIndex.wattIndex].readings);
      this->downVoteData(index, targetPosition, neighbor_targetPosition);
    }
  } else {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Was not in range failed at (%d)(%d)(%d), readings (%d)", oppositeNeighborIndex.cadIndex, oppositeNeighborIndex.wattIndex,
             oppositeNeighbor_targetPosition, this->ptData.tableRow[oppositeNeighborIndex.cadIndex].tableEntry[oppositeNeighborIndex.wattIndex].readings);
    this->downVoteData(neighborIndex, targetPosition, neighbor_targetPosition);
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
float PowerTable::calculatePosition(float watts, float cad, float targetPos, ptIndex index) {
  TestResults testResults = ptHelpers.testNeighbors(index, targetPos, ptData);

  if (this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition == INT16_MIN ||
      !(testResults.bottomNeighbor.passedTest || testResults.topNeighbor.passedTest || testResults.rightNeighbor.passedTest || testResults.leftNeighbor.passedTest) ||
      this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition == targetPos) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "index.cadIndex old targetPosition: (%f)", targetPos);
    return targetPos;
  }

  int wattPosition = POWERTABLE_WATT_INCREMENT * index.wattIndex;
  int cadPosition  = MINIMUM_TABLE_CAD + (POWERTABLE_CAD_INCREMENT * index.cadIndex);

  float deltas[]     = {float(POWERTABLE_WATT_INCREMENT), float(POWERTABLE_WATT_INCREMENT), float(POWERTABLE_CAD_INCREMENT), float(POWERTABLE_CAD_INCREMENT)};
  float positions[]  = {float(wattPosition + POWERTABLE_WATT_INCREMENT), float(wattPosition - POWERTABLE_WATT_INCREMENT), float(cadPosition + POWERTABLE_CAD_INCREMENT),
                        float(cadPosition - POWERTABLE_CAD_INCREMENT)};
  bool passedTests[] = {testResults.rightNeighbor.passedTest, testResults.leftNeighbor.passedTest, testResults.bottomNeighbor.passedTest, testResults.topNeighbor.passedTest};
  bool isValid[]     = {testResults.rightNeighbor.found, testResults.leftNeighbor.found, testResults.bottomNeighbor.found, testResults.topNeighbor.found};

  float totalValue = 0.0f;
  int count        = 0;

  for (int idx = 0; idx < sizeof(passedTests) / sizeof(passedTests[0]); ++idx) {
    if (passedTests[idx] && isValid[idx]) {
      float delta = deltas[idx] / abs(targetPos - float(this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].targetPosition));
      float x     = abs((idx < 2 ? watts : cad) - positions[idx]);
      totalValue += targetPos - (x / delta);
      count++;
    }
  }

  if (count > 0) {
    targetPos = totalValue / float(count);
  }

  SS2K_LOG(POWERTABLE_LOG_TAG, "New averaged targetPosition: (%f) count: (%d)", targetPos, count);
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

/**
 * @brief Applies a downvote penalty to a specific entry in the power table.
 *
 * This function reduces the number of readings for a specific entry in the power table
 * based on a calculated penalty. If the resulting number of readings falls below zero,
 * it is set to zero. This is used to penalize entries that have failed validation.
 *
 * @param index The index of the table entry to be downvoted.
 * @param target The target position value for the entry.
 * @param neighbor The neighbor position value for the entry. (used to calculate penalty)
 */
void PowerTable::downVoteData(ptIndex index, float target, int neighbor) {
  // determine penalty amount before applying to failed neighbor
  int penalty = weightedDownVote(target, neighbor);

  if (this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings < penalty) {
    this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings = 0;
  } else {
    this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings -= penalty;
  }
  SS2K_LOG(POWERTABLE_LOG_TAG, "PT failed (%d)(%d)(%d), readings (%d)", index.cadIndex, index.wattIndex, neighbor,
           this->ptData.tableRow[index.cadIndex].tableEntry[index.wattIndex].readings);
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
    int activeReadings = ptHelpers.getTotalReadings(ptData);
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
          if ((j > 2) && (this->ptData.tableRow[i].tableEntry[j].targetPosition != INT16_MIN) && (this->ptData.tableRow[i].tableEntry[j].readings > MINIMUM_RELIABLE_POSITIONS) &&
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
          if ((this->ptData.tableRow[i].tableEntry[j].targetPosition != INT16_MIN) && (savedTargetPosition != INT16_MIN) && (savedReadings > 0) &&
              (this->ptData.tableRow[i].tableEntry[j].readings > MINIMUM_RELIABLE_POSITIONS)) {
            int offset = this->ptData.tableRow[i].tableEntry[j].targetPosition - savedTargetPosition;
            offsetDifferences.push_back(offset);
            SS2K_LOG(POWERTABLE_LOG_TAG, "offset %d", offset);
            reliablePositions++;
          }
          this->ptData.tableRow[i].tableEntry[j].targetPosition = savedTargetPosition;
          this->ptData.tableRow[i].tableEntry[j].readings       = savedReadings;
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
          this->ptData.tableRow[i].tableEntry[j].targetPosition = savedTargetPosition;
          this->ptData.tableRow[i].tableEntry[j].readings       = savedReadings;
        }
      }
      SS2K_LOG(POWERTABLE_LOG_TAG, "Both tables were created with homing, loaded values directly");
    }

    file.close();

    // Apply the offset if needed
    if (!canSkipReliabilityChecks) {
      for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
        for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
          if (this->ptData.tableRow[i].tableEntry[j].targetPosition != INT16_MIN) {
            this->ptData.tableRow[i].tableEntry[j].targetPosition += averageOffset;
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
  int validReadings = ptHelpers.getTotalReadings(ptData);

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
      if (file.write((uint8_t*)&this->ptData.tableRow[i].tableEntry[j].targetPosition, sizeof(this->ptData.tableRow[i].tableEntry[j].targetPosition)) !=
          sizeof(this->ptData.tableRow[i].tableEntry[j].targetPosition)) {
        SS2K_LOG(POWERTABLE_LOG_TAG, "Failed to write table entry position at [%d][%d]", i, j);
        file.close();
        return false;
      }

      if (file.write((uint8_t*)&this->ptData.tableRow[i].tableEntry[j].readings, sizeof(this->ptData.tableRow[i].tableEntry[j].readings)) !=
          sizeof(this->ptData.tableRow[i].tableEntry[j].readings)) {
        SS2K_LOG(POWERTABLE_LOG_TAG, "Failed to write table entry readings at [%d][%d]", i, j);
        file.close();
        return false;
      }

      // log the raw data directly to serial
      Serial.printf("%d, %d ", this->ptData.tableRow[i].tableEntry[j].targetPosition, this->ptData.tableRow[i].tableEntry[j].readings);
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

// Reset the PowerTable to 0;
bool PowerTable::reset() {
  ss2k->resetPowerTableFlag = false;
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      this->ptData.tableRow[i].tableEntry[j].targetPosition = INT16_MIN;
      this->ptData.tableRow[i].tableEntry[j].readings       = 0;
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
      if (this->ptData.tableRow[i].tableEntry[j].targetPosition == INT16_MIN) {
        continue;
      }
      int len = snprintf(nullptr, 0, "%d", this->ptData.tableRow[i].tableEntry[j].targetPosition);
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
      int targetPosition = this->ptData.tableRow[i].tableEntry[j].targetPosition;
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