#pragma once

#include "SensorData.h"

class EchelonData : public SensorData
{
public:
    EchelonData() : SensorData("ECH"){};

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
    int resistance;
};