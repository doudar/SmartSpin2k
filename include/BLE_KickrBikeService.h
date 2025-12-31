/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <NimBLEDevice.h>
#include "BLE_Common.h"

// Forward declaration for custom callback
class KickrBikeCharacteristicCallbacks;

// Gear system configuration for virtual shifting
#define KICKR_BIKE_NUM_GEARS 24
#define KICKR_BIKE_DEFAULT_GEAR 11  // Middle gear (0-indexed, so gear 12 in 1-indexed)

class BLE_KickrBikeService {
 public:
  BLE_KickrBikeService();
  void setupService(NimBLEServer *pServer, MyCharacteristicCallbacks *chrCallbacks);
  void update();
  
  // Gear management
  void shiftUp();
  void shiftDown();
  double getCurrentGearRatio() const;
  
  // Function to check shifter position and modify incline accordingly
  void updateGearFromShifterPosition();
  
  // RideOn handshake handling
  void processWrite(const std::string& value);
  void sendRideOnResponse();
  void sendKeepAlive();
  void sendRideData();
  void sendButtonPress(uint8_t buttonId);
  
  // Wahoo gearing service notifications
  void sendGearingNotification();
  
  // Opcode message handlers
  void handleGetRequest(const uint8_t* data, size_t length);
  void handleSetRequest(const uint8_t* data, size_t length);
  void handleInfoRequest(const uint8_t* data, size_t length);
  void handleReset();
  void handleSetLogLevel(const uint8_t* data, size_t length);
  void handleVendorMessage(const uint8_t* data, size_t length);
  void sendGetResponse(uint16_t objectId, const uint8_t* data, size_t length);
  void sendStatusResponse(uint8_t status);
  
  // Gradient/resistance control (independent of FTMS)
  void applyGradientToTrainer(float gradient);
  void applyGearChange(bool fromZwift = false);
  
  // Power control for ERG mode
  void setTargetPower(int watts);
  int getTargetPower() const { return targetPower; }
  
  // Enable/disable the service
  void enable() { isEnabled = true; }
  void disable() { isEnabled = false; }
  bool isServiceEnabled() const { return isEnabled; }
  
 private:
  BLEService *pKickrBikeService;
  BLECharacteristic *syncRxCharacteristic;   // Write characteristic for commands
  BLECharacteristic *asyncTxCharacteristic;  // Notify characteristic for events
  BLECharacteristic *syncTxCharacteristic;   // Notify characteristic for responses
  BLECharacteristic *debugCharacteristic; // Optional debug characteristic
  BLECharacteristic *unknown6Characteristic; // Optional unknown characteristic
  
  // Wahoo gearing service (separate from KICKR BIKE protocol)
  BLEService *pGearingService;
  BLECharacteristic *gearingCharacteristic;  // Notify characteristic for gear display
  
  // Gear system state
  int lastShifterPosition;
  
  // Gradient and resistance state (independent of FTMS)
  int targetPower;            // Target power for ERG mode (watts)
  
  // Service state
  bool isHandshakeComplete;
  bool isEnabled;  // Whether this service should control the trainer
  unsigned long lastKeepAliveTime;
  unsigned long lastGradientUpdateTime;
  unsigned long lastRideDataTime;
  unsigned long lastGearingUpdateTime;
  
  // Gear ratio table (24 gears)
  static const double gearRatios[KICKR_BIKE_NUM_GEARS];
  
  // Helper methods
  double calculateEffectiveGrade(double baseGrade, double gearRatio);
  bool isRideOnMessage(const std::string& data);
};

// Custom callback class for KickrBike Sync RX characteristic
class KickrBikeCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
};

extern BLE_KickrBikeService kickrBikeService;
