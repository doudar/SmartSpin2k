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
#define TORQUETABLE_LOG_TAG  "TTable"
#define ERG_MODE_DELAY       700
#define RETURN_ERROR         -99
#define TORQUE_CONSTANT      9.5488
#define CAD_MULTIPLIER       2.7

extern TaskHandle_t ErgTask;
void setupERG();
void ergTaskLoop(void* pvParameters);

int _torqueToWatts(float torque, float cad);
float _wattsToTorque(int watts, float cad);

class TorqueEntry {
 public:
  float torque;
  int32_t targetPosition;
  int readings;

  TorqueEntry() {
    this->torque         = 0;
    this->targetPosition = 0;
    this->readings       = 0;
  }
};

class TorqueBuffer {
 public:
  TorqueEntry torqueEntry[TORQUE_SAMPLES];
  void set(int);
  void reset();
};

class TorqueTable {
 public:
  TorqueEntry torqueEntry[TORQUETABLE_SIZE];

  // Pick up new torque value and put them into the torque table
  void processTorqueValue(TorqueBuffer& torqueBuffer, int cadence, Measurement torque);

  // Sets stepper min/max value from torque table
  void setStepperMinMax();

  // Catalogs a new entry into the torque table.
  void newEntry(TorqueBuffer& torqueBuffer);

  // returns incline for wattTarget. Null if not found.
  int32_t lookup(int watts, int cad);

  // automatically load or save the Torque Table
  bool _manageSaveState();

  // save torquetable from littlefs
  bool _save();

  // Display torque table in log
  void toLog();

 private:
  unsigned long lastSaveTime = millis();
  bool _hasBeenLoadedThisSession = false;
};

class ErgMode {
 public:
  ErgMode(TorqueTable* torqueTable) { this->torqueTable = torqueTable; }
  void computeErg();
  void computeResistance();
  void _writeLogHeader();
  void _writeLog(float currentIncline, float newIncline, int currentSetPoint, int newSetPoint, int currentWatts, int newWatts, int currentCadence, int newCadence);

 private:
  bool engineStopped   = false;
  bool initialized     = false;
  int setPoint         = 0;
  int offsetMultiplier = 0;
  int resistance       = 0;
  int cadence          = 0;

  Measurement watts;
  TorqueTable* torqueTable;

  // check if user is spinning, reset incline if user stops spinning
  bool _userIsSpinning(int cadence, float incline);

  // calculate incline if setpoint (from Zwift) changes
  void _setPointChangeState(int newCadence, Measurement& newWatts);

  // calculate incline if setpoint is unchanged
  void _inSetpointState(int newCadence, Measurement& newWatts);

  // update localvalues + incline, creates a log
  void _updateValues(int newCadence, Measurement& newWatts, float newIncline);
};
