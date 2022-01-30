/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <ArduinoWebsockets.h>
#include "LogAppender.h"

using namespace websockets;

class WebSocketAppender : public ILogAppender {
 public:
  void Log(const char *message);
  void Loop();

 private:
  static const uint16_t port = 8080;

  void Initialize();

  WebsocketsServer _webSocketsServer;
  WebsocketsClient _client;
};
