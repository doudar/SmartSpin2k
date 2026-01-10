/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BleAppender.h"
#include "Main.h"
#include "Constants.h"
#include "BLE_Custom_Characteristic.h"
#include <NimBLEDevice.h>
#include <cstring>

void BleAppender::Initialize() {}

void BleAppender::Log(const char *message) {
  if (!rtConfig->getBleLogEnabled()) {
    return;
  }

  // Cache the message
  trimMessage(message);

  // Get the BLE characteristic to notify subscribers
  if (NimBLEDevice::getServer() == nullptr) {
    return;
  }

  NimBLEService *pService = NimBLEDevice::getServer()->getServiceByUUID(SMARTSPIN2K_SERVICE_UUID);
  if (pService == nullptr) {
    return;
  }

  NimBLECharacteristic *pCharacteristic = pService->getCharacteristic(SMARTSPIN2K_CHARACTERISTIC_UUID);
  if (pCharacteristic == nullptr) {
    return;
  }

  // Only notify if there are subscribed clients
  if (pCharacteristic->getSubscribedCount() == 0) {
    return;
  }

  // Prepare notification with status byte and code prefix
  // Use fixed-size buffer to avoid stack overflow from VLA
  size_t messageLen = lastMessage.length();
  const size_t bufferSize = MAX_MESSAGE_SIZE + 2;  // +2 for status byte and code
  uint8_t returnChar[bufferSize];
  
  if (messageLen > MAX_MESSAGE_SIZE) {
    messageLen = MAX_MESSAGE_SIZE;  // Safety check
  }
  
  returnChar[0] = cc_success;
  returnChar[1] = BLE_BLELogging;
  memcpy(&returnChar[2], lastMessage.c_str(), messageLen);

  pCharacteristic->setValue(returnChar, messageLen + 2);
  pCharacteristic->notify();
}

const char *BleAppender::getLastMessage() {
  return lastMessage.c_str();
}

void BleAppender::trimMessage(const char *message) {
  if (message == nullptr) {
    lastMessage = "";
    return;
  }

  // Copy message and remove trailing newlines
  std::string msg(message);
  while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
    msg.pop_back();
  }

  // Trim to MTU-safe size
  if (msg.length() > MAX_MESSAGE_SIZE) {
    msg = msg.substr(0, MAX_MESSAGE_SIZE);
  }

  lastMessage = msg;
}
