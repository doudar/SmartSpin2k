/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// see: https://github.com/gilmaimon/ArduinoWebsockets
#include "WebsocketAppender.h"

void WebSocketAppender::Initialize() { _webSocketsServer.listen(WebSocketAppender::port); }
void WebSocketAppender::Loop() {
  if (WiFi.status() == WL_CONNECTED && !_client.available()) {
    _client.close();
    if (_webSocketsServer.poll() == false) {
      return;
    }
    _client = _webSocketsServer.accept();
  }
}

void WebSocketAppender::Log(const char *message) {
  if (_client.available()) {
    _client.send(message);
  }
}
