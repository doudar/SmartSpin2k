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
#include <vector>
#define POWERTABLE_LOG_TAG "PTable"

class PowerTable {
 public:
  bool saveFlag                  = false;
  bool _hasBeenLoadedThisSession = false;
  bool fillTableFlag           = false;

  PTData ptData;
  PTHelpers ptHelpers;

  // Pick up new power value and put them into the power table
  void processPowerValue(PowerBuffer& powerBuffer, int cadence, Measurement power);

  // Sets stepper min/max value from power table
  void setStepperMinMax();

  // Catalogs a new entry into the power table.
  void newEntry(PowerBuffer& powerBuffer);

  // Interpolates and extrapolates the power table.
  void fillTable();

  // returns target position for given cadence and watts. Returns RETURN_ERROR if not found.
  int32_t lookup(int watts, int cad) { return this->ptHelpers.lookup(watts, cad, this->ptData); }

  // returns watts for given cadence and target position. Returns RETURN_ERROR if not found.
  int32_t lookupWatts(int cad, int32_t targetPosition) { return this->ptHelpers.lookupWatts(cad, targetPosition, this->ptData); }

  // automatically load or save the Power Table
  bool _manageSaveState(bool canSkipReliabilityChecks = false);

  // save powertable from littlefs
  bool _save();

  // Reset the active power table and delete the saved power table.
  bool reset();

  // Display power table in log
  void toLog();

 private:
  unsigned long lastSaveTime = millis();
  
  void clean();
};

extern PowerTable* powerTable;