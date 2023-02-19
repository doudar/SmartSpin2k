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
  BLEDevice::init(userConfig.getDeviceName());
  FTMSWrite = "";
  spinBLEClient.start();
  startBLEServer();

  xTaskCreatePinnedToCore(BLECommunications,      /* Task function. */
                          "BLECommunicationTask", /* name of task. */
                          6000,                   /* Stack size of task*/
                          NULL,                   /* parameter of the task */
                          3,                      /* priority of the task*/
                          &BLECommunicationTask,  /* Task handle to keep track of created task */
                          1);                     /* pin task to core */

  SS2K_LOG(BLE_SETUP_LOG_TAG, "BLE Notify Task Started");
  /*vTaskDelay(100 / portTICK_PERIOD_MS);
  if (strcmp(userConfig.getConnectedPowerMeter(), "none") != 0 || strcmp(userConfig.getConnectedHeartMonitor(), "none") != 0) {
    spinBLEClient.serverScan(true);
    SS2K_LOG(BLE_SETUP_LOG_TAG, "Scanning");
  }*/
  SS2K_LOG(BLE_SETUP_LOG_TAG, "%s %s %s", userConfig.getConnectedPowerMeter(), userConfig.getConnectedHeartMonitor(), userConfig.getConnectedRemote());
  SS2K_LOG(BLE_SETUP_LOG_TAG, "End BLE Setup");
}
