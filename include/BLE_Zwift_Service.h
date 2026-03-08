/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <NimBLEDevice.h>

// Zwift manufacturer ID
#define ZWIFT_MANUFACTURER_ID 0x094A

// Zwift device type identifiers (from manufacturer data)
#define ZWIFT_BC1              0x09  // Zwift Click v1
#define ZWIFT_CLICK_V2_RIGHT   0x0A  // Zwift Click v2 Right
#define ZWIFT_CLICK_V2_LEFT    0x0B  // Zwift Click v2 Left

// Zwift protocol constants
#define ZWIFT_CONTROLLER_NOTIFICATION_OPCODE 0x23  // Zwift Ride key status message
#define ZWIFT_EMPTY_MESSAGE_TYPE             0x15
#define ZWIFT_BATTERY_LEVEL_TYPE             0x19

// Zwift trainer protocol message opcodes
#define ZWIFT_TRAINER_RIDING_DATA   0x03
#define ZWIFT_TRAINER_CONTROL       0x04

// Zwift Ride button masks (inverted logic: 0 = pressed)
// Bit layout per Zwift Ride protocol (note: bit 7 and bit 15 are unused gaps)
#define ZWIFT_BTN_LEFT       0x00001
#define ZWIFT_BTN_UP         0x00002
#define ZWIFT_BTN_RIGHT      0x00004
#define ZWIFT_BTN_DOWN       0x00008
#define ZWIFT_BTN_A          0x00010
#define ZWIFT_BTN_B          0x00020
#define ZWIFT_BTN_Y          0x00040
#define ZWIFT_BTN_Z          0x00100
#define ZWIFT_BTN_SHFT_UP_L  0x00200
#define ZWIFT_BTN_SHFT_DN_L  0x00400
#define ZWIFT_BTN_POWERUP_L  0x00800
#define ZWIFT_BTN_ONOFF_L    0x01000
#define ZWIFT_BTN_SHFT_UP_R  0x02000
#define ZWIFT_BTN_SHFT_DN_R  0x04000
#define ZWIFT_BTN_POWERUP_R  0x10000
#define ZWIFT_BTN_ONOFF_R    0x20000

// Keepalive / riding data interval in milliseconds
#define ZWIFT_KEEPALIVE_INTERVAL_MS 5000
#define ZWIFT_RIDING_DATA_INTERVAL_MS 250

#define ZWIFT_LOG_TAG "BLE_Zwift"

class BLE_Zwift_Service {
 public:
  BLE_Zwift_Service();
  void setupService(NimBLEServer *pServer);
  void update();

  // Returns true if a Zwift client is connected and has completed handshake
  bool isConnected();

  // Returns the current virtual gear ratio x10000 (0 = no virtual shifting active)
  uint32_t getGearRatioX10000();

  // Called when a BLE client disconnects to reset handshake state
  void onClientDisconnect();

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

  volatile bool _handshakeComplete;
  unsigned long _lastKeepaliveTime;
  unsigned long _lastRidingDataTime;
  volatile uint32_t _gearRatioX10000;

  // Encode a button mask into a protobuf varint message and send as notification
  void sendButtonNotification(uint32_t buttonMask);

  // Encode uint32 as protobuf varint, returns number of bytes written
  static size_t encodeVarint32(uint32_t value, uint8_t *buffer);

  // Encode uint64 as ULEB128 varint, returns number of bytes written
  static size_t encodeUleb128(uint64_t value, uint8_t *buffer);

  // Decode ULEB128 varint from buffer, returns number of bytes consumed
  static size_t decodeUleb128(const uint8_t *buf, size_t bufLen, uint64_t *result);

  // Send the "all buttons released" state
  void sendAllButtonsReleased();

  // Send keepalive on sync_tx
  void sendKeepalive();

  // Send riding data notification (trainer protocol message 0x03)
  void sendRidingData();

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
