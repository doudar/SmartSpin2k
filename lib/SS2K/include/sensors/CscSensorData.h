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
  float cadence = nanf("");
  float speed = nanf("");
  uint32_t lastWheelEventTime = 0;
  uint32_t lastCrankEventTime = 0;
  uint32_t lastWheelRevolutions = 0;
  uint32_t lastCrankRevolutions = 0;
  uint8_t missedReadingCount = 0;
};