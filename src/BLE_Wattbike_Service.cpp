/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BLE_Wattbike_Service.h"
#include "BLE_Common.h"
#include <Constants.h>

BLE_Wattbike_Service::BLE_Wattbike_Service() : pWattbikeService(nullptr), wattbikeReadCharacteristic(nullptr), wattbikeWriteCharacteristic(nullptr) {}

void BLE_Wattbike_Service::setupService(NimBLEServer *pServer) {
  // Create Wattbike service
  pWattbikeService = spinBLEServer.pServer->createService(WATTBIKE_SERVICE_UUID);

  // Create characteristic for gear notifications
  wattbikeReadCharacteristic = pWattbikeService->createCharacteristic(WATTBIKE_READ_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  // Create characteristic for receiving commands
  wattbikeWriteCharacteristic = pWattbikeService->createCharacteristic(WATTBIKE_WRITE_UUID, NIMBLE_PROPERTY::WRITE);

  // Start the service
  pWattbikeService->start();
  spinBLEServer.pServer->getAdvertising()->addServiceUUID(pWattbikeService->getUUID());
}

void BLE_Wattbike_Service::parseNemit() {
  static int lastGear = -1;  // Track last gear position
  static unsigned long lastNotifyTime = 0;  // Track last notification time
  const unsigned long NOTIFY_INTERVAL = 30000;  // 30 seconds in milliseconds
  
  // Get current shifter position
  int currentGear = rtConfig->getShifterPosition();
  if (currentGear < 1) {  // Ensure gear is at least 1
    currentGear = 1;
  }

  unsigned long currentTime = millis();
  
  // Only call update if gear changed or 30 seconds elapsed
  if (currentGear != lastGear || (currentTime - lastNotifyTime) >= NOTIFY_INTERVAL) {
    update();  // Call existing update function to handle notification
    
    // Update tracking variables
    lastGear = currentGear;
    lastNotifyTime = currentTime;
  }
}

void BLE_Wattbike_Service::update() {
  // Get current shifter position
  int currentGear = rtConfig->getShifterPosition();
  if (currentGear < 1) {  // Ensure gear is at least 1
    currentGear = 1;
  }

  // Create gear data packet with sequence number and fixed value
  static uint8_t seq = 0;
  ++seq;

  uint8_t gearData[4];
  gearData[0] = seq;                // Sequence number
  gearData[1] = 0x03;               // Fixed value
  gearData[2] = 0xB6;               // Fixed value
  gearData[3] = (byte)currentGear;  // Gear value

  // Update the characteristic
  wattbikeReadCharacteristic->setValue(gearData, sizeof(gearData));
  wattbikeReadCharacteristic->notify();

  // Log the update
  const int kLogBufCapacity = 100;
  char logBuf[kLogBufCapacity];
  logCharacteristic(logBuf, kLogBufCapacity, gearData, sizeof(gearData), WATTBIKE_SERVICE_UUID, wattbikeReadCharacteristic->getUUID(), "Wattbike Gear[ %d ]", currentGear);
}
