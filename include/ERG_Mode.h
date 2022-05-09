/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "settings.h"
#include "SmartSpin_parameters.h"

#define ERG_MODE_LOG_TAG     "ERG_Mode"
#define ERG_MODE_LOG_CSV_TAG "ERG_Mode_CSV"
#define POWERTABLE_LOG_TAG   "PowTab"
#define ERG_MODE_DELAY       700
#define RETURN_ERROR         -99

extern TaskHandle_t ErgTask;
void setupERG();
void ergTaskLoop(void* pvParameters);

class PowerEntry {
 public:
  int watts;
  int32_t targetPosition;
  int cad;
  int readings;

  PowerEntry() {
    this->watts          = 0;
    this->targetPosition = 0;
    this->cad            = 0;
    this->readings       = 0;
  }
};

class PowerBuffer {
 public:
  PowerEntry powerEntry[POWER_SAMPLES];
  void set(int);
  void reset();
};

class PowerTable {
 public:
  PowerEntry powerEntry[POWERTABLE_SIZE];

  // Pick up new power value and put them into the power table
  void processPowerValue(PowerBuffer& powerBuffer, int cadence, Measurement watts);

  // Sets stepper min/max value from power table
  void setStepperMinMax();

  // Catalogs a new entry into the power table.
  void newEntry(PowerBuffer& powerBuffer);

  // returns incline for wattTarget. Null if not found.
  int32_t lookup(int watts, int cad);

  // load power table from littlefs
  bool load();

  // save powertable from littlefs
  bool save();

  // Display power table in log
  void toLog();
};

class ErgMode {
 public:
  ErgMode(PowerTable* powerTable) { this->powerTable = powerTable; }
  void computErg();
  void _writeLogHeader();
  void _writeLog(int cycles, float currentIncline, float newIncline, int currentSetPoint, int newSetPoint, int currentWatts, int newWatts, int currentCadence, int newCadence);

 private:
  bool engineStopped   = false;
  bool initialized     = false;
  int setPoint         = 0;
  int cycle            = 0;
  int offsetMultiplier = 0;
  Measurement watts    = Measurement(0);
  int cadence          = 0;
  PowerTable* powerTable;

  // check if user is spinning, reset incline if user stops spinning
  bool _userIsSpinning(int cadence, float incline);

  // calculate incline if setpoint (from Zwift) changes
  void _setPointChangeState(int newSetPoint, int newCadence, Measurement& newWatts, float currentIncline);

  // calculate incline if setpoint is unchanged
  void _inSetpointState(int newSetPoint, int newCadence, Measurement& newWatts, float currentIncline);

  // update localvalues + incline, creates a log
  void _updateValues(int newSetPoint, int newCadence, Measurement& newWatts, float currentIncline, float newIncline);
};
