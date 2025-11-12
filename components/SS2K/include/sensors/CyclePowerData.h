/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "SensorData.h"

class CyclePowerData : public SensorData {
 public:
  CyclePowerData() : SensorData("CPS") {}

  bool hasHeartRate();
  bool hasCadence();
  bool hasPower();
  bool hasSpeed();
  bool hasResistance();
  int getHeartRate();
  float getCadence();
  int getPower();
  float getSpeed();
  int getResistance();
  void decode(uint8_t *data, size_t length);
  #ifdef PLATFORMIO_ENV_NATIVE
    // For testing purposes - allows setting mock time
    static void setTestTime(unsigned long time);
    static void resetTestTime();
  #endif

 private:
  int power                       = INT_MIN;
  float cadence                   = nanf("");
  uint16_t crankRev               = 0;
  uint16_t lastCrankRev           = 0;
  uint16_t lastCrankEventTime     = 0;
  uint16_t crankEventTime         = 0;
  unsigned long lastCadUpdateTime = 0;
  unsigned long lastPwrUpdateTime = 0;
};
