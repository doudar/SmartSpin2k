#include "BLE_Common.h"
#include "sensors/EchelonData.h"

bool EchelonData::hasHeartRate() { return false; }

bool EchelonData::hasCadence() { return this->hasData; }

bool EchelonData::hasPower() { return this->hasData; }

bool EchelonData::hasSpeed() { return false; }

int EchelonData::getHeartRate() { return INT_MIN; }

float EchelonData::getCadence() { return this->cadence; };

int EchelonData::getPower() { return this->power; };

float EchelonData::getSpeed() { return NAN; }

void EchelonData::decode(uint8_t *data, size_t length)
{
    switch (data[1])
    {
    // Cadence notification
    case 0xD1:
        //runtime = int((data[7] << 8) + data[8]); // This runtime has massive drift
        cadence = int((data[9] << 8) + data[10]);
        hasData = true;
        if (cadence == 0 || resistance == 0)
        {
            power = (pow(1.090112, resistance) * pow(1.015343, cadence) * 7.228958);
            break;
        }      
        break;
    // Resistance notification
    case 0xD2:
        resistance = int(data[3]);
        hasData = true;
        if (cadence == 0 || resistance == 0)
        {
            power = (pow(1.090112, resistance) * pow(1.015343, cadence) * 7.228958);
            break;
        }
        break;
    //Data 1 didn't match
    default:
        hasData = false;
    }
}