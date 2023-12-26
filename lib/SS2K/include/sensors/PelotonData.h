/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "SensorData.h"

class PelotonData : public SensorData {
 public:
  PelotonData() : SensorData("PTON") {}

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
  bool hasData  = false;
  float cadence = nanf("");
  int power     = INT_MIN;
  int resistance = INT_MIN;
  bool receivedResistance = false;
};