// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Joel Baranick and Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

#include "BLE_Common.h"
#include "Main.h"

// See: https://github.com/oesmith/gatt-xml/blob/master/org.bluetooth.characteristic.indoor_bike_data.xml
uint8_t const FitnessMachineIndoorBikeData::flagBitIndices[FieldCount] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8, 9, 10, 11, 12};
uint8_t const FitnessMachineIndoorBikeData::flagEnabledValues[FieldCount] = {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
size_t const FitnessMachineIndoorBikeData::byteSizes[FieldCount] = {2, 2, 2, 2, 3, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2};
uint8_t const FitnessMachineIndoorBikeData::signedFlags[FieldCount] = {0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
double_t const FitnessMachineIndoorBikeData::resolutions[FieldCount] = {0.01, 0.01, 0.5, 0.5, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.1, 1.0, 1.0};

NimBLEUUID KnownDevice::getUUID() {
    return this->uuid;
}

std::shared_ptr<SensorData> KnownDevice::decode(uint8_t *data, size_t length) {
    this->sensorData->decode(data, length);
    return this->sensorData;
}

std::shared_ptr<SensorData> SensorDataFactory::NULL_SENSOR_DATA = std::shared_ptr<SensorData>(new NullData());

std::shared_ptr<SensorData> SensorDataFactory::getSensorData(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length)
{
    auto uuid = characteristic->getUUID();
    for (auto &it : SensorDataFactory::knownDevices) {
        if (it->getUUID() == uuid) {
            return it->decode(data, length);
        }
    }

    std::shared_ptr<SensorData> sensorData = NULL_SENSOR_DATA;
    if (uuid == CYCLINGPOWERMEASUREMENT_UUID)
    {
        sensorData = std::shared_ptr<SensorData>(new CyclePowerData());
    }
    else if (uuid == HEARTCHARACTERISTIC_UUID)
    {
        sensorData = std::shared_ptr<SensorData>(new HeartRateData());
    }
    else if (uuid == FITNESSMACHINEINDOORBIKEDATA_UUID)
    {
        sensorData = std::shared_ptr<SensorData>(new FitnessMachineIndoorBikeData());
    } 
    else if (uuid == FLYWHEEL_UART_SERVICE_UUID)
    {
        sensorData = std::shared_ptr<SensorData>(new FlywheelData());
    }
    else 
    {
        return NULL_SENSOR_DATA;
    }

    KnownDevice *knownDevice = new KnownDevice(uuid, sensorData);
    SensorDataFactory::knownDevices.push_back(knownDevice);
    return knownDevice->decode(data, length);
}

String SensorData::getId()
{
    return id;
}

bool NullData::hasHeartRate() { return false; }
bool NullData::hasCadence() { return false; }
bool NullData::hasPower() { return false; }
bool NullData::hasSpeed() { return false; }
int NullData::getHeartRate() { return INT_MIN; }
float NullData::getCadence() { return NAN; }
int NullData::getPower() { return INT_MIN; }
float NullData::getSpeed() { return NAN; }
void NullData::decode(uint8_t *data, size_t length) {}

bool HeartRateData::hasHeartRate() { return true; }
bool HeartRateData::hasCadence() { return false; }
bool HeartRateData::hasPower() { return false; }
bool HeartRateData::hasSpeed() { return false; }
int HeartRateData::getHeartRate() { return this->heartrate; }
float HeartRateData::getCadence() { return NAN; }
int HeartRateData::getPower() { return INT_MIN; }
float HeartRateData::getSpeed() { return NAN; }
void HeartRateData::decode(uint8_t *data, size_t length) {
    this->heartrate = (int)data[1];
}

bool CyclePowerData::hasHeartRate() { return false; }
bool CyclePowerData::hasCadence() { return false; }
bool CyclePowerData::hasPower() { return false; }
bool CyclePowerData::hasSpeed() { return false; }
int CyclePowerData::getHeartRate() { return INT_MIN; }
float CyclePowerData::getCadence() { return NAN; }
int CyclePowerData::getPower() { return INT_MIN; }
float CyclePowerData::getSpeed() { return NAN; }
void CyclePowerData::decode(uint8_t *data, size_t length) {
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
            int cadence = abs(this->crankRev - this->lastCrankRev * 1024) / abs(this->crankEventTime - this->lastCrankEventTime) * 60;
            if (cadence > 1)
            {
                if (cadence > 200) //Cadence Error
                {
                    // TODO: Should this be NAN
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
                // TODO: Should this be NAN
                this->cadence = 0;
            }
            this->missedReadingCount++;
        }
    }
}

bool FlywheelData::hasHeartRate() { return false; }
bool FlywheelData::hasCadence() { return this->hasData; }
bool FlywheelData::hasPower() { return this->hasData; }
bool FlywheelData::hasSpeed() { return false; }
int FlywheelData::getHeartRate() { return INT_MIN; }
float FlywheelData::getCadence() { return this->cadence; };
int FlywheelData::getPower() { return this->power; };
float FlywheelData::getSpeed() { return NAN; }
void FlywheelData::decode(uint8_t *data, size_t length) {
    if (data[0] == 0xFF) {
        cadence = float(bytes_to_u16(data[4], data[3]));
        power = data[12];
        hasData = true;
    } else {
        cadence = NAN;
        power = INT_MIN;
        hasData = false;
    }
}

bool FitnessMachineIndoorBikeData::hasHeartRate()
{
    return values[Types::HeartRate] != NAN;
}

bool FitnessMachineIndoorBikeData::hasCadence()
{
    return values[Types::InstantaneousCadence] != NAN;
}

bool FitnessMachineIndoorBikeData::hasPower()
{
    return values[Types::InstantaneousPower] != NAN;
}

bool FitnessMachineIndoorBikeData::hasSpeed()
{
    return values[Types::InstantaneousSpeed] != NAN;
}

int FitnessMachineIndoorBikeData::getHeartRate()
{
    double_t value = values[Types::HeartRate];
    if (value == NAN)
    {
        return INT_MIN;
    }
    return int(value);
}

float FitnessMachineIndoorBikeData::getCadence()
{
    double_t value = values[Types::InstantaneousCadence];
    if (value == NAN)
    {
        return nanf("");
    }
    return float(value);
}

int FitnessMachineIndoorBikeData::getPower()
{
    double_t value = values[Types::InstantaneousPower];
    if (value == NAN)
    {
        return INT_MIN;
    }
    return int(value);
}

float FitnessMachineIndoorBikeData::getSpeed()
{
    double_t value = values[Types::InstantaneousSpeed];
    if (value == NAN)
    {
        return nanf("");
    }
    return float(value);
}

void FitnessMachineIndoorBikeData::decode(uint8_t *data, size_t length) {
    int flags = bytes_to_u16(data[1], data[0]);
    uint8_t dataIndex = 2;
    for (int typeIndex = Types::InstantaneousSpeed; typeIndex <= Types::RemainingTime; typeIndex++)
    {
        if (bitRead(flags, flagBitIndices[typeIndex]) == flagEnabledValues[typeIndex])
        {
            uint8_t byteSize = byteSizes[typeIndex];
            if (byteSize > 0)
            {
                int value = data[dataIndex];
                for (int dataOffset = 1; dataOffset < byteSize; dataOffset++)
                {
                    uint8_t dataByte = data[dataIndex + dataOffset];
                    value |= (dataByte << (dataOffset * 8));
                }
                dataIndex += byteSize;
                value = convert(value, byteSize, signedFlags[typeIndex]);
                double_t result = double_t(int((value * resolutions[typeIndex] * 10) + 0.5)) / 10.0;
                values[typeIndex] = result;
                continue;
            }
        }
        values[typeIndex] = NAN;
    }
}

int FitnessMachineIndoorBikeData::convert(int value, size_t length, uint8_t isSigned)
{
    int mask = (1u << (8 * length)) - (1u);
    int convertedValue = value & mask;
    if (isSigned)
    {
        convertedValue = convertedValue - (convertedValue >> (length * 8 - 1) << (length * 8));
    }
    return convertedValue;
}