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

struct Mode {
  static const int MAINTAIN   = 0;
  static const int DECREASING = 1;
  static const int INCREASING = 2;
};

class ErgMode {
 public:
  // What used to be in the ERGTaskLoop(). This is the main control function for ERG Mode and the powertable operations.
  void runERG();
  void computeErg();
  void _writeLog(float currentIncline, float newIncline, int currentSetPoint, int newSetPoint, int currentWatts, int newWatts, int currentCadence, int newCadence);

 private:
  int mode = Mode::MAINTAIN;
  Measurement prevWatts;
  Measurement prevCadence;

  // calculate incline if setpoint (from Zwift) changes
  int32_t _setPointChangeState();

  // calculate incline if setpoint is unchanged
  int32_t _inSetpointState();

  // update localvalues + incline, creates a log
  void _updateValues(float newIncline);
};

extern ErgMode* ergMode;
