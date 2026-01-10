/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BleAppender.h"
#include "Main.h"
#include "BLE_Custom_Characteristic.h"

void BleAppender::Initialize() {}

void BleAppender::Log(const char *message) {
  if (!rtConfig->getBleLogEnabled()) {
    return;
  }

  // Cache the message
  trimMessage(message);

  // Use the existing custom characteristic notification mechanism
  BLE_ss2kCustomCharacteristic::notify(BLE_BLELogging);
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
