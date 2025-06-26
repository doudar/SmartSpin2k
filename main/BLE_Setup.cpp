/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BLE_Common.h"
#include "Main.h"
#include "SS2KLog.h"
#include <ArduinoJson.h>
#include <NimBLEDevice.h>

void setupBLE() {  // Common BLE setup for both client and server
  SS2K_LOG(BLE_SETUP_LOG_TAG, "Starting Arduino BLE Client application...");
  NimBLEDevice::init(userConfig->getDeviceName());
  NimBLEDevice::setMTU(515);  //-- enabling this is very important for BLE firmware updates.
  spinBLEClient.start();
  startBLEServer();
  SS2K_LOG(BLE_SETUP_LOG_TAG, "%s %s %s", userConfig->getConnectedPowerMeter(), userConfig->getConnectedHeartMonitor(), userConfig->getConnectedRemote());
  SS2K_LOG(BLE_SETUP_LOG_TAG, "End BLE Setup");
}
