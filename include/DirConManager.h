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
#define DIRCON_MAX_CHARACTERISTICS   20   // maximum number of characteristics to track for subscriptions
#define DIRCON_MAX_SERVICES          10   // maximum number of services that can register with DirCon

// Result struct populated by service write handler callbacks
struct DirConWriteResult {
  bool updateResponseData;           // If true, response includes characteristic's current value
  NimBLEUUID autoSubscribeUuids[4];  // UUIDs to auto-subscribe the client to
  size_t autoSubscribeCount;         // Number of valid entries in autoSubscribeUuids

  DirConWriteResult() : updateResponseData(false), autoSubscribeCount(0) {}
};

// Subscription entry: stores UUID alongside active flag to avoid hash collisions
struct Subscription {
  NimBLEUUID uuid;
  bool active = false;
};

// Write handler callback: returns true if the characteristic was handled by this service
typedef bool (*DirConWriteHandler)(NimBLECharacteristic* characteristic, const uint8_t* data, size_t length, DirConWriteResult* result);

class DirConManager {
 public:
  static bool start();
  static void stop();
  static void update();

  // Register a BLE service with DirCon for discovery and optional write handling.
  // Services call this during setup. Write handler may be nullptr for read-only/notify-only services.
  static void registerService(const NimBLEUUID& serviceUuid, DirConWriteHandler writeHandler = nullptr);

  // Notify DirCon clients about BLE characteristic changes
  static void notifyCharacteristic(const NimBLEUUID& serviceUuid, const NimBLEUUID& characteristicUuid, uint8_t* data, size_t length, bool onlySubscribers = true);

 private:
  // Service registration
  struct ServiceRegistration {
    NimBLEUUID serviceUuid;
    DirConWriteHandler writeHandler;
  };
  static ServiceRegistration registeredServices[DIRCON_MAX_SERVICES];
  static size_t registeredServiceCount;

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
  static void broadcastNotification(const NimBLEUUID& characteristicUuid, uint8_t* data, size_t length, bool onlySubscribers = true);

  // Service and characteristic handling
  static void addBleServiceUuid(const NimBLEUUID& serviceUuid);
  static std::vector<NimBLECharacteristic*> getCharacteristics(const NimBLEUUID& serviceUuid);
  static uint8_t getDirConProperties(uint32_t characteristicProperties);
  static NimBLECharacteristic* findCharacteristic(const NimBLEUUID& characteristicUuid);

  // Subscription tracking
  static Subscription clientSubscriptions[DIRCON_MAX_CLIENTS][DIRCON_MAX_CHARACTERISTICS];
  static void addSubscription(size_t clientIndex, const NimBLEUUID& characteristicUuid);
  static void removeSubscription(size_t clientIndex, const NimBLEUUID& characteristicUuid);
  static void removeAllSubscriptions(size_t clientIndex);
  static bool hasSubscription(size_t clientIndex, const NimBLEUUID& characteristicUuid);

  // Sequence number tracking
  static uint8_t lastSequenceNumber[DIRCON_MAX_CLIENTS];
};

#endif  // DIRCONMANAGER_H