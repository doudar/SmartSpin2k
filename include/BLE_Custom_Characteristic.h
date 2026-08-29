/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <NimBLEDevice.h>
#include "BLE_Common.h"
#include "CustomCharacteristicProtocol.h"

class BLE_ss2kCustomCharacteristic {
 public:
  void setupService(NimBLEServer *pServer);
  void update();
  // Used internally for notify and onWrite Callback.
  static void process(std::string rxValue, uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE, uint16_t mtu = 23, bool indicateResponse = true);
  // Custom Characteristic value that needs to be notified
  static void notify(char _item, int tableRow = -1);
  // Notify any changed value in userConfig
  static void parseNemit();

 private:
  NimBLEService *pSmartSpin2kService;
  NimBLECharacteristic *smartSpin2kCharacteristic;
  uint8_t ss2kCustomCharacteristicValue[3] = {0x00, 0x00, 0x00};
};

class ss2kCustomCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
  void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override;
  void onStatus(NimBLECharacteristic* pCharacteristic, int code) override;
};
