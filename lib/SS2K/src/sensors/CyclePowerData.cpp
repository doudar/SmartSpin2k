/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Data.h"
#include "endian.h"
#include "sensors/CyclePowerData.h"

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
  this->power = get_le16(&data[cPos]);
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
      float cadence           = (crankChange / timeElapsed) * 60;
      if (cadence > 1) {
        if (cadence > 200) {  // Human is unlikely producing 200+ cadence
          // Cadence Error: Could happen if cadence measurements were missed
          //                Leave cadence unchanged
          cadence = this->cadence;
        }
        this->cadence            = cadence;
        this->missedReadingCount = 0;
      } else {
        this->missedReadingCount++;
      }
    } else {                               // the crank rev probably didn't update
      if (this->missedReadingCount > 2) {  // Require three consecutive readings before setting 0 cadence
        this->cadence = 0;
      }
      this->missedReadingCount++;
    }
  }
}
