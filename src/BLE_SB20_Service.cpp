/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BLE_SB20_Service.h"

BLE_SB20_Service::BLE_SB20_Service() : pService(nullptr), pCharacteristic(nullptr) {
  currentData.gear      = 1;
  currentData.cadence   = 0;
  currentData.power     = 0;
  currentData.heartrate = 0;
  currentData.battery   = 4;  // Always full
}

void BLE_SB20_Service::begin() {
  pService        = BLEDevice::createServer()->createService(SB20_SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(SB20_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  pService->start();
  SS2K_LOG(SS2K_LOG_TAG,"SB20 Service started\n");
}

void BLE_SB20_Service::setData(SB20Data data) { currentData = data; }

void BLE_SB20_Service::notify() {
  if (pCharacteristic == nullptr) {
    return;
  }

  // Get current values and populate data struct
  int shifterPosition = rtConfig->getShifterPosition();
  shifterPosition     = constrain(shifterPosition, 1, 22);  // Clamp to 1-22

  currentData.gear      = shifterPosition;
  currentData.cadence   = rtConfig->cad.getValue();  // direct value, no scaling
  currentData.power     = rtConfig->watts.getValue();
  currentData.heartrate = rtConfig->hr.getValue();

  pCharacteristic->setValue((uint8_t *)&currentData, sizeof(currentData));
  pCharacteristic->notify();
  SS2K_LOG(SS2K_LOG_TAG,"SB20 data sent: Gear=%d, Cadence=%d, Power=%d, HR=%d\n", currentData.gear, currentData.cadence, currentData.power, currentData.heartrate);
}