// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud & Joel Baranick
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

#include "BLE_Common.h"
#include "sensors/CyclePowerData.h"

bool CyclePowerData::hasHeartRate() { return false; }

bool CyclePowerData::hasCadence() { return this->cadence > 0 || this->missedReadingCount < 3; }

bool CyclePowerData::hasPower() { return true; }

bool CyclePowerData::hasSpeed() { return false; }

int CyclePowerData::getHeartRate() { return INT_MIN; }

float CyclePowerData::getCadence()
{
    if (!this->hasCadence())
    {
        return NAN;
    }
    return this->cadence;
}

int CyclePowerData::getPower()
{
    if (!this->hasPower())
    {
        return INT_MIN;
    }
    return this->power;
}

float CyclePowerData::getSpeed() { return NAN; }

void CyclePowerData::decode(uint8_t *data, size_t length)
{
    byte flags = data[0];
    int cPos = 2; //lowest position cadence could ever be
    //Instanious power is always present. Do that first.
    //first calculate which fields are present. Power is always 2 & 3, cadence can move depending on the flags.
    this->power = bytes_to_u16(data[cPos + 1], data[cPos]);
    cPos += 2;

    if (bitRead(flags, 0))
    {
        //pedal balance field present
        cPos++;
    }
    if (bitRead(flags, 1))
    {
        //pedal power balance reference
        //no field associated with this.
    }
    if (bitRead(flags, 2))
    {
        //accumulated torque field present
        cPos += 2;
    }
    if (bitRead(flags, 3))
    {
        //accumulated torque field source
        //no field associated with this.
    }
    if (bitRead(flags, 4))
    {
        //Wheel Revolution field PAIR Data present. 32-bits for wheel revs, 16 bits for wheel event time.
        //Why is that so hard to find in the specs?
        cPos += 6;
    }
    if (bitRead(flags, 5))
    {
        //Crank Revolution data present, lets process it.
        this->lastCrankRev = this->crankRev;
        this->crankRev = bytes_to_int(data[cPos + 1], data[cPos]);
        this->lastCrankEventTime = this->crankEventTime;
        this->crankEventTime = bytes_to_int(data[cPos + 3], data[cPos + 2]);
        if (this->crankRev > this->lastCrankRev && this->crankEventTime - this->lastCrankEventTime != 0)
        {
            int cadence = ((abs(this->crankRev - this->lastCrankRev) * 1024) / abs(this->crankEventTime - this->lastCrankEventTime)) * 60;
            if (cadence > 1)
            {
                if (cadence > 200) //Cadence Error
                {
                    cadence = 0;
                }
                this->cadence = cadence;
                this->missedReadingCount = 0;
            }
            else
            {
                this->missedReadingCount++;
            }
        }
        else //the crank rev probably didn't update
        {
            if (this->missedReadingCount > 2) //Require three consecutive readings before setting 0 cadence
            {
                this->cadence = 0;
            }
            this->missedReadingCount++;
        }
    }
}