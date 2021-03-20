#pragma once

#include "SensorData.h"

class HeartRateData : public SensorData
{
public:
    HeartRateData() : SensorData("HRM"){};

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
    int heartrate;
};