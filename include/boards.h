/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <Arduino.h>

class Board {
 public:
  String name;
  int versionVoltage;
  int versionTolerance;
  int revisionPin;
  int shiftUpPin;
  int shiftDownPin;
  int enablePin;
  int stepPin;
  int dirPin;
  int stepperSerialTxPin;
  int stepperSerialRxPin;
  int auxSerialTxPin;
  int auxSerialRxPin;
  int ledPin;
  int pwrScaler;
  float rSense;
  bool homingSupported;
  float homingSensitivityScaler;
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
    rev3.name               = "Revision Three (ESP32-S3)";
    rev3.versionVoltage     = 1241;
    rev3.versionTolerance   = 300;
    rev3.revisionPin        = 4;
    rev3.shiftUpPin         = 14;
    rev3.shiftDownPin       = 13;
    rev3.enablePin          = 48;
    rev3.stepPin            = 21;
    rev3.dirPin             = 47;
    rev3.stepperSerialTxPin = 11;
    rev3.stepperSerialRxPin = 12;
    rev3.auxSerialTxPin     = 17;
    rev3.auxSerialRxPin     = 18;
    rev3.ledPin             = 2;
    rev3.pwrScaler          = 12;
    rev3.rSense             = 0.04f;
    rev3.homingSupported    = true;
    rev3.homingSensitivityScaler = 1.6f;
#else
    // Rev 1
    rev1.name               = "Revision One";
    rev1.versionVoltage     = 0;
    rev1.versionTolerance   = 0;
    rev1.revisionPin        = 34;
    rev1.shiftUpPin         = 19;
    rev1.shiftDownPin       = 18;
    rev1.enablePin          = 13;
    rev1.stepPin            = 25;
    rev1.dirPin             = 33;
    rev1.stepperSerialTxPin = 12;
    rev1.stepperSerialRxPin = 14;
    rev1.auxSerialTxPin     = 0;
    rev1.auxSerialRxPin     = 0;
    rev1.ledPin             = 2;
    rev1.pwrScaler          = 31;
    rev1.rSense             = 0.08f;
    rev1.homingSupported    = false;
    rev1.homingSensitivityScaler = 1.0f;
    // Rev 2
    rev2.name               = "Revision Two";
    rev2.versionVoltage     = 4095;
    rev2.versionTolerance   = 0;
    rev2.revisionPin        = 34;
    rev2.shiftUpPin         = 26;
    rev2.shiftDownPin       = 32;
    rev2.enablePin          = 27;
    rev2.stepPin            = 25;
    rev2.dirPin             = 33;
    rev2.stepperSerialTxPin = 19;
    rev2.stepperSerialRxPin = 18;
    rev2.auxSerialTxPin     = 21;
    rev2.auxSerialRxPin     = 22;
    rev2.ledPin             = 2;
    rev2.pwrScaler          = 12;
    rev2.rSense             = 0.08f;
    rev2.homingSupported    = true;
    rev2.homingSensitivityScaler = 1.0f;
#endif
  }
};
