/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BleAppender.h"
#include "Main.h"
#include "BLE_Custom_Characteristic.h"
#include <utility>

void BleAppender::Initialize() { pendingMessage.clear(); }

void BleAppender::Log(const char *message) {
  pendingMessage.clear();
  if (!rtConfig->getBleLogEnabled() || message == nullptr) {
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
    msg.resize(MAX_MESSAGE_SIZE);
  }

  pendingMessage = std::move(msg);
  BLE_ss2kCustomCharacteristic::notify(BLE_BLELogging);
  // A settings snapshot can suppress notify before it consumes the message.
  // Drop that unsent log: replaying one old message per new arrival leaves a
  // permanent delay which grows with every interruption (including calibration).
  pendingMessage.clear();
}

std::string BleAppender::getLastMessage() { return std::exchange(pendingMessage, std::string()); }
