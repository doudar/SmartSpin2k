/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BLE_Device_Information_Service.h"
#include "Constants.h"

BLE_Device_Information_Service::BLE_Device_Information_Service()
    : pDeviceInformationService(nullptr),
      pManufacturerNameCharacteristic(nullptr),
      pModelNumberCharacteristic(nullptr),
      pSerialNumberCharacteristic(nullptr),
      pHardwareRevisionCharacteristic(nullptr),
      pFirmwareRevisionCharacteristic(nullptr),
      pSoftwareRevisionCharacteristic(nullptr),
      pSystemIDCharacteristic(nullptr) {}

void BLE_Device_Information_Service::setupService(NimBLEServer* pServer) {
  pDeviceInformationService = pServer->createService(DEVICE_INFORMATION_SERVICE_UUID);

  pManufacturerNameCharacteristic = pDeviceInformationService->createCharacteristic(MANUFACTURER_NAME_UUID, NIMBLE_PROPERTY::READ);
  pManufacturerNameCharacteristic->setValue((String)userConfig->getDeviceName());

  pModelNumberCharacteristic = pDeviceInformationService->createCharacteristic(MODEL_NUMBER_UUID, NIMBLE_PROPERTY::READ);
  pModelNumberCharacteristic->setValue((String)userConfig->getDeviceName());

  pSerialNumberCharacteristic = pDeviceInformationService->createCharacteristic(SERIAL_NUMBER_UUID, NIMBLE_PROPERTY::READ);
  pSerialNumberCharacteristic->setValue((String)ESP.getEfuseMac());

  pHardwareRevisionCharacteristic = pDeviceInformationService->createCharacteristic(HARDWARE_REVISION_UUID, NIMBLE_PROPERTY::READ);
  pHardwareRevisionCharacteristic->setValue((String)userConfig->getDeviceName());

  pFirmwareRevisionCharacteristic = pDeviceInformationService->createCharacteristic(FIRMWARE_REVISION_UUID, NIMBLE_PROPERTY::READ);
  pFirmwareRevisionCharacteristic->setValue((String)ESP.getChipRevision());

  pSoftwareRevisionCharacteristic = pDeviceInformationService->createCharacteristic(SOFTWARE_REVISION_UUID, NIMBLE_PROPERTY::READ);
  pSoftwareRevisionCharacteristic->setValue(FIRMWARE_VERSION);

  pSystemIDCharacteristic = pDeviceInformationService->createCharacteristic(SYSTEM_ID_UUID, NIMBLE_PROPERTY::READ);
  pSystemIDCharacteristic->setValue((String)userConfig->getDeviceName());

  pDeviceInformationService->start();
}
