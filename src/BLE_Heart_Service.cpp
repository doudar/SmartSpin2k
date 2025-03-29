/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "BLE_Heart_Service.h"
#include "DirConManager.h"
#include <Constants.h>

BLE_Heart_Service::BLE_Heart_Service() : pHeartService(nullptr), heartRateMeasurementCharacteristic(nullptr) {}

void BLE_Heart_Service::setupService(NimBLEServer *pServer, MyCharacteristicCallbacks *chrCallbacks) {
  // HEART RATE MONITOR SERVICE SETUP
  pHeartService                      = spinBLEServer.pServer->createService(HEARTSERVICE_UUID);
  heartRateMeasurementCharacteristic = pHeartService->createCharacteristic(HEARTCHARACTERISTIC_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  byte heartRateMeasurement[2]       = {0x00, 0x00};
  heartRateMeasurementCharacteristic->setValue(heartRateMeasurement, 2);
  heartRateMeasurementCharacteristic->setCallbacks(chrCallbacks);
  pHeartService->start();
  
  // Add service UUID to DirCon MDNS
  DirConManager::addBleServiceUuid(pHeartService->getUUID());
}

void BLE_Heart_Service::deinit() {
  if (pHeartService != nullptr) {
    if (heartRateMeasurementCharacteristic != nullptr) {
      pHeartService->removeCharacteristic(heartRateMeasurementCharacteristic);
      heartRateMeasurementCharacteristic = nullptr;
    }
    spinBLEServer.pServer->removeService(pHeartService);
    pHeartService = nullptr;
    spinBLEServer.pServer->getAdvertising()->stop();
    SS2K_LOG(BLE_COMMON_LOG_TAG, "Heart Rate Service De-initialized");
  }
}

void BLE_Heart_Service::update() {
  /* Check if heart rate monitor is connected
  if (String(userConfig->getConnectedHeartMonitor()) == "none") {
    deinit();
    return;
  }

  if (!pHeartService) {
    // Re-initialize if needed, most likely after changing HRM from none to a specific device
    MyCharacteristicCallbacks* chrCallbacks;
    setupService(spinBLEServer.pServer, chrCallbacks);
  }*/
 
  byte heartRateMeasurement[2] = {0x00, (byte)rtConfig->hr.getValue()};
  heartRateMeasurementCharacteristic->setValue(heartRateMeasurement, 2);
  heartRateMeasurementCharacteristic->notify();
  DirConManager::notifyCharacteristic(NimBLEUUID(HEARTSERVICE_UUID), heartRateMeasurementCharacteristic->getUUID(), heartRateMeasurement, 2);

  const int kLogBufCapacity = 125;  // Data(10), Sep(data/2), Arrow(3), CharId(37), Sep(3), CharId(37), Sep(3), Name(8), Prefix(2), HR(7), Suffix(2), Nul(1), rounded up
  char logBuf[kLogBufCapacity];
  const size_t heartRateMeasurementLength = sizeof(heartRateMeasurement) / sizeof(heartRateMeasurement[0]);
  logCharacteristic(logBuf, kLogBufCapacity, heartRateMeasurement, heartRateMeasurementLength, HEARTSERVICE_UUID, heartRateMeasurementCharacteristic->getUUID(),
                    "HRS(HRM)[ HR(%d) ]", rtConfig->hr.getValue() % 1000);
}
