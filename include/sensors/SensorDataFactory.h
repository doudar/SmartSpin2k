/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <memory>
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "SensorData.h"
#include "CyclePowerData.h"
#include "FlywheelData.h"
#include "FitnessMachineIndoorBikeData.h"
#include "HeartRateData.h"
#include "EchelonData.h"

class SensorDataFactory {
 public:
  SensorDataFactory() {}

  std::shared_ptr<SensorData> getSensorData(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length);

 private:
  class KnownDevice {
   public:
    KnownDevice(BLERemoteCharacteristic *characteristic, std::shared_ptr<SensorData> sensorData)
        : peerAddress(characteristic->getRemoteService()->getClient()->getPeerAddress()), characteristicId(characteristic->getUUID()), sensorData(sensorData) {}
    std::shared_ptr<SensorData> decode(uint8_t *data, size_t length);
    bool isSameDeviceCharacteristic(BLERemoteCharacteristic *characteristic);

   private:
    NimBLEAddress peerAddress;
    NimBLEUUID characteristicId;
    std::shared_ptr<SensorData> sensorData;
  };

  class NullData : public SensorData {
   public:
    NullData() : SensorData("Null") {}

    virtual bool hasHeartRate();
    virtual bool hasCadence();
    virtual bool hasPower();
    virtual bool hasSpeed();
    virtual int getHeartRate();
    virtual float getCadence();
    virtual int getPower();
    virtual float getSpeed();
    virtual void decode(uint8_t *data, size_t length);
  };

  std::vector<KnownDevice *> knownDevices;
  static std::shared_ptr<SensorData> NULL_SENSOR_DATA;
};
