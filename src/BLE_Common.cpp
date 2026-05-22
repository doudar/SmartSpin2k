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

#include <Arduino.h>
#include <math.h>
#include <sensors/SensorData.h>
#include <sensors/SensorDataFactory.h>
#include <NimBLEDevice.h>

static uint32_t ledAllowedUntil = 15UL * 60UL * 1000UL;

void setLEDAllowedUntil(uint32_t t) { ledAllowedUntil = t; }
void extendLEDWindow() { ledAllowedUntil = millis() + 15UL * 60UL * 1000UL; }

/**
 * @brief Retrieves the BLE service information for a given advertised device and device name.
 *
 * This function checks if the advertised device is advertising any of the supported BLE services.
 * For the Flywheel UART service, it additionally verifies that the device name matches the expected Flywheel BLE name.
 * If a matching service is found, a pointer to the corresponding BLEServiceInfo is returned.
 *
 * @param advertisedDevice Pointer to the NimBLEAdvertisedDevice representing the BLE device being checked.
 * @param deviceName The name of the BLE device as a String.
 * @return Pointer to the matching BLEServiceInfo if found; otherwise, nullptr.
 */
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

/**
 * @brief Checks if a BLE device is supported based on its advertised information and name.
 *
 * Determines whether the specified BLE device is supported by attempting to retrieve its service information.
 *
 * @param advertisedDevice Pointer to the advertised BLE device to check.
 * @param deviceName The name of the device to match against.
 * @return true if the device is supported; false otherwise.
 */
bool isDeviceSupported(const NimBLEAdvertisedDevice* advertisedDevice, const String& deviceName) { return getDeviceServiceInfo(advertisedDevice, deviceName) != nullptr; }

void BLECommunications() {
  // **********************************Client***************************************
  for (auto& _BLEd : spinBLEClient.myBLEDevices) {  // loop through discovered devices
    if (_BLEd.connectedClientID != BLE_HS_CONN_HANDLE_NONE) {
      if (_BLEd.advertisedDevice) {                                                                // is device registered?
        if ((_BLEd.connectedClientID != BLE_HS_CONN_HANDLE_NONE) && (_BLEd.doConnect == false)) {  // client must not be in connection process
          if (BLEDevice::getClientByHandle(_BLEd.connectedClientID)) {                             // nullptr check
            BLEClient* pClient = NimBLEDevice::getClientByHandle(_BLEd.connectedClientID);
            // Client connected with a valid UUID registered
            if ((_BLEd.serviceUUID != BLEUUID((uint16_t)0x0000)) && (pClient->isConnected())) {
              // Check for data timeout and trigger disconnect if needed
              if (_BLEd.lastDataUpdateTime > 0) {  // Only check if we've received data before
                unsigned long timeSinceLastData = millis() - _BLEd.lastDataUpdateTime;
                if (timeSinceLastData > BLE_CLIENT_DISCONNECT_TIMEOUT) {
                  SS2K_LOG(BLE_COMMON_LOG_TAG, "%s data timeout (%lu ms), triggering disconnect", _BLEd.uniqueName.c_str(), timeSinceLastData);
                  pClient->disconnect();
                  continue;                                   // Skip further processing for this device
                }
              }
              // Handle BLE HID Remotes
              if (_BLEd.serviceUUID == HID_SERVICE_UUID) {
                spinBLEClient.keepAliveBLE_HID(pClient);  // keep alive doesn't seem to help
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
                collectAndSet(incomingNotifyData.charUUID, incomingNotifyData.serviceUUID, _BLEd.uniqueName, pData, length);
              }
            } else if (!pClient->isConnected()) {  // This is a workaround for a bug in NimBLE where onDisconnect() is not called automatically.
              MyClientCallback workaroundCallback;
              workaroundCallback.onDisconnect(pClient, 0);
              SS2K_LOG(BLE_COMMON_LOG_TAG, "%s not connected in communications loop", _BLEd.uniqueName.c_str());
            }
          }
        }
      }
    }
  }

  // ***********************************SERVER**************************************
#ifdef DEBUG_HR_TO_PWR
  calculateInstPwrFromHR();
#endif  // DEBUG_HR_TO_PWR

  // Set outputs to zero if we're not simulating or have connected devices.
  if (!spinBLEClient.connectedPM && !rtConfig->watts.getSimulate() && !rtConfig->cad.getSimulate() && !userConfig->getPTab4Pwr()) {
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
      if ((!BLEDevice::getAdvertising()->isAdvertising()) && (spinBLEServer.connectedClientCount() < (CONFIG_BT_NIMBLE_MAX_CONNECTIONS - NUM_BLE_DEVICES))) {
        SS2K_LOG(BLE_COMMON_LOG_TAG, "Starting Advertising From Communication Loop. Connected Clients: %d", spinBLEServer.connectedClientCount());
        auto clients = BLEDevice::getConnectedClients();
        // loop through clients and log their addresses
        for (const auto& client : clients) {
          SS2K_LOG(BLE_COMMON_LOG_TAG, "Connected Client: %s", client->getPeerAddress().toString().c_str());
        }
        BLEDevice::startAdvertising();
      }
    }
  }

  static int _lastClientCount = -1;
  int currentCount            = spinBLEServer.connectedClientCount();
  if (currentCount != _lastClientCount) {
    _lastClientCount = currentCount;
    extendLEDWindow();
  }

  if (millis() < ledAllowedUntil) {
    if (currentCount == 0) {
      digitalWrite(LED_PIN, (millis() / 500) % 2 == 0 ? LOW : HIGH);
    } else {
      digitalWrite(LED_PIN, HIGH);
    }
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}
