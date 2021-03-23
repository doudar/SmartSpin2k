/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "SensorData.h"

class EchelonData : public SensorData {
 public:
  EchelonData() : SensorData("ECH"), cadence(NAN), resistance(INT_MIN), power(INT_MIN) {}

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
  float cadence;
  int resistance;
  int power;
};
