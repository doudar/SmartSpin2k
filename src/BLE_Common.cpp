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

const BLEServiceInfo* getDeviceServiceInfo(const NimBLEAdvertisedDevice* advertisedDevice, const String& deviceName) {
  if (!advertisedDevice->haveServiceUUID()) {
    return nullptr;
  }

  for (const auto& service : SUPPORTED_SERVICES) {
    // Special case for Flywheel which requires name check
    if (service.serviceUUID == FLYWHEEL_UART_SERVICE_UUID) {
      if (advertisedDevice->isAdvertisingService(service.serviceUUID) && deviceName == String(FLYWHEEL_BLE_NAME)) {
        return &service;
      }
    }
    // For all other services
    else if (advertisedDevice->isAdvertisingService(service.serviceUUID)) {
      return &service;
    }
  }

  return nullptr;
}

bool isDeviceSupported(const NimBLEAdvertisedDevice* advertisedDevice, const String& deviceName) { return getDeviceServiceInfo(advertisedDevice, deviceName) != nullptr; }

void BLECommunications() {
    // **********************************Client***************************************
    for (auto& _BLEd : spinBLEClient.myBLEDevices) {  // loop through discovered devices
      if (_BLEd.connectedClientID != BLE_HS_CONN_HANDLE_NONE) {
        if (_BLEd.advertisedDevice) {                                                                // is device registered?
          if ((_BLEd.connectedClientID != BLE_HS_CONN_HANDLE_NONE) && (_BLEd.doConnect == false)) {  // client must not be in connection process
            if (BLEDevice::getClientByPeerAddress(_BLEd.peerAddress)) {                              // nullptr check
              BLEClient* pClient = NimBLEDevice::getClientByPeerAddress(_BLEd.peerAddress);
              // Client connected with a valid UUID registered
              if ((_BLEd.serviceUUID != BLEUUID((uint16_t)0x0000)) && (pClient->isConnected())) {
                // Handle BLE HID Remotes
                if (_BLEd.serviceUUID == HID_SERVICE_UUID) {
                  spinBLEClient.keepAliveBLE_HID(pClient);  // keep alive doesn't seem to help :(
                  continue;                                 // There is not data that needs to be dequeued for the remote, so got to the next device.
                }
                // Dequeue sensor data we stored during notifications
                while (pdTRUE) {
                  NotifyData incomingNotifyData = _BLEd.dequeueData();
                  if (incomingNotifyData.length == 0) {
                    break;
                  }
                  size_t length = incomingNotifyData.length;
                  uint8_t pData[length];

                  for (size_t i = 0; i < length; i++) {
                    pData[i] = incomingNotifyData.data[i];
                  }
                  collectAndSet(incomingNotifyData.charUUID, incomingNotifyData.serviceUUID, _BLEd.peerAddress, pData, length);
                }
                if (_BLEd.getPostConnected()) {
                  spinBLEClient.handleBattInfo(pClient, false);
                }

              } else if (!pClient->isConnected()) {  // This is a workaround for a bug in NimBLE where onDisconnect() is not called automatically.
                MyClientCallback workaroundCallback;
                workaroundCallback.onDisconnect(pClient, 0);
                SS2K_LOG(BLE_COMMON_LOG_TAG, "Client %s not connected in communications loop", _BLEd.peerAddress.toString().c_str());
              }
            }
          }
        }
      }
    }

    // ***********************************SERVER**************************************
    if ((spinBLEClient.connectedHRM || rtConfig->hr.getSimulate()) && !spinBLEClient.connectedPM && !rtConfig->watts.getSimulate() && (rtConfig->hr.getValue() > 0) &&
        userPWC->hr2Pwr) {
      calculateInstPwrFromHR();
      hr2p = true;
    } else {
      hr2p = false;
    }
#ifdef DEBUG_HR_TO_PWR
    calculateInstPwrFromHR();
#endif  // DEBUG_HR_TO_PWR

    // Set outputs to zero if we're not simulating or have connected devices.
    if (!spinBLEClient.connectedPM && !hr2p && !rtConfig->watts.getSimulate() && !rtConfig->cad.getSimulate() && !userConfig->getPTab4Pwr()) {
      rtConfig->cad.setValue(0);
      rtConfig->watts.setValue(0);
    }
    if (!spinBLEClient.connectedHRM && !rtConfig->hr.getSimulate()) {
      rtConfig->hr.setValue(0);
    }

    if (!ss2k->isUpdating) {
      spinBLEServer.update();

#ifdef INTERNAL_ERG_4EXT_FTMS
      uint8_t test[] = {FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters, 0x00, 0x00, 0x00, 0x00, 0x28, 0x33};
      spinBLEClient.FTMSControlPointWrite(test, 7);
#endif

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
}
