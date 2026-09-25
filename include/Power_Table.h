/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "settings.h"
#include "SmartSpin_parameters.h"
#include "PowerTable_Helpers.h"
#include "FtmsCalibration.h"
#define POWERTABLE_LOG_TAG "PTable"

class PowerTable {
 public:
  bool saveFlag                  = false;
  bool _hasBeenLoadedThisSession = false;

  PTData ptData;
  PTHelpers ptHelpers;
  FtmsCalibration::Map ftmsCalibration;
  uint32_t positionEpoch = 0;
  bool ftmsPositionUncertain = false;
  // Metadata can be read before homing; watts remain gated on a known origin.
  bool loadFtmsCalibration();

  // Pick up new power value and put them into the power table
  void processPowerValue(PowerBuffer& powerBuffer, int cadence, const Measurement& power, bool learningAllowed = true);

  // Sets stepper min/max value from power table
  void setStepperMinMax();

  // Catalogs a new entry into the power table.
  void newEntry(PowerBuffer& powerBuffer);

  // returns target position for given cadence and watts. Returns RETURN_ERROR if not found.
  int32_t lookup(int watts, int cad) { return this->ptHelpers.lookup(watts, cad, this->ptData); }
  bool hasErgSeekSupport() { return this->ptHelpers.hasErgSeekSupport(this->ptData); }

  // Returns a local steps-per-watt slope only when two nearby cadence rows
  // provide consistent measured segments around the requested watts.
  bool lookupSlope(int watts, int cad, double& stepsPerWatt, PowerTableSlopeStatus::Value* status = nullptr) {
    return this->ptHelpers.lookupSlope(watts, cad, stepsPerWatt, this->ptData, status);
  }
  bool lookupErgSlope(int watts, int cad, double& stepsPerWatt, PowerTableSlopeStatus::Value* status = nullptr) {
    return this->ptHelpers.lookupErgSlope(watts, cad, stepsPerWatt, this->ptData, status);
  }

  // returns watts for given cadence and target position. Returns RETURN_ERROR if not found.
  int32_t lookupWatts(int cad, int32_t targetPosition) { return this->ptHelpers.lookupWatts(cad, targetPosition, this->ptData); }

  // Automatically load/save; allowSave=false reads without repairing or replacing the file.
  bool _manageSaveState(bool canSkipReliabilityChecks = false, bool allowSave = true);

  // save powertable from littlefs
  bool _save();

  // Reset the active power table and delete the saved power table.
  bool reset();

  // Discard coordinates in RAM without touching the saved table or homing settings.
  void clearRuntime(bool allowSavedTableLoad = false);

  // Display power table in log
  void toLog();

 private:
  struct LearningAnchor {
    float watts = 0, position = 0;
    bool valid = false, published = false;
  };
  LearningAnchor learningAnchors[POWERTABLE_CAD_SIZE];
  uint32_t learningEpoch     = 0;
  unsigned long lastSaveTime = millis();
};

extern PowerTable* powerTable;
