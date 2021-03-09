// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

#pragma once

#include <Arduino.h>

void startHttpServer();
void webClientUpdate(void *pvParameters);
void handleSpiffsFile();
void handleIndexFile();
void settingsProcessor();
void handleHrSlider();
void FirmwareUpdate();

#ifdef USE_TELEGRAM
void sendTelegram(String textToSend);
void telegramUpdate(void *pvParameters);
#endif

//wifi Function
void startWifi();
