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