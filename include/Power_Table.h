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

  PTData ptData;
  PTHelpers ptHelpers;

  // Pick up new power value and put them into the power table
  void processPowerValue(PowerBuffer& powerBuffer, int cadence, Measurement power);

  // Sets stepper min/max value from power table
  void setStepperMinMax();

  // Catalogs a new entry into the power table.
  void newEntry(PowerBuffer& powerBuffer);

  // enters data into power table
  void enterData(int i, int j, int pos);

  void downVoteData(int i, int j, float target, int neighbor);

  // returns target position for given cadence and watts. Returns RETURN_ERROR if not found.
  int32_t lookup(int watts, int cad) { return this->ptHelpers.lookup(watts, cad, this->ptData); }

  // returns
  int32_t splineLookup(int watts, int cad);

  // returns watts for given cadence and target position. Returns RETURN_ERROR if not found.
  int32_t lookupWatts(int cad, int32_t targetPosition) { return this->ptHelpers.lookupWatts(cad, targetPosition, this->ptData); }

  // automatically load or save the Power Table
  bool _manageSaveState(bool canSkipReliabilityChecks = false);

  // save powertable from littlefs
  bool _save();

  // Reset the active power table and delete the saved power table.
  bool reset();

  // return number of readings in the table.
  int getNumReadings();

  // Display power table in log
  void toLog();

  void fillEmptyTable(int outerValue, const std::vector<int>& emptyIndices, const float* x, const float* y, size_t n, bool horizontal, bool useNaturalSpline);

  float calculatePosition(float watts, float cad, float targetPos, int i, int k);

  void processNeighbor(int k, int i, float targetPosition, int neighbor_i, int neighbor_j, int neighbor_targetPosition, int oppositeNeighbor_i, int oppositeNeighbor_j,
                       int oppositeNeighbor_targetPosition, float rangeFactor);

 private:
  unsigned long lastSaveTime = millis();
  
  void extrapFillTable();
  int getNumEntries();
  // remove entries with < 1 readings
  void clean();
};

extern PowerTable* powerTable;