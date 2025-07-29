/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef DIRCONMANAGER_H
#define DIRCONMANAGER_H

#include "Main.h"
#include "BLE_Common.h"
#include "DirConMessage.h"
#include <WiFi.h>
#include <ESPmDNS.h>

// DirCon protocol definitions
#define DIRCON_MDNS_SERVICE_NAME     "_wahoo-fitness-tnp"
#define DIRCON_MDNS_SERVICE_PROTOCOL "tcp"
#define DIRCON_TCP_PORT              8081
#define DIRCON_MAX_CLIENTS           1
#define DIRCON_RECEIVE_BUFFER_SIZE   256
#define DIRCON_SEND_BUFFER_SIZE      256
#define DIRCON_MAX_CHARACTERISTICS   10   // maximum number of characteristics to track for subscriptions

class DirConManager {
 public:
  static bool start();
  static void stop();
  static void update();

  // Add a BLE service UUID to DirCon MDNS service
  static void addBleServiceUuid(const NimBLEUUID& serviceUuid);

  // Notify DirCon clients about BLE characteristic changes
  static void notifyCharacteristic(const NimBLEUUID& serviceUuid, const NimBLEUUID& characteristicUuid, uint8_t* data, size_t length);

 private:
  // Core functionality
  static bool started;
  static String statusMessage;
  static WiFiClient dirConClients[DIRCON_MAX_CLIENTS];
  static WiFiServer* tcpServer;
  static void setupMDNS();
  static void updateStatusMessage();
  static int connectedClients();

  // TCP connection handling
  static void checkForNewClients();
  static void handleClientData();
  static uint8_t receiveBuffer[DIRCON_MAX_CLIENTS][DIRCON_RECEIVE_BUFFER_SIZE];
  static size_t receiveBufferLength[DIRCON_MAX_CLIENTS];
  static uint8_t sendBuffer[DIRCON_SEND_BUFFER_SIZE];

  // Message handling
  static bool processDirConMessage(DirConMessage* message, size_t clientIndex);
  static void sendErrorResponse(uint8_t messageId, uint8_t sequenceNumber, uint8_t errorCode, size_t clientIndex);
  static void sendResponse(DirConMessage* message, size_t clientIndex);
  static void broadcastNotification(const NimBLEUUID& characteristicUuid, uint8_t* data, size_t length);

  // Service and characteristic handling
  static std::vector<NimBLEUUID> getAvailableServices();
  static std::vector<NimBLECharacteristic*> getCharacteristics(const NimBLEUUID& serviceUuid);
  static uint8_t getDirConProperties(uint32_t characteristicProperties);
  static NimBLECharacteristic* findCharacteristic(const NimBLEUUID& characteristicUuid);

  // Subscription tracking
  static bool clientSubscriptions[DIRCON_MAX_CLIENTS][DIRCON_MAX_CHARACTERISTICS];  // Simple subscription tracking
  static size_t charSubscriptionIndex(const NimBLEUUID& characteristicUuid);
  static void addSubscription(size_t clientIndex, const NimBLEUUID& characteristicUuid);
  static void removeSubscription(size_t clientIndex, const NimBLEUUID& characteristicUuid);
  static void removeAllSubscriptions(size_t clientIndex);
  static bool hasSubscription(size_t clientIndex, const NimBLEUUID& characteristicUuid);

  // Sequence number tracking
  static uint8_t lastSequenceNumber[DIRCON_MAX_CLIENTS];
};

#endif  // DIRCONMANAGER_H