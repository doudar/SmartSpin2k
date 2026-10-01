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
  EchelonData() : SensorData("ECH") {}

  bool hasHeartRate() override;
  bool hasCadence() override;
  bool hasPower() override;
  bool hasSpeed() override;
  bool hasResistance() override;
  int getHeartRate() override;
  float getCadence() override;
  int getPower() override;
  float getSpeed() override;
  int getResistance() override;
  void decode(uint8_t *data, size_t length) override;

 private:
  float cadence  = nanf("");
  int resistance = INT_MIN;
  int power      = INT_MIN;
};
