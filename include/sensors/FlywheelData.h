/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "SensorData.h"

class FlywheelData : public SensorData {
 public:
  FlywheelData() : SensorData("FLYW") {}

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
  bool hasData  = false;
  float cadence = NAN;
  int power     = INT_MIN;
};
