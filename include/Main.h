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
bool IRAM_ATTR deBounce();
void IRAM_ATTR moveStepper(void* pvParameters);
void IRAM_ATTR shifterCheck(void* pvParameters);
void IRAM_ATTR shiftUp();
void IRAM_ATTR shiftDown();
void resetIfShiftersHeld();
void scanIfShiftersHeld();
void setupTMCStepperDriver();
void updateStepperPower();
void updateStealthchop();
void checkDriverTemperature();
void motorStop(bool releaseTension = false);
void startTasks();
void stopTasks();

// Main program variable that stores most everything
extern userParameters userConfig;
extern RuntimeParameters rtConfig;

class SS2K {
 public:
  int32_t targetPosition;
  int32_t currentPosition;
  bool externalControl;
  bool syncMode;

  SS2K() {
    targetPosition  = 0;
    externalControl = false;
    syncMode        = false;
  }
};

// Users Physical Working Capacity Calculation Parameters (heartrate to Power
// calculation)
extern physicalWorkingCapacity userPWC;
extern SS2K ss2k;
