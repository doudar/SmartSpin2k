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

// Function Prototypes
bool IRAM_ATTR deBounce();
void IRAM_ATTR moveStepper(void* pvParameters);
void IRAM_ATTR shiftUp();
void IRAM_ATTR shiftDown();
void debugDirector(String, bool = true, bool = false);
void resetIfShiftersHeld();
void scanIfShiftersHeld();
void setupTMCStepperDriver();
void updateStepperPower();
void updateStealthchop();

// Main program variable that stores most everything
extern userParameters userConfig;

// Users Physical Working Capacity Calculation Parameters (heartrate to Power
// calculation)
extern physicalWorkingCapacity userPWC;

// Variable that will store debugging information that will get appended and
// then cleared once posted to HTML or a timer expires.
extern String debugToHTML;
