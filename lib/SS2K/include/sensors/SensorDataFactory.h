/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <memory>
#include <NimBLEUUID.h>
#include <vector>
#include "sensors/SensorData.h"

class SensorDataFactory {
 public:
  SensorDataFactory() {}

  std::shared_ptr<SensorData> getSensorData(NimBLEUUID characteristicUUID, const uint64_t peerAddress, uint8_t *data, size_t length);

 private:
  class KnownDevice {
   public:
    KnownDevice(const NimBLEUUID characteristicUUID, const uint64_t peerAddress, std::shared_ptr<SensorData> sensorData)
        : characteristicId(characteristicUUID), peerAddress(peerAddress), sensorData(sensorData) {}
    std::shared_ptr<SensorData> decode(uint8_t *data, size_t length);
    bool isSameDeviceCharacteristic(const NimBLEUUID characteristicUUID, const uint64_t peerAddress);

   private:
    NimBLEUUID characteristicId;
    uint64_t peerAddress;
    std::shared_ptr<SensorData> sensorData;
  };

  class NullData : public SensorData {
   public:
    NullData() : SensorData("Null") {}

    virtual bool hasHeartRate();
    virtual bool hasCadence();
    virtual bool hasPower();
    virtual bool hasSpeed();
    virtual bool hasResistance();
    virtual int getHeartRate();
    virtual float getCadence();
    virtual int getPower();
    virtual float getSpeed();
    virtual int getResistance();
    virtual void decode(uint8_t *data, size_t length);
  };

  std::vector<KnownDevice *> knownDevices;
  static std::shared_ptr<SensorData> NULL_SENSOR_DATA;
};
