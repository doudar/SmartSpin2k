// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive
// 
#ifndef MAIN_H
#define MAIN_H

#include "settings.h"
#include "HTTP_Server_Basic.h"
#include "SmartSpin_parameters.h"
#include "BLE_Common.h"

//Function Prototypes
bool IRAM_ATTR deBounce();
void IRAM_ATTR moveStepper(void * pvParameters);
void IRAM_ATTR shiftUp();
void IRAM_ATTR shiftDown(); 
void debugDirector(String, bool = true);
void resetIfShiftersHeld();
void scanIfShiftersHeld();
void setupTMCStepperDriver();
void updateStepperPower();
void updateStealthchop();

//Main program variable that stores most everything
extern userParameters userConfig;

//Users Physical Working Capacity Calculation Parameters (heartrate to Power calculation)
extern physicalWorkingCapacity userPWC;

//Variable that will store debugging information that will get appended and then cleared once posted to HTML or a timer expires.
extern String debugToHTML; 

#endif


