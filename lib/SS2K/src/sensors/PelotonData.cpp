/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <cmath>

#include "sensors/PelotonData.h"
#include "Constants.h"

bool PelotonData::hasHeartRate() { return false; }

bool PelotonData::hasCadence() { return receivedCadence; }

bool PelotonData::hasPower() { return receivedPower; }

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
  // The factory caches this parser. Only the current packet can mark a value
  // fresh; otherwise cadence/resistance traffic republishes held watts to ERG.
  receivedCadence = receivedPower = receivedResistance = false;
  if (data == nullptr || length < 4 || data[0] != PELOTON_HEADER) return;
  const uint8_t payload_length = data[2];
  if (payload_length == 0 || length < static_cast<size_t>(payload_length) + 4) return;
  uint8_t checksum = 0;
  for (size_t i = 0; i < static_cast<size_t>(payload_length) + 3; ++i) checksum += data[i];
  if (checksum != data[3 + payload_length]) return;

  uint32_t value = 0;
  for (size_t i = 2 + static_cast<size_t>(payload_length); i > 2; i--) {
    if (data[i] < '0' || data[i] > '9') return;
    const uint8_t next_digit = data[i] - '0';
    if (value > 6553 || (value == 6553 && next_digit > 5)) {
      return;
    }
    value = value * 10 + next_digit;
  }
  switch (data[1]) {
    case PELOTON_POW_ID:
      // Publish current feedback without a rolling average: extra sensor lag
      // hides the motor response. ERG owns the correction-rate limit.
      if (value >= 30000) return;
      power = std::lround(value / 10.0f);
      receivedPower = true;
      break;

    case PELOTON_CAD_ID:
      if (value >= 250) return;
      cadence = value;
      receivedCadence = true;
      break;

    case PELOTON_RES_ID:
      resistance = value;
      receivedResistance = true;
      break;

    case PELOTON_RES_ID2:
      // Alternate resistance encoding is not implemented.
      break;

    default:
      break;
  }
}
