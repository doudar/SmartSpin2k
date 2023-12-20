/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "endian.h"
#include "sensors/PelotonData.h"
#include "Constants.h"

bool PelotonData::hasHeartRate() { return false; }

bool PelotonData::hasCadence() { return this->hasData; }

bool PelotonData::hasPower() { return this->hasData; }

bool PelotonData::hasSpeed() { return false; }

bool PelotonData::hasResistance() { return receivedResistance; }

int PelotonData::getHeartRate() { return INT_MIN; }

float PelotonData::getCadence() { return this->cadence; }

int PelotonData::getPower() { return this->power; }

float PelotonData::getSpeed() { return nanf(""); }

int PelotonData::getResistance() { return this->resistance; }

// example Peloton data f1 41 03 31 35 30 cb CD(51.00) PW(34) RS(36)
//                         1: 2:  3:      4:
// 1:data type 2:length 3:data 4:checksum

void PelotonData::decode(uint8_t *data, size_t length) {
  float value                  = 0.0;
  const uint8_t payload_length = data[2];
  for (uint8_t i = 2 + payload_length; i > 2; i--) {
    // -30 = Convert from ASCII to numeric
    uint8_t next_digit = data[i] - 0x30;
    // Check for overflow
    if (value > 6553 || (value == 6553 && next_digit > 5)) {
      return;
    }
    value = value * 10 + next_digit;
  }
  hasData = true;
  switch (data[1]) {
    case PELOTON_POW_ID:
      if (value >= 0) {
        power = value / 10;
      } else {
        power = 0;
      }

      break;

    case PELOTON_CAD_ID:
      cadence = value;
      break;

    case PELOTON_RES_ID:
      resistance = value;
      receivedResistance = true;
      break;

    case PELOTON_RES_ID2:
      //resistance = -99; //send an error because the table lookup hasn't been implemented yet. 
      break;

    default:
      break;
      //   cadence = nanf("");
      //   power   = INT_MIN;
      //   hasData = false;
  }
}