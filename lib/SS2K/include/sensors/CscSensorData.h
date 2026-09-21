/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "SensorData.h"

class CscSensorData : public SensorData {
 public:
  CscSensorData() : SensorData("CSC") {}

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
  float cadence                 = nanf("");
  float speed                   = nanf("");
  uint16_t lastWheelEventTime   = 0;
  uint16_t lastCrankEventTime   = 0;
  uint32_t lastWheelRevolutions = 0;
  uint32_t lastCrankRevolutions = 0;
  unsigned long lastUpdateTime  = 0;
};
