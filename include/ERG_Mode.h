/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <Arduino.h>
#include "SmartSpin_parameters.h"
#include "settings.h"

#define ERG_MODE_LOG_TAG     "ERG_Mode"
#define ERG_MODE_LOG_CSV_TAG "ERG_Mode_CSV"
#define POWERTABLE_LOG_TAG   "PowTab"
#define ERG_MODE_DELAY       700

extern TaskHandle_t ErgTask;
void setupERG();
void ergTaskLoop(void *pvParameters);

class ErgMode {
 public:
  void computErg(int newSetpoint);
  void _writeLogHeader();
  void _writeLog(int cycles, float currentIncline, float newIncline, int currentSetPoint, int newSetPoint, int currentWatts, int newWatts, int currentCadence, int newCadence);

 private:
  bool engineStopped  = false;
  bool initialized    = false;
  int setPoint        = 0;
  int cycles          = 0;
  int offsetMuliplier = 0;
  Measurement watts   = Measurement(0);
  int cadence         = 0;

  bool _userIsSpinning(int cadence, float incline);
};

class PowerEntry {
 public:
  int watts;
  int32_t targetPosition;
  int cad;
  int readings;

  PowerEntry() {
    this->watts    = 0;
    this->targetPosition = 0;
    this->cad      = 0;
    this->readings = 0;
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
  // Catalogs a new entry into the power table.
  void newEntry(PowerBuffer powerBuffer);
  // returns incline for wattTarget. Null if not found.
  int32_t lookup(int watts, int cad);
  // load power table from spiffs
  bool load();
  // save powertable from spiffs
  bool save();
  // Display power table in log
  void toLog();
};


