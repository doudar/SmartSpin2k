/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "SS2KLog.h"
#include "BLE_Common.h"

#include <math.h>
#include <sensors/SensorData.h>
#include <sensors/SensorDataFactory.h>

bool hr2p = false;

TaskHandle_t BLECommunicationTask;

void BLECommunications(void *pvParameters) {
  for (;;) {
    // **********************************Client***************************************
    for (size_t x = 0; x < NUM_BLE_DEVICES; x++) {  // loop through discovered devices
      if (spinBLEClient.myBLEDevices[x].connectedClientID != BLE_HS_CONN_HANDLE_NONE) {
        SS2K_LOGD(BLE_COMMON_LOG_TAG, "Address: (%s) Client ID: (%d) SerUUID: (%s) CharUUID: (%s) HRM: (%s) PM: (%s) CSC: (%s) CT: (%s) doConnect: (%s)",
                  spinBLEClient.myBLEDevices[x].peerAddress.toString().c_str(), spinBLEClient.myBLEDevices[x].connectedClientID,
                  spinBLEClient.myBLEDevices[x].serviceUUID.toString().c_str(), spinBLEClient.myBLEDevices[x].charUUID.toString().c_str(),
                  spinBLEClient.myBLEDevices[x].userSelectedHR ? "true" : "false", spinBLEClient.myBLEDevices[x].userSelectedPM ? "true" : "false",
                  spinBLEClient.myBLEDevices[x].userSelectedCSC ? "true" : "false", spinBLEClient.myBLEDevices[x].userSelectedCT ? "true" : "false",
                  spinBLEClient.myBLEDevices[x].doConnect ? "true" : "false");
        if (spinBLEClient.myBLEDevices[x].advertisedDevice) {  // is device registered?
          SpinBLEAdvertisedDevice myAdvertisedDevice = spinBLEClient.myBLEDevices[x];
          if ((myAdvertisedDevice.connectedClientID != BLE_HS_CONN_HANDLE_NONE) && (myAdvertisedDevice.doConnect == false)) {  // client must not be in connection process
            if (BLEDevice::getClientByPeerAddress(myAdvertisedDevice.peerAddress)) {                                           // nullptr check
              BLEClient *pClient = NimBLEDevice::getClientByPeerAddress(myAdvertisedDevice.peerAddress);
              // Client connected with a valid UUID registered
              if ((myAdvertisedDevice.serviceUUID != BLEUUID((uint16_t)0x0000)) && (pClient->isConnected())) {
                BLERemoteCharacteristic *pRemoteBLECharacteristic = pClient->getService(myAdvertisedDevice.serviceUUID)->getCharacteristic(myAdvertisedDevice.charUUID);
                // std::string data                                  = pRemoteBLECharacteristic->getValue();
                // uint8_t *pData                                    = reinterpret_cast<uint8_t *>(&data[0]);
                // int length                                        = data.length();
                // 250 == Data(60), Spaces(Data/2), Arrow(4), SvrUUID(37), Sep(3), ChrUUID(37), Sep(3),
                //        Name(10), Prefix(2), HR(8), SEP(1), CD(10), SEP(1), PW(8), SEP(1), SP(7), Suffix(2), Nul(1) - 225 rounded up

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
                  collectAndSet(pRemoteBLECharacteristic->getUUID(), myAdvertisedDevice.serviceUUID, pRemoteBLECharacteristic->getRemoteService()->getClient()->getPeerAddress(), pData, length);

                }
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
    if ((spinBLEClient.connectedHR || rtConfig.hr.getSimulate()) && !spinBLEClient.connectedPM && !rtConfig.watts.getSimulate() && (rtConfig.hr.getValue() > 0) &&
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
    if (!spinBLEClient.connectedHR && !rtConfig.hr.getSimulate()) {
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
    if (spinBLEClient.doScan && (spinBLEClient.scanRetries > 0) && !NimBLEDevice::getScan()->isScanning()) {
      spinBLEClient.scanRetries--;
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Initiating Scan from Client Task:");
      spinBLEClient.scanProcess();
    }
    vTaskDelay((BLE_NOTIFY_DELAY) / portTICK_PERIOD_MS);
#ifdef DEBUG_STACK
    Serial.printf("BLEComm: %d \n", uxTaskGetStackHighWaterMark(BLECommunicationTask));
#endif  // DEBUG_STACK
  }
}