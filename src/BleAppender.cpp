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
  appendMessage(message);

  // Use the existing custom characteristic notification mechanism
  // only notify if there are messages to send
  if (!messageQueue.empty()) {
    BLE_ss2kCustomCharacteristic::notify(BLE_BLELogging);
  }
}

std::string BleAppender::getLastMessage() {
  if (!messageQueue.empty()) {
    std::string msg = messageQueue.front();
    messageQueue.pop();
    return msg;
  }
  return "";
}

void BleAppender::appendMessage(const char *message) {
  if (message == nullptr) {
    return;
  }

  // Copy message and remove trailing newlines
  std::string msg(message);
  while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
    msg.pop_back();
  }

  if (msg.empty()) {
    return;
  }

  // Truncate message if it's too long on its own
  if (msg.length() > MAX_MESSAGE_SIZE) {
    msg = msg.substr(0, MAX_MESSAGE_SIZE);
  }

  messageQueue.push(msg);
}
