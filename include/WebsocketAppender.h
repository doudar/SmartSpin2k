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
  WebSocketAppender();
  void Log(const char* message);
  void Loop();

 private:
  static const uint16_t port      = 8080;
  static const uint8_t maxClients = 4;

  void Initialize() override;
  uint8_t GetClientsCount();
  void AddClient(WebsocketsClient* client);

  WebsocketsServer _webSocketsServer;
  WebsocketsClient* _clients[maxClients];
};
