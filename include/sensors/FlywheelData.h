// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud & Joel Baranick
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

#pragma once

#include "SensorData.h"

class FlywheelData : public SensorData
{
public:
    FlywheelData() : SensorData("FLYW"){};

    bool hasHeartRate();
    bool hasCadence();
    bool hasPower();
    bool hasSpeed();
    int getHeartRate();
    float getCadence();
    int getPower();
    float getSpeed();
    void decode(uint8_t *data, size_t length);

private:
    bool hasData = false;
    float cadence;
    int power;
};