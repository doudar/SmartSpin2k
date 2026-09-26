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
#include <algorithm>
#include <cmath>

void PowerBuffer::set(int i, int watts, int cadence, int32_t position) {
  powerEntry[i].readings       = 1;
  powerEntry[i].watts          = watts;
  powerEntry[i].cad            = cadence;
  powerEntry[i].targetPosition = static_cast<float>(position) / TABLE_DIVISOR;
}

void PowerBuffer::clearSamples() {
  for (auto& entry : powerEntry) entry = PowerEntry{};
}

void PowerBuffer::reset() {
  clearSamples();
  stable = false;
  // Do not forget the last report: a pause or target write cannot make it fresh.
}

int PowerBuffer::getReadings() {
  int count = 0;
  for (const auto& entry : powerEntry) count += entry.readings != 0;
  return count;
}

void PowerTable::processPowerValue(PowerBuffer& buffer, int cadence, const Measurement& watts, bool learningAllowed) {
  const auto sample  = watts.getValueSample();
  const uint32_t now = millis();  // Read after the snapshot to avoid unsigned age underflow.
  const auto resetCollection = [&](const char* reason) {
    // Report lost acquisition windows, not every disabled 700 ms poll.
    if (buffer.stable) {
      SS2K_LOG(POWERTABLE_LOG_TAG, "Collection reset: %s, samples=%d, positionSpan=%ld, cadenceSpan=%d", reason, buffer.getReadings(),
               static_cast<long>(buffer.maximumPosition - buffer.minimumPosition), buffer.maximumCadence - buffer.minimumCadence);
    }
    buffer.reset();
  };
  const bool fresh   = !buffer.seenReport || sample.timestamp != buffer.lastReport;
  const bool gap     = buffer.seenReport && static_cast<uint32_t>(sample.timestamp - buffer.lastReport) > POWER_SAMPLE_MAX_AGE_MS;
  buffer.seenReport  = true;
  buffer.lastReport  = sample.timestamp;
  if (buffer.positionEpoch != positionEpoch) {
    resetCollection("coordinate epoch changed");
    buffer.positionEpoch = positionEpoch;
  }
  if (!learningAllowed || ftmsPositionUncertain || sample.simulate || !ptHelpers.cadenceIsWithinTable(cadence) || sample.value <= 10 ||
      sample.value >= POWERTABLE_WATT_SIZE * POWERTABLE_WATT_INCREMENT || static_cast<uint32_t>(now - sample.timestamp) > POWER_SAMPLE_MAX_AGE_MS) {
    resetCollection(!learningAllowed ? "controller acquisition or table-derived power" : ftmsPositionUncertain ? "uncertain coordinates" :
                    sample.simulate ? "simulated power" : !ptHelpers.cadenceIsWithinTable(cadence) ? "cadence outside learning range" :
                    static_cast<uint32_t>(now - sample.timestamp) > POWER_SAMPLE_MAX_AGE_MS ? "stale power" : "power outside learning range");
    return;
  }
  if (gap) resetCollection("power report gap");

  const int32_t position = ss2k->getCurrentPosition();
  // Pending substantial travel also excludes delayed power before motion starts.
  if (std::abs(static_cast<int64_t>(ss2k->getTargetPosition()) - position) > POWER_SAMPLE_POSITION_SPAN) {
    resetCollection("pending travel exceeds 100 steps");
    return;
  }
  if (buffer.stable) {
    buffer.minimumPosition = std::min(buffer.minimumPosition, position);
    buffer.maximumPosition = std::max(buffer.maximumPosition, position);
    buffer.minimumCadence  = std::min(buffer.minimumCadence, cadence);
    buffer.maximumCadence  = std::max(buffer.maximumCadence, cadence);
    if (static_cast<int64_t>(buffer.maximumPosition) - buffer.minimumPosition > POWER_SAMPLE_POSITION_SPAN)
      resetCollection("position span exceeds 100 steps");
    else if (buffer.maximumCadence - buffer.minimumCadence > POWER_SAMPLE_CADENCE_SPAN)
      resetCollection("cadence span");
  }
  if (!buffer.stable) {
    buffer.stable          = true;
    buffer.stableSince     = now;
    buffer.minimumPosition = buffer.maximumPosition = position;
    buffer.minimumCadence = buffer.maximumCadence = cadence;
    return;
  }
  if (!fresh || static_cast<uint32_t>(sample.timestamp - buffer.stableSince) < POWER_SAMPLE_SETTLE_MS ||
      static_cast<uint32_t>(sample.timestamp - buffer.stableSince) > static_cast<uint32_t>(now - buffer.stableSince) ||
      (buffer.getReadings() && static_cast<uint32_t>(sample.timestamp - buffer.lastAccepted) < POWER_SAMPLE_MIN_SPACING_MS))
    return;

  int minimumWatts = sample.value, maximumWatts = sample.value;
  for (const auto& entry : buffer.powerEntry) {
    if (!entry.readings) continue;
    minimumWatts = std::min(minimumWatts, entry.watts);
    maximumWatts = std::max(maximumWatts, entry.watts);
  }
  if (maximumWatts - minimumWatts > std::max(20, (maximumWatts + minimumWatts) * 15 / 200)) {
    resetCollection("power span");
    return;
  }
  buffer.set(buffer.getReadings(), sample.value, cadence, position);
  buffer.lastAccepted = sample.timestamp;
  if (buffer.getReadings() == POWER_SAMPLES) {
    newEntry(buffer);
    toLog();
    _manageSaveState();
    buffer.clearSamples();
    // Consecutive stable windows need no extra dwell, but each has its own span.
    buffer.minimumPosition = buffer.maximumPosition = position;
    buffer.minimumCadence = buffer.maximumCadence = cadence;
  }
}

// Set min / max stepper position
void PowerTable::setStepperMinMax() {
  // if Homing was preformed, skip estimating min_max
  if (rtConfig->getHomed() && userConfig->getHMin() != INT32_MIN && userConfig->getHMax() != INT32_MIN) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Using detected travel limits during homing");
    rtConfig->setMinStep(userConfig->getHMin());
    rtConfig->setMaxStep(userConfig->getHMax());
    return;
  } else if (rtConfig->getHomed()) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "HOMING VALUES NOT FOUND");
  }

  // Failed homing always uses watt-derived limits, even with real resistance feedback.
  if (!ss2k->homingFallback && rtConfig->resistance.getValue() > 0 && !rtConfig->resistance.getSimulate()) {
    rtConfig->setMinStep(-DEFAULT_STEPPER_TRAVEL);
    rtConfig->setMaxStep(DEFAULT_STEPPER_TRAVEL);
    SS2K_LOG(POWERTABLE_LOG_TAG, "Using Resistance Travel Limits");
    return;
  }

  int minBreakWatts = userConfig->getMinWatts();
  if (minBreakWatts > 1) {
    int32_t _return = this->lookup(minBreakWatts, NORMAL_CAD);
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
    int32_t _return = this->lookup(maxBreakWatts, NORMAL_CAD);
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

  ptIndex index = ptHelpers.calculateIndex(std::lround(watts), std::lround(cad));
  SS2K_LOG(POWERTABLE_LOG_TAG, "Raw observation: watts=%f, cad=%f, targetPosition=%f, (%d)(%d)", watts, cad, targetPosition, index.cadIndex, index.wattIndex);

  if ((index.cadIndex < 0) || (index.cadIndex > (POWERTABLE_CAD_SIZE - 1))) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Cad index was out of range %d", index.cadIndex);
    return;
  }

  if (index.wattIndex < 0 || index.wattIndex > (POWERTABLE_WATT_SIZE - 1)) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Watt index was out of range %d max %d", index.wattIndex, POWERTABLE_WATT_SIZE - 1);
    return;
  }

  if (learningEpoch != positionEpoch) {
    for (auto& anchor : learningAnchors) anchor = LearningAnchor{};
    learningEpoch = positionEpoch;
  }
  const int gridCadence = MINIMUM_TABLE_CAD + index.cadIndex * POWERTABLE_CAD_INCREMENT;
  // Equal torque places the observation at the row cadence before watt binning.
  const float rowWatts = watts * gridCadence / cad;
  index                = ptHelpers.calculateIndex(std::lround(rowWatts), gridCadence);
  if (index.wattIndex < 0 || index.wattIndex >= POWERTABLE_WATT_SIZE) return;
  const int gridWatts  = index.wattIndex * POWERTABLE_WATT_INCREMENT;
  auto& anchor         = learningAnchors[index.cadIndex];
  uint16_t changedRows = 0;
  const auto insert    = [&](ptIndex cellIndex, float position) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Averaged Entry: watts=%f, cad=%f, targetPosition=%f, (%d)(%d)", static_cast<double>(cellIndex.wattIndex * POWERTABLE_WATT_INCREMENT),
             static_cast<double>(gridCadence), static_cast<double>(position), cellIndex.cadIndex, cellIndex.wattIndex);
    changedRows |= ptHelpers.enterData(ptData, cellIndex, position);
  };
  float normalizedPosition = targetPosition;
  const int32_t lower      = lookup(gridWatts - POWERTABLE_WATT_INCREMENT / 2, gridCadence);
  const int32_t upper      = lookup(gridWatts + POWERTABLE_WATT_INCREMENT / 2, gridCadence);
  if (lower != RETURN_ERROR && upper != RETURN_ERROR && upper > lower) {
    const float slope = static_cast<float>(upper - lower) / (POWERTABLE_WATT_INCREMENT * TABLE_DIVISOR);
    normalizedPosition += (gridWatts - rowWatts) * slope;
  } else if (anchor.valid && std::abs(rowWatts - anchor.watts) >= POWERTABLE_WATT_INCREMENT / 2.0f && (targetPosition - anchor.position) * (rowWatts - anchor.watts) > 0) {
    const float slope = (targetPosition - anchor.position) / (rowWatts - anchor.watts);
    normalizedPosition += (gridWatts - rowWatts) * slope;
    const ptIndex anchorIndex = ptHelpers.calculateIndex(std::lround(anchor.watts), gridCadence);
    if (!anchor.published && anchorIndex.wattIndex >= 0 && anchorIndex.wattIndex < POWERTABLE_WATT_SIZE) {
      insert(anchorIndex, anchor.position + (anchorIndex.wattIndex * POWERTABLE_WATT_INCREMENT - anchor.watts) * slope);
    }
  } else if (std::abs(rowWatts - gridWatts) > 0.5f) {
    // A lone observation cannot determine steps/watt. Retain it until another
    // stable, separated point supports a positive local slope in this row.
    if (!anchor.valid || std::abs(rowWatts - anchor.watts) >= POWERTABLE_WATT_INCREMENT / 2.0f) anchor = {rowWatts, targetPosition, true};
    SS2K_LOG(POWERTABLE_LOG_TAG, "Holding off-grid observation for a measured slope: %.1fW at %drpm", rowWatts, gridCadence);
    return;
  }
  anchor = {rowWatts, targetPosition, true, true};
  SS2K_LOG(POWERTABLE_LOG_TAG, "Grid observation: %.1fW -> %dW, position %.2f -> %.2f", rowWatts, gridWatts, targetPosition, normalizedPosition);
  insert(index, normalizedPosition);
  // Projection may move neighboring cadence rows too; publish every changed row.
  for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
    if (changedRows & (1u << row)) BLE_ss2kCustomCharacteristic::notify(0x27, row);
  }
}

bool PowerTable::loadFtmsCalibration() {
  ftmsCalibration = FtmsCalibration::Map{};
  File file = LittleFS.open(POWER_TABLE_FILENAME, FILE_READ);
  if (!file) return false;
  const size_t cells = POWERTABLE_CAD_SIZE * POWERTABLE_WATT_SIZE * (sizeof(int16_t) + sizeof(int8_t));
  const size_t header = 2 * sizeof(int) + sizeof(bool);
  int version = 0, quality = 0;
  bool homed = false;
  uint8_t bytes[FtmsCalibration::WIRE_SIZE];
  bool valid = file.size() == header + cells + sizeof(bytes) &&
               file.read(reinterpret_cast<uint8_t*>(&version), sizeof(version)) == sizeof(version) && version == TABLE_VERSION &&
               file.read(reinterpret_cast<uint8_t*>(&quality), sizeof(quality)) == sizeof(quality) && quality >= 0 &&
               file.read(reinterpret_cast<uint8_t*>(&homed), sizeof(homed)) == sizeof(homed) && homed && file.seek(header + cells) &&
               file.read(bytes, sizeof(bytes)) == sizeof(bytes) && ftmsCalibration.decode(bytes, sizeof(bytes));
  file.close();
  if (!valid || userConfig->getHMin() != 0 ||
      !ftmsCalibration.matches(FtmsCalibration::identity(userConfig->getConnectedPowerMeter(), userConfig->getStepperDir()), userConfig->getHMax())) {
    ftmsCalibration = FtmsCalibration::Map{};
    return false;
  }
  return true;
}

bool PowerTable::_manageSaveState(bool /*canSkipReliabilityChecks*/, bool allowSave) {
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
      if (allowSave) this->_save();
      return false;
    }

    // Read version and size
    int version = 0;
    file.read((uint8_t*)&version, sizeof(version));
    int savedQuality = 0;
    file.read((uint8_t*)&savedQuality, sizeof(savedQuality));
    bool savedHomed = false;
    file.read((uint8_t*)&savedHomed, sizeof(savedHomed));

    const size_t expected = 2 * sizeof(int) + sizeof(bool) + POWERTABLE_CAD_SIZE * POWERTABLE_WATT_SIZE * (sizeof(int16_t) + sizeof(int8_t));
    // The version-6 watts prefix is independent of the optional trailer. A
    // damaged/missing future trailer requires calibration, not loss of watts.
    if (version != TABLE_VERSION || !savedHomed || savedQuality < 0 || file.size() < expected) {
      SS2K_LOG(POWERTABLE_LOG_TAG, "Expected power table version %d, found version %d", TABLE_VERSION, version);
      file.close();
      if (allowSave) this->_save();
      return false;
    }

    // Is the data we are working with better than the saved file?
    int activeReadings = ptHelpers.getTotalReadings(ptData);
    if (allowSave && activeReadings > savedQuality) {
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
      if (allowSave) this->_save();
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
        this->ptData.tableRow[i].tableEntry[j]                = TableEntry{};
        this->ptData.tableRow[i].tableEntry[j].targetPosition = savedTargetPosition;
        this->ptData.tableRow[i].tableEntry[j].readings       = savedReadings;
      }
    }
    ++positionEpoch;
    SS2K_LOG(POWERTABLE_LOG_TAG, "Loaded values directly");
    file.close();

    // set the flag so it isn't loaded again this session.
    this->_hasBeenLoadedThisSession = true;
  }

  // Implement saving on a timer
  if (allowSave && (millis() - lastSaveTime) > POWER_TABLE_SAVE_INTERVAL) {
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
  if (validReadings < 1 && !ftmsCalibration.valid()) {
    SS2K_LOG(POWERTABLE_LOG_TAG, "Not enough valid readings to save power table (%d)", validReadings);
    return false;
  }

  // Replace only a complete file, so a failed metadata/table write leaves the
  // previous calibration usable. FILE_WRITE truncates the temporary file.
  SS2K_LOG(POWERTABLE_LOG_TAG, "Writing File: %s", POWER_TABLE_FILENAME);
  const String temporary = String(POWER_TABLE_FILENAME) + ".tmp";
  File file = LittleFS.open(temporary, FILE_WRITE);
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
  if (ftmsCalibration.valid()) {
    uint8_t bytes[FtmsCalibration::WIRE_SIZE];
    ftmsCalibration.encode(bytes);
    if (file.write(bytes, sizeof(bytes)) != sizeof(bytes)) {
      file.close();
      return false;
    }
  }
  file.flush();
  file.close();
  if (!LittleFS.rename(temporary, POWER_TABLE_FILENAME)) return false;
  lastSaveTime                    = millis();
  this->_hasBeenLoadedThisSession = true;
  SS2K_LOG(POWERTABLE_LOG_TAG, "Power table saved successfully with %d readings", validReadings);
  return true;  // return successful
}

// Start a new coordinate session without modifying the persisted calibration.
void PowerTable::clearRuntime(bool allowSavedTableLoad) {
  ftmsPositionUncertain = false;
  ftmsCalibration = FtmsCalibration::Map{};
  _hasBeenLoadedThisSession = !allowSavedTableLoad;
  saveFlag = false;
  lastSaveTime = millis();
  ++positionEpoch;
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      this->ptData.tableRow[i].tableEntry[j] = TableEntry{};
    }
  }
}

bool PowerTable::reset() {
  clearRuntime();
  ss2k->resetPowerTableFlag = false;
  rtConfig->setHomed(false);
  userConfig->setHMax(INT32_MIN);
  userConfig->setHMin(INT32_MIN);
  return !LittleFS.exists(POWER_TABLE_FILENAME) || LittleFS.remove(POWER_TABLE_FILENAME);
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
