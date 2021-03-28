/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BLE_Common.h"
#include "sensors/FitnessMachineIndoorBikeData.h"

// See:
// https://github.com/oesmith/gatt-xml/blob/master/org.bluetooth.characteristic.indoor_bike_data.xml
uint8_t const FitnessMachineIndoorBikeData::flagBitIndices[FieldCount]    = {0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8, 9, 10, 11, 12};
uint8_t const FitnessMachineIndoorBikeData::flagEnabledValues[FieldCount] = {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
size_t const FitnessMachineIndoorBikeData::byteSizes[FieldCount]          = {2, 2, 2, 2, 3, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2};
uint8_t const FitnessMachineIndoorBikeData::signedFlags[FieldCount]       = {0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
double_t const FitnessMachineIndoorBikeData::resolutions[FieldCount]      = {0.01, 0.01, 0.5, 0.5, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.1, 1.0, 1.0};

bool FitnessMachineIndoorBikeData::hasHeartRate() { return values[Types::HeartRate] != NAN && values[Types::HeartRate] != 0; }

bool FitnessMachineIndoorBikeData::hasCadence() { return values[Types::InstantaneousCadence] != NAN; }

bool FitnessMachineIndoorBikeData::hasPower() { return values[Types::InstantaneousPower] != NAN; }

bool FitnessMachineIndoorBikeData::hasSpeed() { return values[Types::InstantaneousSpeed] != NAN; }

int FitnessMachineIndoorBikeData::getHeartRate() {
  double_t value = values[Types::HeartRate];
  if (value == NAN || value == 0) {
    return INT_MIN;
  }
  return static_cast<int>(value);
}

float FitnessMachineIndoorBikeData::getCadence() {
  double_t value = values[Types::InstantaneousCadence];
  if (value == NAN) {
    return nanf("");
  }
  return static_cast<float>(value);
}

int FitnessMachineIndoorBikeData::getPower() {
  double_t value = values[Types::InstantaneousPower];
  if (value == NAN) {
    return INT_MIN;
  }
  return static_cast<int>(value);
}

float FitnessMachineIndoorBikeData::getSpeed() {
  double_t value = values[Types::InstantaneousSpeed];
  if (value == NAN) {
    return nanf("");
  }
  return static_cast<float>(value);
}

void FitnessMachineIndoorBikeData::decode(uint8_t *data, size_t length) {
  int flags         = bytes_to_u16(data[1], data[0]);
  uint8_t dataIndex = 2;
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
        value             = convert(value, byteSize, signedFlags[typeIndex]);
        double_t result   = double_t(static_cast<int>((value * resolutions[typeIndex] * 10) + 0.5)) / 10.0;
        values[typeIndex] = result;
        continue;
      }
    }
    values[typeIndex] = NAN;
  }
}

int FitnessMachineIndoorBikeData::convert(int value, size_t length, uint8_t isSigned) {
  int mask           = (1u << (8 * length)) - (1u);
  int convertedValue = value & mask;
  if (isSigned) {
    convertedValue = convertedValue - (convertedValue >> (length * 8 - 1) << (length * 8));
  }
  return convertedValue;
}
