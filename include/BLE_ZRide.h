/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <NimBLEDevice.h>
#include "BLE_Common.h"
#include "SmartSpin_parameters.h"  // Required for rtConfig

// Forward declaration for extern
class BLE_Zwift_Ride_Service;
extern BLE_Zwift_Ride_Service zwiftRideService;

class BLE_Zwift_Ride_Service {
 public:
  BLE_Zwift_Ride_Service();
  void setupService(NimBLEServer *pServer);
  void update();
  void processZwiftSyncWrite(const uint8_t *data, size_t length);
  BLECharacteristic *zwiftRideAsyncCharacteristic;   // For notifications (button presses)
  BLECharacteristic *zwiftRideSyncTxCharacteristic;  // For sending data to client

 private:
  BLEService *pZwiftRideService;
  BLECharacteristic *zwiftRideSyncRxCharacteristic;  // For receiving data from client

  // To track gear changes
  int lastShifterPosition;
};