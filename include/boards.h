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
  Board rev1;
  Board rev2;

  Boards() {
    // Rev 1
    rev1.name               = r1_NAME;
    rev1.versionVoltage     = r1_VERSION_VOLTAGE;
    rev1.shiftUpPin         = r1_SHIFT_UP_PIN;
    rev1.shiftDownPin       = r1_SHIFT_DOWN_PIN;
    rev1.enablePin          = r1_ENABLE_PIN;
    rev1.stepPin            = r1_STEP_PIN;
    rev1.dirPin             = r1_DIR_PIN;
    rev1.stepperSerialTxPin = r1_STEPPER_SERIAL_TX;
    rev1.stepperSerialRxPin = r1_STEPPER_SERIAL_RX;
    rev1.auxSerialTxPin     = 0;
    rev1.auxSerialRxPin     = 0;
    rev1.pwrScaler          = r1_PWR_SCALER;
    // Rev 2
    rev2.name               = r2_NAME;
    rev2.versionVoltage     = r2_VERSION_VOLTAGE;
    rev2.shiftUpPin         = r2_SHIFT_UP_PIN;
    rev2.shiftDownPin       = r2_SHIFT_DOWN_PIN;
    rev2.enablePin          = r2_ENABLE_PIN;
    rev2.stepPin            = r2_STEP_PIN;
    rev2.dirPin             = r2_DIR_PIN;
    rev2.stepperSerialTxPin = r2_STEPPER_SERIAL_TX;
    rev2.stepperSerialRxPin = r2_STEPPER_SERIAL_RX;
    rev2.auxSerialTxPin     = r2_AUX_SERIAL_TX;
    rev2.auxSerialRxPin     = r2_AUX_SERIAL_RX;
    rev2.pwrScaler          = r2_PWR_SCALER;
  }
};