/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BLE_Common.h"
#include "sensors/FlywheelData.h"

bool FlywheelData::hasHeartRate() { return false; }

bool FlywheelData::hasCadence() { return this->hasData; }

bool FlywheelData::hasPower() { return this->hasData; }

bool FlywheelData::hasSpeed() { return false; }

int FlywheelData::getHeartRate() { return INT_MIN; }

float FlywheelData::getCadence() { return this->cadence; }

int FlywheelData::getPower() { return this->power; }

float FlywheelData::getSpeed() { return NAN; }

void FlywheelData::decode(uint8_t *data, size_t length) {
  if (data[0] == 0xFF) {
    power   = bytes_to_u16(data[3], data[4]);  // uint16 big-endian at ofs 3
    cadence = data[12];
    hasData = true;
  } else {
    cadence = NAN;
    power   = INT_MIN;
    hasData = false;
  }
}
