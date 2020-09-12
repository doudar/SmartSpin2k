
// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Physical hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

#ifndef SBHTTP_SERVER_H
#define SBHTTP_SERVER_H
//Webserver structures and classes
#include <Arduino.h>
#include <smartbike_parameters.h>
//function Prototypes

/*espidf stuff
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sdkconfig.h"
*/
void startHttpServer();
String processor(const String& var);

#endif
