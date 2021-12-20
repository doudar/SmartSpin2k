/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <Arduino.h>
#include "SmartSpin_parameters.h"

#define ERG_MODE_LOG_TAG     "ERG_Mode"
#define ERG_MODE_LOG_CSV_TAG "ERG_Mode_CSV"
#define ERG_MODE_DELAY       250

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
  float incline;
  int cad;
  int readings;

  PowerEntry() {
    this->watts     = 0;
    this->incline   = 0;
    this->cad       = 0;
    this->readings  = 0;
  }
};

