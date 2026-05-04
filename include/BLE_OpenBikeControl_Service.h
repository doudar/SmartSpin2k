/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <NimBLEDevice.h>

class BLE_OpenBikeControl_Service {
 public:
  BLE_OpenBikeControl_Service();

  void setupService(NimBLEServer *pServer);
  bool isConnected();
  void sendShiftUp();
  void sendShiftDown();

  void handleHapticWrite(const uint8_t *data, size_t length, bool isDirCon = false);
  void handleAppInfoWrite(const uint8_t *data, size_t length, bool isDirCon = false);
  void handleButtonStateSubscription(uint16_t subValue);

 private:
  NimBLEService *pOpenBikeControlService;
  NimBLECharacteristic *buttonStateCharacteristic;
  NimBLECharacteristic *hapticFeedbackCharacteristic;
  NimBLECharacteristic *appInformationCharacteristic;
  unsigned long _lastClientActivityMs;

  static void setupMDNS();
  static void addServiceUuidToMDNS(const NimBLEUUID& serviceUuid);

  void sendButtonState(uint8_t buttonId, uint8_t state);
  void markClientActivity();
};

class OpenBikeControlHapticCallbacks : public NimBLECharacteristicCallbacks {
 public:
  void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override;
};

class OpenBikeControlAppInfoCallbacks : public NimBLECharacteristicCallbacks {
 public:
  void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override;
};

class OpenBikeControlButtonStateCallbacks : public NimBLECharacteristicCallbacks {
 public:
  void onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue) override;
};

extern BLE_OpenBikeControl_Service openBikeControlService;
