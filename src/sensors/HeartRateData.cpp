// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud & Joel Baranick
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

#include "sensors/HeartRateData.h"

bool HeartRateData::hasHeartRate() { return true; }

bool HeartRateData::hasCadence() { return false; }

bool HeartRateData::hasPower() { return false; }

bool HeartRateData::hasSpeed() { return false; }

int HeartRateData::getHeartRate() { return this->heartrate; }

float HeartRateData::getCadence() { return NAN; }

int HeartRateData::getPower() { return INT_MIN; }

float HeartRateData::getSpeed() { return NAN; }

void HeartRateData::decode(uint8_t *data, size_t length)
{
    this->heartrate = (int)data[1];
}