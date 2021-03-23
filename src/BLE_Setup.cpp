/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BLE_Common.h"
#include "Main.h"
#include <ArduinoJson.h>
#include <NimBLEDevice.h>

void setupBLE() {  // Common BLE setup for both client and server
  debugDirector("Starting Arduino BLE Client application...");
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

  debugDirector("BLE Notify Task Started");
  vTaskDelay(100 / portTICK_PERIOD_MS);
  if (!((String(userConfig.getconnectedPowerMeter()) == "none") && (String(userConfig.getconnectedHeartMonitor()) == "none"))) {
    spinBLEClient.serverScan(true);
    debugDirector("Scanning");
  }
  debugDirector(String(userConfig.getconnectedPowerMeter()) + " " + String(userConfig.getconnectedHeartMonitor()));
  debugDirector("End BLE Setup");
}
