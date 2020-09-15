// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive
// 
#ifndef MAIN_H
#define MAIN_H
/*#define CONFIG_BTDM_CONTROLLER_PINNED_TO_CORE = 1
#define CONFIG_BTDM_CONTROLLER_PINNED_TO_CORE_0 = y
#define CONFIG_SW_COEXIST_ENABLE = y
#define CONFIG_CLASSIC_BT_ENABLED =
#define CONFIG_SW_COEXIST_PREFERENCE_VALUE = 0 //0=wifi, 1=bt, 2=balanced
*/

//#include <SBHttp_Server.h>
#include <HTTP_Server_Basic.h>
#include <smartbike_parameters.h>
#include <SBBLE_Server.h>

/*espidf stuff
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sdkconfig.h"
*/
//Function Prototypes
bool IRAM_ATTR deBounce();
void IRAM_ATTR moveStepper(void * pvParameters);
void IRAM_ATTR shiftUp();
void IRAM_ATTR shiftDown(); 
void IRAM_ATTR changeRadioStateButton();
void switchRadioState();


// I don't like this (not sure why really, but it allows the userConfig variable 
//to be accessable in all of the .cpp files
extern userParameters userConfig;

#endif


