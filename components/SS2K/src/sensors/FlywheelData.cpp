/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "endian.h"
#include "sensors/FlywheelData.h"

bool FlywheelData::hasHeartRate() { return false; }

bool FlywheelData::hasCadence() { return this->hasData; }

bool FlywheelData::hasPower() { return this->hasData; }

bool FlywheelData::hasSpeed() { return false; }

bool FlywheelData::hasResistance() { return true; }

int FlywheelData::getHeartRate() { return INT_MIN; }

float FlywheelData::getCadence() { return this->cadence; }

int FlywheelData::getPower() { return this->power; }

float FlywheelData::getSpeed() { return nanf(""); }

int FlywheelData::getResistance() { return INT_MIN; }

void FlywheelData::decode(uint8_t *data, size_t length) {
  if (data[0] == 0xFF) {
    power      = get_be16(&data[3]);  // uint16 big-endian at ofs 3
    cadence    = data[12];
    resistance = data[15];
    hasData    = true;
  } else {
    cadence    = nanf("");
    power      = INT_MIN;
    resistance = INT_MIN;
    hasData    = false;
  }
}
