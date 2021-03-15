// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

#include "BLE_Common.h"
#include "Main.h"
#include <ArduinoJson.h>
#include <NimBLEDevice.h>


void setupBLE() //Common BLE setup for both client and server
{
  debugDirector("Starting Arduino BLE Client application...");
  BLEDevice::init(userConfig.getDeviceName());
  spinBLEClient.start();
  startBLEServer();

  xTaskCreatePinnedToCore(
      BLECommunications,      /* Task function. */
      "BLECommunicationTask", /* name of task. */
      3000,                   /* Stack size of task*/
      NULL,                   /* parameter of the task */
      1,                      /* priority of the task*/
      &BLECommunicationTask,  /* Task handle to keep track of created task */
      1);                     /* pin task to core 0 */

  debugDirector("BLE Notify Task Started");
  vTaskDelay(100 / portTICK_PERIOD_MS);
  if (!((String(userConfig.getconnectedPowerMeter()) == "none") && (String(userConfig.getconnectedHeartMonitor()) == "none")))
  {
    spinBLEClient.serverScan(true);
    debugDirector("Scanning");
  }
  debugDirector(String(userConfig.getconnectedPowerMeter()) + " " + String(userConfig.getconnectedHeartMonitor()));
  debugDirector("End BLE Setup");
}