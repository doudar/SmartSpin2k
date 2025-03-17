/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "settings.h"
#include "SmartSpin_parameters.h"

#define ERG_MODE_LOG_CSV_TAG "ERG_Mode_CSV"
#define ERG_MODE_LOG_TAG     "ERG_Mode"
#define ERG_MODE_DELAY       700

class ErgMode {
 public:
   // What used to be in the ERGTaskLoop(). This is the main control function for ERG Mode and the powertable operations.
  void runERG();
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

  // check if user is spinning, reset incline if user stops spinning
  bool _userIsSpinning(int cadence, float incline);

  // calculate incline if setpoint (from Zwift) changes
  void _setPointChangeState(int newCadence, Measurement& newWatts);

  // calculate incline if setpoint is unchanged
  void _inSetpointState(int newCadence, Measurement& newWatts);

  // update localvalues + incline, creates a log
  void _updateValues(int newCadence, Measurement& newWatts, float newIncline);
};

extern ErgMode* ergMode;
