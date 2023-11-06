/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "SS2KLog.h"
#include "BLE_Common.h"
#include "Constants.h"

#include <math.h>
#include <sensors/SensorData.h>
#include <sensors/SensorDataFactory.h>
#include <NimBLEDevice.h>

bool hr2p = false;

TaskHandle_t BLECommunicationTask;

void BLECommunications(void *pvParameters) {
  for (;;) {
    // if (!spinBLEClient.dontBlockScan) {
    //   NimBLEDevice::getScan()->stop();  // stop routine scans
    //}
    // **********************************Client***************************************
    for (size_t x = 0; x < NUM_BLE_DEVICES; x++) {  // loop through discovered devices
      if (spinBLEClient.myBLEDevices[x].connectedClientID != BLE_HS_CONN_HANDLE_NONE) {
        SS2K_LOGD(BLE_COMMON_LOG_TAG, "Address: (%s) Client ID: (%d) SerUUID: (%s) CharUUID: (%s) HRM: (%s) PM: (%s) CSC: (%s) CT: (%s) doConnect: (%s)",
                  spinBLEClient.myBLEDevices[x].peerAddress.toString().c_str(), spinBLEClient.myBLEDevices[x].connectedClientID,
                  spinBLEClient.myBLEDevices[x].serviceUUID.toString().c_str(), spinBLEClient.myBLEDevices[x].charUUID.toString().c_str(),
                  spinBLEClient.myBLEDevices[x].isHRM ? "true" : "false", spinBLEClient.myBLEDevices[x].isPM ? "true" : "false",
                  spinBLEClient.myBLEDevices[x].isCSC ? "true" : "false", spinBLEClient.myBLEDevices[x].isCT ? "true" : "false",
                  spinBLEClient.myBLEDevices[x].doConnect ? "true" : "false");
        if (spinBLEClient.myBLEDevices[x].advertisedDevice) {  // is device registered?
          SpinBLEAdvertisedDevice myAdvertisedDevice = spinBLEClient.myBLEDevices[x];
          if ((myAdvertisedDevice.connectedClientID != BLE_HS_CONN_HANDLE_NONE) && (myAdvertisedDevice.doConnect == false)) {  // client must not be in connection process
            if (BLEDevice::getClientByPeerAddress(myAdvertisedDevice.peerAddress)) {                                           // nullptr check
              BLEClient *pClient = NimBLEDevice::getClientByPeerAddress(myAdvertisedDevice.peerAddress);
              // Client connected with a valid UUID registered
              if ((myAdvertisedDevice.serviceUUID != BLEUUID((uint16_t)0x0000)) && (pClient->isConnected())) {
                BLERemoteCharacteristic *pRemoteBLECharacteristic = pClient->getService(myAdvertisedDevice.serviceUUID)->getCharacteristic(myAdvertisedDevice.charUUID);

                // Handle BLE HID Remotes
                if (spinBLEClient.myBLEDevices[x].serviceUUID == HID_SERVICE_UUID) {
                  spinBLEClient.keepAliveBLE_HID(pClient);  // keep alive doesn't seem to help :(
                  continue;                                 // There is not data that needs to be dequeued for the remote, so got to the next device.
                }

                // Dequeue sensor data we stored during notifications
                while (pdTRUE) {
                  NotifyData incomingNotifyData = myAdvertisedDevice.dequeueData();
                  if (incomingNotifyData.length == 0) {
                    break;
                  }
                  size_t length = incomingNotifyData.length;
                  uint8_t pData[length];

                  for (size_t i = 0; i < length; i++) {
                    pData[i] = incomingNotifyData.data[i];
                  }
                  collectAndSet(pRemoteBLECharacteristic->getUUID(), myAdvertisedDevice.serviceUUID, pRemoteBLECharacteristic->getRemoteService()->getClient()->getPeerAddress(),
                                pData, length);
                }

                spinBLEClient.handleBattInfo(pClient, false);

              } else if (!pClient->isConnected()) {  // This shouldn't ever be
                                                     // called...
                if (pClient->disconnect() == 0) {    // 0 is a successful disconnect
                  BLEDevice::deleteClient(pClient);
                  vTaskDelay(100 / portTICK_PERIOD_MS);
                  SS2K_LOG(BLE_COMMON_LOG_TAG, "Workaround connect");
                  myAdvertisedDevice.doConnect = true;
                }
              }
            }
          }
        }
      }
    }

    // ***********************************SERVER**************************************
    if ((spinBLEClient.connectedHRM || rtConfig.hr.getSimulate()) && !spinBLEClient.connectedPM && !rtConfig.watts.getSimulate() && (rtConfig.hr.getValue() > 0) &&
        userPWC.hr2Pwr) {
      calculateInstPwrFromHR();
      hr2p = true;
    } else {
      hr2p = false;
    }
#ifdef DEBUG_HR_TO_PWR
    calculateInstPwrFromHR();
#endif  // DEBUG_HR_TO_PWR

    if (!spinBLEClient.connectedPM && !hr2p && !rtConfig.watts.getSimulate() && !rtConfig.cad.getSimulate()) {
      rtConfig.cad.setValue(0);
      rtConfig.watts.setValue(0);
    }
    if (!spinBLEClient.connectedHRM && !rtConfig.hr.getSimulate()) {
      rtConfig.hr.setValue(0);
    }

    if (connectedClientCount() > 0) {
      // update the BLE information on the server
      updateIndoorBikeDataChar();
      updateCyclingPowerMeasurementChar();
      updateHeartRateMeasurementChar();
      // controlPointIndicate();

      if (spinDown()) {
        // Possibly do something in the future. Right now we just fake the spindown.
      }

      processFTMSWrite();
      spinBLEClient.postConnect();

      if (BLEDevice::getAdvertising()) {
        if (!(BLEDevice::getAdvertising()->isAdvertising()) && (BLEDevice::getServer()->getConnectedCount() < CONFIG_BT_NIMBLE_MAX_CONNECTIONS - NUM_BLE_DEVICES)) {
          SS2K_LOG(BLE_COMMON_LOG_TAG, "Starting Advertising From Communication Loop");
          BLEDevice::startAdvertising();
        }
      }
    }

    // blink if no client connected
    if (connectedClientCount() == 0) {
      if ((millis() / 500) % 2 == 0) {
        digitalWrite(LED_PIN, LOW);
      } else {
        digitalWrite(LED_PIN, HIGH);
      }
    } else {
      digitalWrite(LED_PIN, HIGH);
    }
    if (httpServer.isServing) {  // slow the BLE communications for faster web tasks.
      vTaskDelay((BLE_NOTIFY_DELAY) / portTICK_PERIOD_MS);
    }
    vTaskDelay((BLE_NOTIFY_DELAY) / portTICK_PERIOD_MS);
#ifdef DEBUG_STACK
    Serial.printf("BLEComm: %d \n", uxTaskGetStackHighWaterMark(BLECommunicationTask));
#endif  // DEBUG_STACK
  }
}