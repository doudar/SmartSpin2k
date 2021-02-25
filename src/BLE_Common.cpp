// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

#include "Main.h"
#include <math.h>
#include "BLE_Common.h"

// See: https://github.com/oesmith/gatt-xml/blob/master/org.bluetooth.characteristic.indoor_bike_data.xml
uint8_t     const FitnessMachineIndoorBikeData::flagBitIndices[FieldCount]    = {    0,    1,   2,   3,   4,   5,   6,   7,   8,   8,   8,   9,  10,  11,   12 };
uint8_t     const FitnessMachineIndoorBikeData::flagEnabledValues[FieldCount] = {    0,    1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,    1 };
size_t      const FitnessMachineIndoorBikeData::byteSizes[FieldCount]         = {    2,    2,   2,   2,   3,   2,   2,   2,   2,   2,   1,   1,   1,   2,    2 };
uint8_t     const FitnessMachineIndoorBikeData::signedFlags[FieldCount]       = {    0,    0,   0,   0,   0,   1,   1,   1,   0,   0,   0,   0,   0,   0,    0 };
double_t    const FitnessMachineIndoorBikeData::resolutions[FieldCount]       = { 0.01, 0.01, 0.5, 0.5, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.1, 1.0,  1.0 };

//https://github.com/oesmith/gatt-xml/blob/master/org.bluetooth.characteristic.cycling_power_measurement.xml
//                                                                               IPWR, PPB,  PPR,    AT, ATS,    WRD,    CRD, EFM,  ETM,  EA, TDS, BDS,   AE, OCI 
uint8_t     const CyclingPowerMeasurement::flagBitIndices[FieldCount]         = {  37,    0,    1,     2,   3,      4,      5,   6,    7,   8,   9,  10,   11,  12};
uint8_t     const CyclingPowerMeasurement::flagEnabledValues[FieldCount]      = {   0,    1,    1,     1,   1,      1,      1,   1,    1,   1,   1,   1,   1,   1};
size_t      const CyclingPowerMeasurement::byteSizes[FieldCount]              = {   2,    1,    0,     2,   0,      6,      4,   4,    4,   6,   2,   2,   2,   0};
uint8_t     const CyclingPowerMeasurement::signedFlags[FieldCount]            = {   1,    0,    0,     0,   0,      0,      0,   1,    1,   0,   0,   0,   0,   0};
double_t    const CyclingPowerMeasurement::resolutions[FieldCount]            = {   1,  1.0,   .5,  1/32, 1.0, 1/2048, 1/1024, 1.0, 1/32, 1.0, 1.0, 1.0, 1.0, 1.0};

std::unique_ptr<SensorData> SensorDataFactory::getSensorData(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length) {

    if (characteristic->getUUID() == HEARTCHARACTERISTIC_UUID) {
        return std::unique_ptr<SensorData>(new HeartRateData(characteristic, data, length));
    }

    if (characteristic->getUUID() == FLYWHEEL_UART_SERVICE_UUID) {
        return std::unique_ptr<SensorData>(new FlywheelData(characteristic, data, length));
    }

    if (characteristic->getUUID() == FITNESSMACHINEINDOORBIKEDATA_UUID) {
        return std::unique_ptr<SensorData>(new FitnessMachineIndoorBikeData(characteristic, data, length));
    }

    if (characteristic->getUUID() == CYCLINGPOWERMEASUREMENT_UUID) {
        return std::unique_ptr<SensorData>(new CyclingPowerMeasurement(characteristic, data, length));
    }

    return std::unique_ptr<SensorData>(new NullData(characteristic, data, length));
}

String SensorData::getId() {
    return id;
}

bool    NullData::hasHeartRate()        { return false; }
bool    NullData::hasCadence()          { return false; }
bool    NullData::hasPower()            { return false; }
int     NullData::getHeartRate()        { return INT_MIN; }
float   NullData::getCadence()          { return NAN; }
int     NullData::getPower()            { return INT_MIN; }

bool    HeartRateData::hasHeartRate()   { return true; }
bool    HeartRateData::hasCadence()     { return false; }
bool    HeartRateData::hasPower()       { return false; }
int     HeartRateData::getHeartRate()   { return (int)data[1]; }
float   HeartRateData::getCadence()     { return NAN; }
int     HeartRateData::getPower()       { return INT_MIN; }

bool    FlywheelData::hasHeartRate()    { return false; }
bool    FlywheelData::hasCadence()      { return data[0] == 0xFF; }
bool    FlywheelData::hasPower()        { return data[0] == 0xFF; }
int     FlywheelData::getHeartRate()    { return INT_MIN; }

float   FlywheelData::getCadence() {
    if (!hasCadence()) {
        return NAN;
    }
    return float(bytes_to_u16(data[4], data[3]));
}

int     FlywheelData::getPower() { 
    if (!hasPower()) {
        return INT_MIN; 
    }
    return data[12];
}

bool FitnessMachineIndoorBikeData::hasHeartRate() {
    return values[Types::HeartRate] != NAN;
}

bool FitnessMachineIndoorBikeData::hasCadence() {
    return values[Types::InstantaneousCadence] != NAN;
}

bool FitnessMachineIndoorBikeData::hasPower() {
    return values[Types::InstantaneousPower] != NAN;
}

int FitnessMachineIndoorBikeData::getHeartRate() {
    double_t value = values[Types::HeartRate];
    if (value == NAN) {
        return INT_MIN;
    }
    return int(value);
}

float FitnessMachineIndoorBikeData::getCadence() {
    double_t value = values[Types::InstantaneousCadence];
    if (value == NAN) {
        return nanf("");
    }
    return float(value);
}

int FitnessMachineIndoorBikeData::getPower() {
    double_t value = values[Types::InstantaneousPower];
    if (value == NAN) {
        return INT_MIN;
    }
    return int(value);
}

FitnessMachineIndoorBikeData::FitnessMachineIndoorBikeData(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length) : 
        SensorData("FTMS", characteristic, data, length), flags(bytes_to_u16(data[1], data[0])) {
    uint8_t dataIndex = 2;
    values = new double_t[FieldCount];
    std::fill_n(values, FieldCount, NAN);
    for (int typeIndex = Types::InstantaneousSpeed; typeIndex <= Types::RemainingTime; typeIndex++) {
        if (bitRead(flags, flagBitIndices[typeIndex]) == flagEnabledValues[typeIndex]) {
            uint8_t byteSize = byteSizes[typeIndex];
            if (byteSize > 0) {
                int value = data[dataIndex];
                for (int dataOffset = 1; dataOffset < byteSize; dataOffset++) {
                    uint8_t dataByte = data[dataIndex + dataOffset];
                    value |= (dataByte << (dataOffset * 8));
                }
                dataIndex += byteSize;
                value = convert(value, byteSize, signedFlags[typeIndex]);
                double_t result = double_t(int((value * resolutions[typeIndex] * 10) + 0.5)) / 10.0;
                values[typeIndex] = result;
            }
        }
    }
}

FitnessMachineIndoorBikeData::~FitnessMachineIndoorBikeData() {
    delete []values;
}

int FitnessMachineIndoorBikeData::convert(int value, size_t length, uint8_t isSigned) {
    int mask = 255 * length;
    int convertedValue = value & mask;
    if (isSigned) {
        convertedValue = convertedValue - (convertedValue >> (length * 8 - 1) << (length * 8));
    }
    return convertedValue;
}


/***************************************************************************************************/

int  CyclingPowerMeasurement::getHeartRate()    { return INT_MIN; }
bool CyclingPowerMeasurement::hasHeartRate()    { return false; }
 
bool CyclingPowerMeasurement::hasCadence() {
    return values[Types::CrankRevolutionData] != NAN;
}

bool CyclingPowerMeasurement::hasPower() {
    return values[Types::InstantaneousPower] != NAN;
}

float CyclingPowerMeasurement::getCadence() {
    double_t value = values[Types::CrankRevolutionData];
    if (value == NAN) {
        return nanf("");
    }
    return float(value);
}

int CyclingPowerMeasurement::getPower() {
    double_t value = values[Types::InstantaneousPower];
    if (value == NAN) {
        return INT_MIN;
    }
    return int(value);
}

CyclingPowerMeasurement::CyclingPowerMeasurement(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length) : 
        SensorData("CPS", characteristic, data, length), flags(bytes_to_u16(data[1], data[0])) {
    uint8_t dataIndex = 2;
    values = new double_t[FieldCount];
    std::fill_n(values, FieldCount, NAN);
    for (int typeIndex = Types::InstantaneousPower; typeIndex <= Types::OffsetCompensation; typeIndex++) {
        if (bitRead(flags, flagBitIndices[typeIndex]) == flagEnabledValues[typeIndex]) {
            uint8_t byteSize = byteSizes[typeIndex];
            if (byteSize > 0) {
                int value = data[dataIndex];
                for (int dataOffset = 1; dataOffset < byteSize; dataOffset++) {
                    uint8_t dataByte = data[dataIndex + dataOffset];
                    value |= (dataByte << (dataOffset * 8));
                }
                dataIndex += byteSize;
                value = convert(value, byteSize, signedFlags[typeIndex]);
                double_t result = double_t(int((value * resolutions[typeIndex] * 10) + 0.5)) / 10.0;
                values[typeIndex] = result;
            }
        }
    }
}

CyclingPowerMeasurement::~CyclingPowerMeasurement() {
    delete []values;
}

int CyclingPowerMeasurement::convert(int value, size_t length, uint8_t isSigned) {
    int mask = 255 * length;
    int convertedValue = value & mask;
    if (isSigned) {
        convertedValue = convertedValue - (convertedValue >> (length * 8 - 1) << (length * 8));
    }
    return convertedValue;
}