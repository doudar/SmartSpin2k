/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "HTTP_Server_Basic.h"
#include "SmartSpin_parameters.h"
#include "BLE_Common.h"
// #include "LittleFS_Upgrade.h"
#include "boards.h"
#include "SensorCollector.h"
#include "SS2KLog.h"

#define MAIN_LOG_TAG "Main"

// Function Prototypes

enum ButtonState {
  RELEASED,
  PRESSED
};

class SS2K {
 private:
  unsigned long int lastDebounceTime = 0;
  ButtonState upButtonState;
  ButtonState downButtonState;
  int lastShifterPosition;
  int shiftersHoldForScan;
  unsigned long int scanDelayTime;
  unsigned long int scanDelayStart;
  int32_t targetPosition;
  int32_t currentPosition;
  bool ledEnabled;
  void handleShiftButtons();
  static void finishSetup();

 public:
  bool stepperIsRunning;
  bool externalControl;
  bool syncMode;
  int txCheck;
  bool pelotonIsConnected;
  bool rebootFlag          = false;
  bool saveFlag            = false;
  bool resetDefaultsFlag   = false;
  bool resetPowerTableFlag = false;
  bool isUpdating          = false;

  static void ARDUINO_ISR_ATTR maintenanceLoop(void *pvParameters);
  static void ARDUINO_ISR_ATTR handleUpShift();
  static void ARDUINO_ISR_ATTR handleDownShift();
  static void moveStepper();
  bool _findEndStop(bool moveForward);
  void _findFTMSHome(bool bothDirections = false);
  void _resistanceMove();

  // the position the stepper motor will move to
  int32_t getTargetPosition() { return targetPosition; }
  void setTargetPosition(int32_t tp) { targetPosition = tp; }

  // the position the stepper motor is currently at
  int32_t getCurrentPosition() { return currentPosition; }
  void setCurrentPosition(int32_t cp) { currentPosition = cp; }

  int getLastShifterPosition() { return lastShifterPosition; }
  void setLastShifterPosition(int sp) { lastShifterPosition = sp; }

  void resetIfShiftersHeld();
  void startTasks();
  void stopTasks();
  void restartWifi();
  void setupTMCStepperDriver(bool reset = false);
  void updateStepperPower(int pwr = 0);
  void updateStealthChop(bool coolStepEnabled = true);
  void updateStepperSpeed(int speed = 0);
  void FTMSModeShiftModifier();
  static void rxSerial(void);
  void txSerial();
  bool pelotonConnected();
  void goHome(bool bothDirections = false);
  void setLEDEnabled(bool enabled);
  void updateLED();

  SS2K() {
    upButtonState        = RELEASED;
    downButtonState      = RELEASED;
    targetPosition      = 0;
    currentPosition     = 0;
    ledEnabled          = false;
    stepperIsRunning    = false;
    externalControl     = false;
    syncMode            = false;
    lastShifterPosition = 0;
    shiftersHoldForScan = SHIFTERS_HOLD_FOR_SCAN;
    scanDelayTime       = 10000;
    scanDelayStart      = 0;
    pelotonIsConnected  = false;
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

extern SS2K *ss2k;
extern Board currentBoard;

// Main program variable that stores most everything
extern userParameters *userConfig;
extern RuntimeParameters *rtConfig;
