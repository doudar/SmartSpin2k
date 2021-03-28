/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <Arduino.h>
#include "SensorData.h"

class CyclePowerData : public SensorData {
 public:
  CyclePowerData() : SensorData("CPS") {}

  bool hasHeartRate();
  bool hasCadence();
  bool hasPower();
  bool hasSpeed();
  int getHeartRate();
  float getCadence();
  int getPower();
  float getSpeed();
  void decode(uint8_t *data, size_t length);

 private:
  int power                  = INT_MIN;
  float cadence              = NAN;
  float crankRev             = 0;
  float lastCrankRev         = 0;
  float lastCrankEventTime   = 0;
  float crankEventTime       = 0;
  uint8_t missedReadingCount = 0;
};
