/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <NimBLEDevice.h>
#include "Zwift_Protocol_Messages.h"

// Zwift manufacturer ID
inline constexpr uint16_t kZwiftManufacturerId = 0x094A;

// Zwift device type identifiers (from manufacturer data)
enum class ZwiftDeviceType : uint8_t {
  BC1 = 0x09,
  ClickV2Right = 0x0A,
  ClickV2Left = 0x0B,
};

// Keepalive / riding data interval in milliseconds
inline constexpr unsigned long kZwiftKeepaliveIntervalMs = 5000;
inline constexpr unsigned long kZwiftSessionTimeoutMs = kZwiftKeepaliveIntervalMs * 2;
inline constexpr unsigned long kZwiftRidingDataIntervalMs = 250;
inline constexpr char kZwiftBleLogTag[] = "BLE_Zwift";
inline constexpr char kZwiftDirConLogTag[] = "DRC_Zwift";

class BLE_Zwift_Service {
 public:
  BLE_Zwift_Service();
  void setupService(NimBLEServer *pServer);
  void update();

  // Returns true if a Zwift client has been used recently.
  bool isConnected();

  // Returns the current virtual gear ratio x10000 (0 = no virtual shifting active)
  uint32_t getGearRatioX10000();

  // Send a shift up notification to Zwift (key down + key up)
  void sendShiftUp();

  // Send a shift down notification to Zwift (key down + key up)
  void sendShiftDown();

  // Handle incoming write to sync_rx (handshake and protocol commands)
  // isDirCon=true uses trainer protocol, isDirCon=false uses Click v2 controller protocol
  void handleSyncRxWrite(const std::string &value, bool isDirCon = false);

 private:
  NimBLEService *pZwiftService;
  NimBLECharacteristic *asyncCharacteristic;
  NimBLECharacteristic *syncRxCharacteristic;
  NimBLECharacteristic *syncTxCharacteristic;
  NimBLECharacteristic *unknownCharacteristic5;
  NimBLECharacteristic *unknownCharacteristic6;

  bool isDirCon;
  unsigned long _lastActivityTime;
  unsigned long _lastKeepaliveTime;
  unsigned long _lastRidingDataTime;
  uint32_t _gearRatioX10000;

  const char *getLogTag() const;
  bool keepAlive(unsigned long now);
  void resetSession();

  // Encode a button mask into a protobuf varint message and send as notification
  void sendButtonNotification(ZwiftProtocol::RideButtonMask buttonMask);

  // Encode uint32 as protobuf varint, returns number of bytes written
  static size_t encodeVarint32(uint32_t value, uint8_t *buffer);

  // Encode uint64 as ULEB128 varint, returns number of bytes written
  static size_t encodeUleb128(uint64_t value, uint8_t *buffer);

  // Decode ULEB128 varint from buffer, returns number of bytes consumed
  static size_t decodeUleb128(const uint8_t *buf, size_t bufLen, uint64_t *result);

  // Send the "all buttons released" state
  void sendAllButtonsReleased();

  // Handle Zwift trainer protocol command (non-RideOn messages)
  void handleZwiftCommand(const uint8_t *data, size_t length);

  // Apply received gear ratio to shifter position
  void applyGearRatio();

  friend class ZwiftSyncRxCallbacks;
};

// Callback class for the Zwift sync_rx characteristic writes
class ZwiftSyncRxCallbacks : public NimBLECharacteristicCallbacks {
 public:
  void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override;
  void onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue) override;
};

extern BLE_Zwift_Service zwiftService;
