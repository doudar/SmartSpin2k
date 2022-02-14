/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "settings.h"
#include "HTTP_Server_Basic.h"
#include "SmartSpin_parameters.h"
#include "BLE_Common.h"

#define MAIN_LOG_TAG "Main"

// Function Prototypes

class SS2K {
 private:
  uint64_t lastDebounceTime;
  uint16_t debounceDelay;
  int lastShifterPosition;
  int shiftersHoldForScan;
  uint64_t scanDelayTime;
  uint64_t scanDelayStart;

 public:
  int32_t targetPosition;
  int32_t currentPosition;
  bool stepperIsRunning;
  bool externalControl;
  bool syncMode;

  bool IRAM_ATTR deBounce();
  static void IRAM_ATTR moveStepper(void* pvParameters);
  static void IRAM_ATTR maintenanceLoop(void* pvParameters);
  static void IRAM_ATTR shiftUp();
  static void IRAM_ATTR shiftDown();
  void resetIfShiftersHeld();
  void scanIfShiftersHeld();
  void startTasks();
  void stopTasks();
  void setupTMCStepperDriver();
  void updateStepperPower();
  void updateStealthchop();
  void checkDriverTemperature();
  void motorStop(bool releaseTension = false);

  SS2K() {
    targetPosition         = 0;
    currentPosition        = 0;
    stepperIsRunning       = false;
    externalControl        = false;
    syncMode               = false;
    lastDebounceTime       = 0;
    debounceDelay          = DEBOUNCE_DELAY;
    lastShifterPosition    = 0;
    shiftersHoldForScan    = SHIFTERS_HOLD_FOR_SCAN;
    scanDelayTime          = 10000;
    scanDelayStart         = 0;
  }
};

// Users Physical Working Capacity Calculation Parameters (heartrate to Power
// calculation)
extern physicalWorkingCapacity userPWC;
extern SS2K ss2k;

// Main program variable that stores most everything
extern userParameters userConfig;
extern RuntimeParameters rtConfig;