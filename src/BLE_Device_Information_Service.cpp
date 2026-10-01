/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BLE_Device_Information_Service.h"
#include "Constants.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <array>
#include <cstdio>

namespace {
constexpr char kDisManufacturerName[] = "SmartSpin2k";
constexpr char kDisModelNumber[] = "SmartSpin2k";
constexpr char kDisHardwareRevision[] = "SmartSpin2k";

String buildSerialNumber(uint64_t efuseMac) {
  char serialNumber[13];
  snprintf(serialNumber, sizeof(serialNumber), "%012llX", efuseMac & 0xFFFFFFFFFFFFULL);
  return String(serialNumber);
}

std::array<uint8_t, 8> buildSystemId(uint64_t efuseMac) {
  const uint8_t mac0 = static_cast<uint8_t>((efuseMac >> 40) & 0xFF);
  const uint8_t mac1 = static_cast<uint8_t>((efuseMac >> 32) & 0xFF);
  const uint8_t mac2 = static_cast<uint8_t>((efuseMac >> 24) & 0xFF);
  const uint8_t mac3 = static_cast<uint8_t>((efuseMac >> 16) & 0xFF);
  const uint8_t mac4 = static_cast<uint8_t>((efuseMac >> 8) & 0xFF);
  const uint8_t mac5 = static_cast<uint8_t>(efuseMac & 0xFF);

  return {mac0, mac1, mac2, 0xFF, 0xFE, mac3, mac4, mac5};
}
}

void BLE_Device_Information_Service::setupService(NimBLEServer* pServer) {
  const uint64_t efuseMac           = ESP.getEfuseMac();
  const String serialNumber         = buildSerialNumber(efuseMac);
  const std::array<uint8_t, 8> systemId = buildSystemId(efuseMac);

  NimBLEService* const deviceInformationService = pServer->createService(DEVICE_INFORMATION_SERVICE_UUID);

  NimBLECharacteristic* const manufacturerNameCharacteristic =
      deviceInformationService->createCharacteristic(MANUFACTURER_NAME_UUID, NIMBLE_PROPERTY::READ);
  manufacturerNameCharacteristic->setValue(kDisManufacturerName);

  NimBLECharacteristic* const modelNumberCharacteristic = deviceInformationService->createCharacteristic(MODEL_NUMBER_UUID, NIMBLE_PROPERTY::READ);
  modelNumberCharacteristic->setValue(kDisModelNumber);

  NimBLECharacteristic* const serialNumberCharacteristic = deviceInformationService->createCharacteristic(SERIAL_NUMBER_UUID, NIMBLE_PROPERTY::READ);
  serialNumberCharacteristic->setValue(serialNumber);

  NimBLECharacteristic* const hardwareRevisionCharacteristic = deviceInformationService->createCharacteristic(HARDWARE_REVISION_UUID, NIMBLE_PROPERTY::READ);
  hardwareRevisionCharacteristic->setValue(kDisHardwareRevision);

  NimBLECharacteristic* const firmwareRevisionCharacteristic = deviceInformationService->createCharacteristic(FIRMWARE_REVISION_UUID, NIMBLE_PROPERTY::READ);
  firmwareRevisionCharacteristic->setValue(FIRMWARE_VERSION);

  NimBLECharacteristic* const softwareRevisionCharacteristic = deviceInformationService->createCharacteristic(SOFTWARE_REVISION_UUID, NIMBLE_PROPERTY::READ);
  softwareRevisionCharacteristic->setValue(FIRMWARE_VERSION);

  NimBLECharacteristic* const systemIdCharacteristic = deviceInformationService->createCharacteristic(SYSTEM_ID_UUID, NIMBLE_PROPERTY::READ);
  systemIdCharacteristic->setValue(systemId.data(), systemId.size());

}
