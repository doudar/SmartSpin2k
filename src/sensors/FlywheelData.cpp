#include "BLE_Common.h"
#include "sensors/FlywheelData.h"

bool FlywheelData::hasHeartRate() { return false; }

bool FlywheelData::hasCadence() { return this->hasData; }

bool FlywheelData::hasPower() { return this->hasData; }

bool FlywheelData::hasSpeed() { return false; }

int FlywheelData::getHeartRate() { return INT_MIN; }

float FlywheelData::getCadence() { return this->cadence; };

int FlywheelData::getPower() { return this->power; };

float FlywheelData::getSpeed() { return NAN; }

void FlywheelData::decode(uint8_t *data, size_t length)
{
    if (data[0] == 0xFF)
    {
        cadence = float(bytes_to_u16(data[4], data[3]));
        power = data[12];
        hasData = true;
    }
    else
    {
        cadence = NAN;
        power = INT_MIN;
        hasData = false;
    }
}