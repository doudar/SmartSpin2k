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
#if defined(SMARTSPIN2K_S3)
  Board rev3;
#else
  Board rev1;
  Board rev2;
#endif

  Boards() {
#if defined(SMARTSPIN2K_S3)
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
#else
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
#endif
  }
};
