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
  // Use the same cadence binning rule for collection and insertion. This
  // admits the complete edge bins (58-62 RPM and 103-107 RPM) while rejecting
  // cadences that calculateIndex() cannot place in the table.
  if (ptHelpers.cadenceIsWithinTable(cadence) && (watts.getValue() > 10) &&  // adding constraints
      (watts.getValue() < (POWERTABLE_WATT_SIZE * POWERTABLE_WATT_INCREMENT))) {
    if (powerBuffer.powerEntry[0].readings == 0) {  // we need to make sure stepper position is not negative so it only takes positive resistance values
      // Take Initial reading
      powerBuffer.set(0);
      // Check if the current stepper position is within a 5% range of the previous stepper position and that the current position is not negative
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
        long int timer = millis();
        this->newEntry(powerBuffer);
        SS2K_LOG(POWERTABLE_LOG_TAG, "New Entry Processed in %ld ms", millis() - timer);
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
  if (rtConfig->getHomed() && userConfig->getHMin() != INT32_MIN && userConfig->getHMax() != INT32_MIN) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Using detected travel limits during homing");
    rtConfig->setMinStep(userConfig->getHMin());
    rtConfig->setMaxStep(userConfig->getHMax());
    return;
  } else if (rtConfig->getHomed()) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "HOMING VALUES NOT FOUND");
  }

  // if the FTMS device reports resistance feedback, skip estimating min_max
  if (rtConfig->resistance.getValue() > 0 && !rtConfig->resistance.getSimulate()) {
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

  ptHelpers.enterData(ptData, index, (int)targetPosition);
  BLE_ss2kCustomCharacteristic::notify(0x27, index.cadIndex);
}

bool PowerTable::_manageSaveState(bool canSkipReliabilityChecks) {
  // Homing is now a prerequisite for loading and saving the powertable.
  if (!rtConfig->getHomed()) {
    return false;
  }
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
    SS2K_LOG(POWERTABLE_LOG_TAG, "Loaded values directly");
    //}

    file.close();

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
  if (!rtConfig->getHomed()) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Power Table not saved because homing was not performed.");
    return false;  // do not save if homing was not performed.
  }
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
  userConfig->setHMax(INT32_MIN);
  userConfig->setHMin(INT32_MIN);
  rtConfig->setHomed(false);
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
