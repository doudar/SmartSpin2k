/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "sensors/EchelonData.h"

bool EchelonData::hasHeartRate() { return false; }

bool EchelonData::hasCadence() { return !std::isnan(this->cadence); }

bool EchelonData::hasPower() { return this->cadence >= 0 && this->resistance >= 0; }

bool EchelonData::hasSpeed() { return false; }

bool EchelonData::hasResistance() { return true; }

int EchelonData::getHeartRate() { return INT_MIN; }

float EchelonData::getCadence() { return this->cadence; }

int EchelonData::getPower() { return this->power; }

int EchelonData::getResistance() { return this->resistance; }

float EchelonData::getSpeed() { return nanf(""); }

void EchelonData::decode(uint8_t *data, size_t length) {
  switch (data[1]) {
    // Cadence notification
    case 0xD1:
      this->cadence = static_cast<int>((data[9] << 8) + data[10]);
      break;
    // Resistance notification
    case 0xD2:
      this->resistance = static_cast<int>(data[3]);
      break;
  }
  if (std::isnan(this->cadence) || this->resistance < 0) {
    return;
  }
  if (this->cadence == 0 || this->resistance == 0) {
    power = 0;
  } else {
    power = pow(1.090112, resistance) * pow(1.015343, cadence) * 7.228958;
  }
}
