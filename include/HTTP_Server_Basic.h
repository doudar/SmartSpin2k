// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

#ifndef HTTP_SERVER_BASIC_H
#define HTTP_SERVER_BASIC_H

#include <Arduino.h>

void startHttpServer();
void webClientUpdate(void * pvParameters);
void handleSpiffsFile();
void btScannerHTML();
void handleIndexFile();
void handleHrSlider();
void FirmwareUpdate();

//wifi Function
void startWifi();

#endif
