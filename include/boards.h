/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "settings.h"
#ifndef UNIT_TEST
#include <Arduino.h>
#else
#include <ArduinoFake.h>
#endif

class Board {
 public:
  String name;
  int versionVoltage;
  int shiftUpPin;
  int shiftDownPin;
  int enablePin;
  int stepPin;
  int dirPin;
  int stepperSerialTxPin;
  int stepperSerialRxPin;
  int auxSerialTxPin;
  int auxSerialRxPin;
  int pwrScaler;
};

class Boards {
 public:
  Board rev3;

  Boards() {
    // Rev 3
    rev3.name               = r3_NAME;
    rev3.versionVoltage     = r3_VERSION_VOLTAGE;
    rev3.shiftUpPin         = r3_SHIFT_UP_PIN;
    rev3.shiftDownPin       = r3_SHIFT_DOWN_PIN;
    rev3.enablePin          = r3_ENABLE_PIN;
    rev3.stepPin            = r3_STEP_PIN;
    rev3.dirPin             = r3_DIR_PIN;
    rev3.stepperSerialTxPin = r3_STEPPER_SERIAL_TX;
    rev3.stepperSerialRxPin = r3_STEPPER_SERIAL_RX;
    rev3.auxSerialTxPin     = r3_AUX_SERIAL_TX;
    rev3.auxSerialRxPin     = r3_AUX_SERIAL_RX;
    rev3.pwrScaler          = r3_PWR_SCALER;
  }
};
