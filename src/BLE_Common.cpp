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

int bleConnDesc               = 1;
bool updateConnParametersFlag = false;
bool hr2p                     = false;
TaskHandle_t BLECommunicationTask;
SensorDataFactory sensorDataFactory;

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

                  const int kLogBufMaxLength = 250;
                  char logBuf[kLogBufMaxLength];
                  SS2K_LOGD(BLE_COMMON_LOG_TAG, "Data length: %d", length);
                  int logBufLength = ss2k_log_hex_to_buffer(pData, length, logBuf, 0, kLogBufMaxLength);

                  logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, "<- %.8s | %.8s", myAdvertisedDevice.serviceUUID.toString().c_str(),
                                           myAdvertisedDevice.charUUID.toString().c_str());

                  std::shared_ptr<SensorData> sensorData = sensorDataFactory.getSensorData(
                      pRemoteBLECharacteristic->getUUID(), (uint64_t)pRemoteBLECharacteristic->getRemoteService()->getClient()->getPeerAddress(), pData, length);

                  logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " | %s[", sensorData->getId().c_str());
                  if (sensorData->hasHeartRate() && !rtConfig.getSimulateHr()) {
                    int heartRate = sensorData->getHeartRate();
                    rtConfig.setSimulatedHr(heartRate);
                    spinBLEClient.connectedHR |= true;
                    logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " HR(%d)", heartRate % 1000);
                  }
                  if (sensorData->hasCadence() && !rtConfig.getSimulateCad()) {
                    float cadence = sensorData->getCadence();
                    rtConfig.setSimulatedCad(cadence);
                    spinBLEClient.connectedCD |= true;
                    logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " CD(%.2f)", fmodf(cadence, 1000.0));
                  }
                  if (sensorData->hasPower() && !rtConfig.getSimulateWatts()) {
                    int power = sensorData->getPower() * userConfig.getPowerCorrectionFactor();
                    rtConfig.setSimulatedWatts(power);
                    spinBLEClient.connectedPM |= true;
                    logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " PW(%d)", power % 10000);
                  }
                  if (sensorData->hasSpeed()) {
                    float speed = sensorData->getSpeed();
                    rtConfig.setSimulatedSpeed(speed);
                    logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " SD(%.2f)", fmodf(speed, 1000.0));
                  }
                  strncat(logBuf + logBufLength, " ]", kLogBufMaxLength - logBufLength);
                  SS2K_LOG(BLE_COMMON_LOG_TAG, "%s", logBuf);
                  SEND_TO_TELEGRAM(String(logBuf));
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
    if ((spinBLEClient.connectedHR || rtConfig.getSimulateHr()) && !spinBLEClient.connectedPM && !rtConfig.getSimulateWatts() && (rtConfig.getSimulatedHr() > 0) &&
        userPWC.hr2Pwr) {
      calculateInstPwrFromHR();
      hr2p = true;
    } else {
      hr2p = false;
    }
#ifdef DEBUG_HR_TO_PWR
    calculateInstPwrFromHR();
#endif  // DEBUG_HR_TO_PWR

    if (!spinBLEClient.connectedPM && !hr2p && !rtConfig.getSimulateWatts() && !rtConfig.getSimulateCad()) {
      rtConfig.setSimulatedCad(0);
      rtConfig.setSimulatedWatts(0);
    }
    if (!spinBLEClient.connectedHR && !rtConfig.getSimulateHr()) {
      rtConfig.setSimulatedHr(0);
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
      // computeERG();

      if (updateConnParametersFlag) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        BLEDevice::getServer()->updateConnParams(bleConnDesc, 20, 100, 0, 2000);
        updateConnParametersFlag = false;
      }
    }

    if (BLEDevice::getAdvertising()) {
      if (!(BLEDevice::getAdvertising()->isAdvertising()) && (BLEDevice::getServer()->getConnectedCount() < CONFIG_BT_NIMBLE_MAX_CONNECTIONS - NUM_BLE_DEVICES)) {
        SS2K_LOG(BLE_COMMON_LOG_TAG, "Starting Advertising From Communication Loop");
        BLEDevice::startAdvertising();
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

    vTaskDelay((BLE_NOTIFY_DELAY) / portTICK_PERIOD_MS);
#ifdef DEBUG_STACK
    Serial.printf("BLEComm: %d \n", uxTaskGetStackHighWaterMark(BLECommunicationTask));
#endif  // DEBUG_STACK
  }
}
