/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "SensorData.h"

class HeartRateData : public SensorData {
 public:
  HeartRateData() : SensorData("HRS(HRM)") {}

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

 private:
  int heartrate = INT_MIN;
};
