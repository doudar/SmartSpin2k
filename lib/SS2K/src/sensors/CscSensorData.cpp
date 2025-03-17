/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Data.h"
#include "endian.h"
#include "sensors/CscSensorData.h"

bool CscSensorData::hasHeartRate() { return false; }

bool CscSensorData::hasCadence() { return !std::isnan(this->cadence); }

bool CscSensorData::hasPower() { return false; }

bool CscSensorData::hasSpeed() { return !std::isnan(this->speed); }

bool CscSensorData::hasResistance() { return false; }

int CscSensorData::getHeartRate() { return INT_MIN; }

float CscSensorData::getCadence() { return this->cadence; }

int CscSensorData::getPower() { return INT_MIN; }

float CscSensorData::getSpeed() { return this->speed; }

int CscSensorData::getResistance() { return INT_MIN; }

void CscSensorData::decode(uint8_t *data, size_t length) {
  uint8_t flags = data[0];
  int pos = 1;  // Start after flags byte

  // Check wheel revolution data present flag (bit 0)
  if (flags & 0x01) {
    uint32_t wheelRevolutions = get_le32(&data[pos]);
    pos += 4;
    uint16_t wheelEventTime = get_le16(&data[pos]);
    pos += 2;

    // Calculate speed if we have previous measurements
    if (lastWheelEventTime > 0) {
      // Handle timer wraparound (16-bit timer)
      uint16_t timeDiff = (wheelEventTime >= lastWheelEventTime) ? 
                         (wheelEventTime - lastWheelEventTime) : 
                         (65535 - lastWheelEventTime + wheelEventTime);
      
      if (timeDiff > 0) {
        // Convert to meters/second then km/h
        // Time is in 1/1024th of a second
        float wheelCircumference = 2.095f; // Default 700c wheel circumference in meters
        float revolutions = wheelRevolutions - lastWheelRevolutions;
        float timeSeconds = timeDiff / 1024.0f;
        float speedMS = (revolutions * wheelCircumference) / timeSeconds;
        this->speed = speedMS * 3.6f; // Convert m/s to km/h
      }
    }

    lastWheelRevolutions = wheelRevolutions;
    lastWheelEventTime = wheelEventTime;
  }

  // Check crank revolution data present flag (bit 1)
  if (flags & 0x02) {
    uint16_t crankRevolutions = get_le16(&data[pos]);
    pos += 2;
    uint16_t crankEventTime = get_le16(&data[pos]);
    pos += 2;

    // Calculate cadence if we have previous measurements
    if (lastCrankEventTime > 0) {
      // Handle timer wraparound (16-bit timer)
      uint16_t timeDiff = (crankEventTime >= lastCrankEventTime) ?
                         (crankEventTime - lastCrankEventTime) :
                         (65535 - lastCrankEventTime + crankEventTime);
      
      if (timeDiff > 0) {
        // Time is in 1/1024th of a second
        float revolutions = crankRevolutions - lastCrankRevolutions;
        float timeMinutes = (timeDiff / 1024.0f) / 60.0f;
        float cadence = revolutions / timeMinutes;
        
        if (cadence > 1) {
          if (cadence > 200) {  // Human is unlikely producing 200+ cadence
            // Cadence Error: Could happen if cadence measurements were missed
            //                Leave cadence unchanged
            cadence = this->cadence;
          }
          this->cadence = cadence;
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

    lastCrankRevolutions = crankRevolutions;
    lastCrankEventTime = crankEventTime;
  }
}