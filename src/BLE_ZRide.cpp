/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BLE_ZRide.h"
#include "DirConManager.h"
#include <Constants.h>

// A simple callback class for handling writes to our characteristic
class ZwiftRideServerCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    zwiftRideService.processZwiftSyncWrite(pCharacteristic->getValue().data(), pCharacteristic->getValue().size());
  }
};

BLE_Zwift_Ride_Service::BLE_Zwift_Ride_Service()
    : pZwiftRideService(nullptr), zwiftRideAsyncCharacteristic(nullptr), zwiftRideSyncRxCharacteristic(nullptr), zwiftRideSyncTxCharacteristic(nullptr), lastShifterPosition(0) {}

void BLE_Zwift_Ride_Service::setupService(NimBLEServer* pServer) {
  // Create the Zwift Ride BLE Service
  pZwiftRideService = spinBLEServer.pServer->createService(ZWIFT_RIDE_SERVICE_UUID);

  // Create the Async characteristic for notifications (our button presses)
  zwiftRideAsyncCharacteristic = pZwiftRideService->createCharacteristic(ZWIFT_ASYNC_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::NOTIFY);

  // Create the Sync RX characteristic for client writes
  zwiftRideSyncRxCharacteristic = pZwiftRideService->createCharacteristic(ZWIFT_SYNC_RX_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  zwiftRideSyncRxCharacteristic->setCallbacks(new ZwiftRideServerCallbacks());

  // Create the Sync TX characteristic
  zwiftRideSyncTxCharacteristic = pZwiftRideService->createCharacteristic(ZWIFT_SYNC_TX_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::INDICATE | NIMBLE_PROPERTY::READ);
  zwiftRideSyncTxCharacteristic->setValue("RideOn");  // Initial value for the characteristic

  // Start the service
  pZwiftRideService->start();

  // Add the service UUID to the advertising data
  spinBLEServer.pServer->getAdvertising()->addServiceUUID(pZwiftRideService->getUUID());

  // Add service UUID to DirCon MDNS
  DirConManager::addBleServiceUuid(pZwiftRideService->getUUID());

  // Initialize last shifter position
  lastShifterPosition = rtConfig->getShifterPosition();
}

void BLE_Zwift_Ride_Service::processZwiftSyncWrite(const uint8_t* data, size_t length) {
  std::string value = std::string(reinterpret_cast<const char*>(data), length);
  if (value.length() > 0) {
    // The client sends a "RideOn" handshake message. We can log it here.
    if (true) {
      SS2K_LOG("ZWIFT_RIDE", "Received 'RideOn' handshake from client.");
      uint8_t* handshakeResponse = (uint8_t*)"RideOn";
      zwiftRideSyncTxCharacteristic->notify(handshakeResponse, strlen((const char*)handshakeResponse));  // Notify the client that we received the handshake
      DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_RIDE_SERVICE_UUID), zwiftRideSyncTxCharacteristic->getUUID(), handshakeResponse, strlen((const char*)handshakeResponse));
    } else {
      SS2K_LOG("ZWIFT_RIDE", "Received value: %s", value.c_str());
    }
  }
}

void BLE_Zwift_Ride_Service::update() {
  static bool buttonWasPressed = true;
  int currentShifterPosition   = rtConfig->getShifterPosition();

  if (buttonWasPressed) {
    uint8_t cleared_payload[7] = {0x23, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
    zwiftRideAsyncCharacteristic->setValue(cleared_payload, sizeof(cleared_payload));
    zwiftRideAsyncCharacteristic->notify();
    DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_RIDE_SERVICE_UUID), zwiftRideAsyncCharacteristic->getUUID(), cleared_payload, sizeof(cleared_payload));
    buttonWasPressed = false;  // Reset the button state
    return;                    // We need a delay before doing more.
  }

  if (currentShifterPosition != lastShifterPosition) {
    buttonWasPressed = true;  // A gear shift has occurred, we will send a notification
    // A gear shift has occurred. We will send a notification on the async characteristic.
    // The payload is a multi-byte array that represents the button state.
    // From the client code, pData[2], pData[3], and pData[4] are the relevant bytes.
    uint8_t payload[7] = {0x23, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F};  // Base payload for no buttons pressed

    if (currentShifterPosition > lastShifterPosition) {
      // Upshift: Let's use the right side upper button mapping from the client code (pData[3] == 0xDF)
      payload[3] = 0xDF;
    } else {
      // Downshift: Let's use the left side upper button mapping (pData[3] == 0xFD)
      payload[3] = 0xFD;
    }

    // Set the characteristic value and notify the client
    zwiftRideAsyncCharacteristic->setValue(payload, sizeof(payload));
    zwiftRideAsyncCharacteristic->notify();
    DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_RIDE_SERVICE_UUID), zwiftRideAsyncCharacteristic->getUUID(), payload, sizeof(payload));

    SS2K_LOG("ZWIFT_RIDE", "Shifter position changed to %d. Sent notification.", currentShifterPosition);

    // Update the last known position
    lastShifterPosition = currentShifterPosition;
  }
}