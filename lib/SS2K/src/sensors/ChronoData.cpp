/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ByteUtils.h"
#include "sensors/ChronoData.h"

bool ChronoData::hasHeartRate() { return false; }

bool ChronoData::hasCadence() { return this->hasData; }

bool ChronoData::hasPower() { return this->hasData; }

bool ChronoData::hasSpeed() { return false; }

bool ChronoData::hasResistance() { return false; }

int ChronoData::getHeartRate() { return INT_MIN; }

float ChronoData::getCadence() { return this->cadence; }

int ChronoData::getPower() { return this->power; }

float ChronoData::getSpeed() { return nanf(""); }

int ChronoData::getResistance() { return INT_MIN; }

void ChronoData::decode(uint8_t *data, size_t length) {
  // Validate the fixed 19-byte telemetry packet with its magic header 0x53 0x59 0x16.
  if (length < 19 || data[0] != 0x53 || data[1] != 0x59 || data[2] != 0x16) {
    hasData = false;
    cadence = nanf("");
    power   = INT_MIN;
    return;
  }
  cadence = get_le16(&data[8]) / 10.0f;  // bytes 8-9, uint16 little-endian, ÷10 RPM
  power   = get_le16(&data[17]);         // bytes 17-18, uint16 little-endian, watts
  hasData = true;
}
