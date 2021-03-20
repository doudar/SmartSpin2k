#pragma once

#include <Arduino.h>

class SensorData
{
public:
    SensorData(String id) : id(id){};

    String getId();
    virtual bool hasHeartRate() = 0;
    virtual bool hasCadence() = 0;
    virtual bool hasPower() = 0;
    virtual bool hasSpeed() = 0;
    virtual int getHeartRate() = 0;
    virtual float getCadence() = 0;
    virtual int getPower() = 0;
    virtual float getSpeed() = 0;
    virtual void decode(uint8_t *data, size_t length) = 0;

private:
    String id;
};