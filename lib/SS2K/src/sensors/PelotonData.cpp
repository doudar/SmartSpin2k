/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <cmath>
#include <chrono>

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
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  decode(data, length, static_cast<uint32_t>(now));
}

void PelotonData::decode(uint8_t *data, size_t length, uint32_t now) {
  // Fresh flags describe one completed window, never held values from it.
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
  Accumulator* destination = nullptr;
  switch (data[1]) {
    case PELOTON_POW_ID:
      if (value >= 30000) return;
      destination = &pendingPower;
      break;

    case PELOTON_CAD_ID:
      if (value >= 250) return;
      destination = &pendingCadence;
      break;

    case PELOTON_RES_ID:
      if (value > 100) return;
      destination = &pendingResistance;
      break;

    case PELOTON_RES_ID2:
      // Alternate resistance encoding is not implemented.
      return;

    default:
      return;
  }

  const uint32_t elapsed = now - windowStart;
  if (!collecting || elapsed >= averageWindowMs) {
    // The next valid packet closes [start, start + 1 s), then belongs to the
    // next window. Discard an old window after a gap instead of reviving it.
    if (collecting && elapsed < 2 * averageWindowMs) {
      receivedPower = pendingPower.count != 0;
      receivedCadence = pendingCadence.count != 0;
      receivedResistance = pendingResistance.count != 0;
      if (receivedPower) power = std::lround(pendingPower.average() / 10.0f);
      if (receivedCadence) cadence = pendingCadence.average();
      if (receivedResistance) resistance = std::lround(pendingResistance.average());
    }
    pendingPower = pendingCadence = pendingResistance = {};
    windowStart = now;
    collecting = true;
  }
  // Keep wire precision until publication; each field has its own denominator.
  destination->add(value);
}
