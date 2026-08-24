/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <chrono>
#include "Data.h"
#include "endian.h"
#include "sensors/CyclePowerData.h"

#ifdef PLATFORMIO_ENV_NATIVE
// For testing in native environment
static unsigned long mockTimeMillis = 0;
static bool useMockTime = false;

unsigned long getTimeMillis() {
  if (useMockTime) {
    return mockTimeMillis;
  }
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()
  ).count();
}

void CyclePowerData::setTestTime(unsigned long time) {
  mockTimeMillis = time;
  useMockTime = true;
}

// Reset mock time to use real time
void CyclePowerData::resetTestTime() {
  useMockTime = false;
}
#else
// Replacement for Arduino's millis() using standard C++
static unsigned long getTimeMillis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()
  ).count();
}
#endif

bool CyclePowerData::hasHeartRate() { return false; }

bool CyclePowerData::hasCadence() { return !std::isnan(this->cadence); }

bool CyclePowerData::hasPower() { return this->power != INT_MIN; }

bool CyclePowerData::hasSpeed() { return false; }

bool CyclePowerData::hasResistance() { return false; }

int CyclePowerData::getHeartRate() { return INT_MIN; }

float CyclePowerData::getCadence() { return this->cadence; }

int CyclePowerData::getPower() { return this->power; }

float CyclePowerData::getSpeed() { return nanf(""); }

int CyclePowerData::getResistance() { return INT_MIN; }

void CyclePowerData::decode(uint8_t *data, size_t length) {
  uint8_t flags = data[0];
  int cPos      = 2;  // lowest position power could ever be
  // Instantaneous power is always present. Do that first.
  // first calculate which fields are present. Power is always 2 & 3, cadence
  // can move depending on the flags.
  int newPower = get_le16(&data[cPos]);
  // check newPower is greater than zero. If not, wait 2.5 seconds before setting zero
  if (newPower > 0) {
    this->power = newPower;
    this->lastPwrUpdateTime = getTimeMillis();
  } else {
    unsigned long currentTime = getTimeMillis();
    if (currentTime - lastPwrUpdateTime > 2500) {  // Require five seconds before setting 0 power
      this->power = 0;
    }
  }
  cPos += 2;

  if (bitRead(flags, 0)) {
    // pedal balance field present
    cPos++;
  }
  // if (bitRead(flags, 1)) {
  // pedal power balance reference
  // no field associated with this.
  // }
  if (bitRead(flags, 2)) {
    // accumulated torque field present
    cPos += 2;
  }
  // if (bitRead(flags, 3)) {
  // accumulated torque field source
  // no field associated with this.
  // }
  if (bitRead(flags, 4)) {
    // Wheel Revolution field PAIR Data present. 32-bits for wheel revs, 16
    // bits for wheel event time. Why is that so hard to find in the specs?
    cPos += 6;
  }
  if (bitRead(flags, 5)) {
    // Crank Revolution data present, lets process it.
    if (!this->hasCadence()) {
      // Handle the special case that this is first cadence reading
      // Since we have no lastCrankRev/EventTime we can't do a cadence calc
      // until the next reading
      this->crankRev       = get_le16(&data[cPos]);
      this->crankEventTime = get_le16(&data[cPos + 2]);
      this->cadence        = 0;
      return;
    }

    this->lastCrankRev       = this->crankRev;
    this->crankRev           = get_le16(&data[cPos]);
    this->lastCrankEventTime = this->crankEventTime;
    this->crankEventTime     = get_le16(&data[cPos + 2]);
    if (this->crankRev != this->lastCrankRev && this->crankEventTime != this->lastCrankEventTime) {
      // This casting behavior makes sure the roll over works correctly. Unit tests confirm
      const float crankChange = (uint16_t)((this->crankRev - this->lastCrankRev) * 1024);
      const float timeElapsed = (uint16_t)(this->crankEventTime - this->lastCrankEventTime);
      float cadence           = (crankChange / timeElapsed) * 60.0f;  // cadence in RPM
      if (cadence > 1) {
        if (cadence > 200) {  // Human is unlikely producing 200+ cadence
          // Cadence Error: Could happen if cadence measurements were missed
          //                Leave cadence unchanged
          cadence = this->cadence;
        }
        this->cadence        = cadence;
        this->lastCadUpdateTime = getTimeMillis();
      }
    } else {
      unsigned long currentTime = getTimeMillis();
      if (currentTime - lastCadUpdateTime > 2500) {  // Wait 2.5 seconds before setting 0 cadence
        this->cadence = 0;
      }
    }
  }
}
