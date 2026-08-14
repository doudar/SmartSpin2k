/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BLE_Device_Information_Service.h"
#include "Constants.h"

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

BLE_Device_Information_Service::BLE_Device_Information_Service()
    : pDeviceInformationService(nullptr),
      pManufacturerNameCharacteristic(nullptr),
      pModelNumberCharacteristic(nullptr),
      pSerialNumberCharacteristic(nullptr),
      pHardwareRevisionCharacteristic(nullptr),
      pFirmwareRevisionCharacteristic(nullptr),
      pSoftwareRevisionCharacteristic(nullptr),
      pSystemIDCharacteristic(nullptr),
      pPnPIDCharacteristic(nullptr) {}

void BLE_Device_Information_Service::setupService(NimBLEServer* pServer) {
  const uint64_t efuseMac           = ESP.getEfuseMac();
  const String serialNumber         = buildSerialNumber(efuseMac);
  const std::array<uint8_t, 8> systemId = buildSystemId(efuseMac);

  pDeviceInformationService = pServer->createService(DEVICE_INFORMATION_SERVICE_UUID);

  pManufacturerNameCharacteristic = pDeviceInformationService->createCharacteristic(MANUFACTURER_NAME_UUID, NIMBLE_PROPERTY::READ);
  pManufacturerNameCharacteristic->setValue(kDisManufacturerName);

  pModelNumberCharacteristic = pDeviceInformationService->createCharacteristic(MODEL_NUMBER_UUID, NIMBLE_PROPERTY::READ);
  pModelNumberCharacteristic->setValue(kDisModelNumber);

  pSerialNumberCharacteristic = pDeviceInformationService->createCharacteristic(SERIAL_NUMBER_UUID, NIMBLE_PROPERTY::READ);
  pSerialNumberCharacteristic->setValue(serialNumber);

  pHardwareRevisionCharacteristic = pDeviceInformationService->createCharacteristic(HARDWARE_REVISION_UUID, NIMBLE_PROPERTY::READ);
  pHardwareRevisionCharacteristic->setValue(kDisHardwareRevision);

  pFirmwareRevisionCharacteristic = pDeviceInformationService->createCharacteristic(FIRMWARE_REVISION_UUID, NIMBLE_PROPERTY::READ);
  pFirmwareRevisionCharacteristic->setValue(FIRMWARE_VERSION);

  pSoftwareRevisionCharacteristic = pDeviceInformationService->createCharacteristic(SOFTWARE_REVISION_UUID, NIMBLE_PROPERTY::READ);
  pSoftwareRevisionCharacteristic->setValue(FIRMWARE_VERSION);

  pSystemIDCharacteristic = pDeviceInformationService->createCharacteristic(SYSTEM_ID_UUID, NIMBLE_PROPERTY::READ);
  pSystemIDCharacteristic->setValue(systemId.data(), systemId.size());

}
