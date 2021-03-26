/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BLE_Common.h"
#include "sensors/SensorDataFactory.h"
#include "sensors/SensorData.h"
#include "sensors/CyclePowerData.h"
#include "sensors/FlywheelData.h"
#include "sensors/FitnessMachineIndoorBikeData.h"
#include "sensors/HeartRateData.h"
#include "sensors/EchelonData.h"

std::shared_ptr<SensorData> SensorDataFactory::getSensorData(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length) {
  for (auto &it : SensorDataFactory::knownDevices) {
    if (it->isSameDeviceCharacteristic(characteristic)) {
      return it->decode(data, length);
    }
  }

  auto characteristicUuid                = characteristic->getUUID();
  std::shared_ptr<SensorData> sensorData = NULL_SENSOR_DATA;
  if (characteristicUuid == CYCLINGPOWERMEASUREMENT_UUID) {
    sensorData = std::shared_ptr<SensorData>(new CyclePowerData());
  } else if (characteristicUuid == HEARTCHARACTERISTIC_UUID) {
    sensorData = std::shared_ptr<SensorData>(new HeartRateData());
  } else if (characteristicUuid == FITNESSMACHINEINDOORBIKEDATA_UUID) {
    sensorData = std::shared_ptr<SensorData>(new FitnessMachineIndoorBikeData());
  } else if (characteristicUuid == FLYWHEEL_UART_SERVICE_UUID) {
    sensorData = std::shared_ptr<SensorData>(new FlywheelData());
  } else if (characteristicUuid == ECHELON_DATA_UUID) {
    sensorData = std::shared_ptr<SensorData>(new EchelonData());
  } else {
    return NULL_SENSOR_DATA;
  }

  KnownDevice *knownDevice = new KnownDevice(characteristic, sensorData);
  SensorDataFactory::knownDevices.push_back(knownDevice);
  return knownDevice->decode(data, length);
}

std::shared_ptr<SensorData> SensorDataFactory::KnownDevice::decode(uint8_t *data, size_t length) {
  this->sensorData->decode(data, length);
  return this->sensorData;
}

bool SensorDataFactory::KnownDevice::isSameDeviceCharacteristic(BLERemoteCharacteristic *characteristic) {
  return this->characteristicId == characteristic->getUUID() && this->peerAddress == characteristic->getRemoteService()->getClient()->getPeerAddress();
}

bool SensorDataFactory::NullData::hasHeartRate() { return false; }

bool SensorDataFactory::NullData::hasCadence() { return false; }

bool SensorDataFactory::NullData::hasPower() { return false; }

bool SensorDataFactory::NullData::hasSpeed() { return false; }

int SensorDataFactory::NullData::getHeartRate() { return INT_MIN; }

float SensorDataFactory::NullData::getCadence() { return NAN; }

int SensorDataFactory::NullData::getPower() { return INT_MIN; }

float SensorDataFactory::NullData::getSpeed() { return nanf(""); }

void SensorDataFactory::NullData::decode(uint8_t *data, size_t length) {}

std::shared_ptr<SensorData> SensorDataFactory::NULL_SENSOR_DATA = std::shared_ptr<SensorData>(new NullData());
