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
#include "LittleFS_Upgrade.h"
#include "boards.h"
#include "SensorCollector.h"

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
  int txCheck;
  bool rebootFlag = false;
  bool saveFlag   = false;
  bool isUpdating = false;

  bool IRAM_ATTR deBounce();
  static void IRAM_ATTR moveStepper(void *pvParameters);
  static void IRAM_ATTR maintenanceLoop(void *pvParameters);
  static void IRAM_ATTR shiftUp();
  static void IRAM_ATTR shiftDown();
  void resetIfShiftersHeld();
  void startTasks();
  void stopTasks();
  void restartWifi();
  void setupTMCStepperDriver();
  void updateStepperPower();
  void updateStealthChop();
  void updateStepperSpeed();
  void checkDriverTemperature();
  void motorStop(bool releaseTension = false);
  void FTMSModeShiftModifier();
  static void rxSerial(void);
  void txSerial();
  void pelotonConnected();

  SS2K() {
    targetPosition      = 0;
    currentPosition     = 0;
    stepperIsRunning    = false;
    externalControl     = false;
    syncMode            = false;
    lastDebounceTime    = 0;
    debounceDelay       = DEBOUNCE_DELAY;
    lastShifterPosition = 0;
    shiftersHoldForScan = SHIFTERS_HOLD_FOR_SCAN;
    scanDelayTime       = 10000;
    scanDelayStart      = 0;
    txCheck             = TX_CHECK_INTERVAL;
  }
};

class AuxSerialBuffer {
 public:
  uint8_t data[AUX_BUF_SIZE];
  size_t len;

  AuxSerialBuffer() {
    for (int i = 0; i < AUX_BUF_SIZE; i++) {
      this->data[i] = 0;
    }
    this->len = 0;
  }
};

// Users Physical Working Capacity Calculation Parameters (heart rate to Power
// calculation)
extern physicalWorkingCapacity *userPWC;
extern SS2K *ss2k;

// Main program variable that stores most everything
extern userParameters *userConfig;
extern RuntimeParameters *rtConfig;
