// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive
// 
#ifndef MAIN_H
#define MAIN_H

#include <HTTP_Server_Basic.h>
#include <smartbike_parameters.h>
#include <SBBLE_Server.h>

//Function Prototypes
bool IRAM_ATTR deBounce();
void IRAM_ATTR moveStepper(void * pvParameters);
void IRAM_ATTR shiftUp();
void IRAM_ATTR shiftDown(); 

//Main program variable that stores most everything
extern userParameters userConfig;

#endif


