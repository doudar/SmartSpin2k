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
  SS2K_LOG("BLE_Setup", "Starting Arduino BLE Client application...");
  BLEDevice::init(userConfig.getDeviceName());
  spinBLEClient.start();
  startBLEServer();

  xTaskCreatePinnedToCore(BLECommunications,      /* Task function. */
                          "BLECommunicationTask", /* name of task. */
                          3000,                   /* Stack size of task*/
                          NULL,                   /* parameter of the task */
                          1,                      /* priority of the task*/
                          &BLECommunicationTask,  /* Task handle to keep track of created task */
                          1);                     /* pin task to core 0 */

  SS2K_LOG("BLE_Setup", "BLE Notify Task Started");
  vTaskDelay(100 / portTICK_PERIOD_MS);
  if (strcmp(userConfig.getconnectedPowerMeter(), "none") != 0 || strcmp(userConfig.getconnectedHeartMonitor(), "none") != 0) {
    spinBLEClient.serverScan(true);
    SS2K_LOG("BLE_Setup", "Scanning");
  }
  SS2K_LOG("BLE_Setup", "%s %s", userConfig.getconnectedPowerMeter(), userConfig.getconnectedHeartMonitor());
  SS2K_LOG("BLE_Setup", "End BLE Setup");
}
