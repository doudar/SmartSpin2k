/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <algorithm>
#include <cmath>
#include "Constants.h"
#include "sensors/SensorDataFactory.h"
#include "sensors/CyclePowerData.h"
#include "sensors/FlywheelData.h"
#include "sensors/FitnessMachineIndoorBikeData.h"
#include "sensors/HeartRateData.h"
#include "sensors/EchelonData.h"
#include "sensors/PelotonData.h"
#include "sensors/CscSensorData.h"
#include "sensors/ChronoData.h"

std::shared_ptr<SensorData> SensorDataFactory::getSensorData(const NimBLEUUID& characteristicUUID, const std::string& uniqueName, uint8_t *data, size_t length) {
  const auto knownDevice = std::find_if(knownDevices.begin(), knownDevices.end(), [&](const KnownDevice& device) {
    return device.isSameDeviceCharacteristic(characteristicUUID, uniqueName);
  });
  if (knownDevice != knownDevices.end()) {
    return knownDevice->decode(data, length);
  }

  std::shared_ptr<SensorData> sensorData;
  if (characteristicUUID == CYCLINGPOWERMEASUREMENT_UUID) {
    sensorData = std::make_shared<CyclePowerData>();
  } else if (characteristicUUID == HEARTCHARACTERISTIC_UUID) {
    sensorData = std::make_shared<HeartRateData>();
  } else if (characteristicUUID == FITNESSMACHINEINDOORBIKEDATA_UUID) {
    sensorData = std::make_shared<FitnessMachineIndoorBikeData>();
  } else if (characteristicUUID == FLYWHEEL_UART_SERVICE_UUID) {
    sensorData = std::make_shared<FlywheelData>();
  } else if (characteristicUUID == ECHELON_DATA_UUID) {
    sensorData = std::make_shared<EchelonData>();
  } else if (characteristicUUID == CHRONO_DATA_UUID) {
    sensorData = std::make_shared<ChronoData>();
  } else if (characteristicUUID == PELOTON_DATA_UUID) {
    sensorData = std::make_shared<PelotonData>();
  } else if (characteristicUUID == CSCMEASUREMENT_UUID) {
    sensorData = std::make_shared<CscSensorData>();
  } else {
    return NULL_SENSOR_DATA;
  }

  knownDevices.emplace_back(characteristicUUID, uniqueName, std::move(sensorData));
  return knownDevices.back().decode(data, length);
}

std::shared_ptr<SensorData> SensorDataFactory::KnownDevice::decode(uint8_t *data, size_t length) {
  sensorData->decode(data, length);
  return sensorData;
}

bool SensorDataFactory::KnownDevice::isSameDeviceCharacteristic(const NimBLEUUID& characteristicUUID, const std::string& uniqueName) const {
  return this->characteristicId == characteristicUUID && this->uniqueName == uniqueName;
}

bool SensorDataFactory::NullData::hasHeartRate() { return false; }

bool SensorDataFactory::NullData::hasCadence() { return false; }

bool SensorDataFactory::NullData::hasPower() { return false; }

bool SensorDataFactory::NullData::hasSpeed() { return false; }

bool SensorDataFactory::NullData::hasResistance() { return false; }

int SensorDataFactory::NullData::getHeartRate() { return INT_MIN; }

float SensorDataFactory::NullData::getCadence() { return nanf(""); }

int SensorDataFactory::NullData::getPower() { return INT_MIN; }

float SensorDataFactory::NullData::getSpeed() { return nanf(""); }

int SensorDataFactory::NullData::getResistance() { return INT_MIN; }

void SensorDataFactory::NullData::decode(uint8_t *data, size_t length) {}

std::shared_ptr<SensorData> SensorDataFactory::NULL_SENSOR_DATA = std::make_shared<NullData>();
