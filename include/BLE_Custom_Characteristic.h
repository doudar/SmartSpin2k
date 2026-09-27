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
  // Fast maintenance pass: bounded BLE write/status queue and snapshot timeout.
  void processPendingEvents();
  // Periodic server pass: pace DirCon settings snapshot chunks.
  void update();
  static void onConnect(uint16_t connHandle);
  static void onDisconnect(uint16_t connHandle);
  // Runs from maintenance/DirCon; BLE callbacks enqueue requests for processPendingEvents().
  static void process(const std::string& rxValue, uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE, uint16_t mtu = 23,
                      bool indicateResponse = true);
  // Custom Characteristic value that needs to be notified
  static void notify(char _item, int tableRow = -1);
  static void beginScanResults();
  static void notifyScanResult(const String& name, const NimBLEUUID& serviceUuid);
  static void endScanResults();
  // Notify any changed value in userConfig
  static void parseNemit();

 private:
  NimBLEService *pSmartSpin2kService = nullptr;
  NimBLECharacteristic *smartSpin2kCharacteristic = nullptr;
  uint8_t ss2kCustomCharacteristicValue[3] = {0x00, 0x00, 0x00};
};

extern BLE_ss2kCustomCharacteristic ss2kCustomCharacteristic;

class ss2kCustomCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
  void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override;
  void onStatus(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, int code) override;
};
