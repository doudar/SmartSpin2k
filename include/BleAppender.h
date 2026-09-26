/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "LogAppender.h"
#include <string>

class BleAppender : public ILogAppender {
 public:
  void Log(const char *message);
  void Initialize();
  std::string getLastMessage();

 private:
  static const size_t MAX_MESSAGE_SIZE = 500;  // MTU-safe size
  // A synchronous handoff to the custom characteristic, not a replay queue.
  std::string pendingMessage;
};
