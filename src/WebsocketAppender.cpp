/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// see: https://github.com/gilmaimon/ArduinoWebsockets
#include "WebsocketAppender.h"

WebSocketAppender::WebSocketAppender() {
  for (uint8_t index = 0; index < maxClients; index++) {
    _clients[index] = NULL;
  }
}

void WebSocketAppender::Initialize() { _webSocketsServer.listen(WebSocketAppender::port); }
void WebSocketAppender::Loop() {
  // CheckConnectedClients();
  if (WiFi.status() == WL_CONNECTED && GetClientsCount() < maxClients) {
    if (_webSocketsServer.poll() == false) {
      return;
    }

    // Serial.println("add websocket client.");
    WebsocketsClient client = _webSocketsServer.accept();
    AddClient(new WebsocketsClient(client));
  }
}

void WebSocketAppender::Log(const char* message) {
  // Serial.println("Log websocket.");
  // Serial.printf("%d clients connected.\n", GetClientsCount());

  for (uint8_t index = 0; index < maxClients; index++) {
    WebsocketsClient* client = _clients[index];
    if (client == NULL) {
      continue;
    }

    if (!client->available() || !client->send(message)) {
      _clients[index] = NULL;
      // Serial.println("Remove disconnected websocket client from Log().");
      client->close();
      delete client;
    }
  }
}

uint8_t WebSocketAppender::GetClientsCount() {
  uint8_t count = 0;
  for (uint8_t index = 0; index < maxClients; index++) {
    if (_clients[index] != NULL) {
      count++;
    }
  }

  return count;
}

void WebSocketAppender::AddClient(WebsocketsClient* client) {
  for (uint8_t index = 0; index < maxClients; index++) {
    if (_clients[index] == NULL) {
      _clients[index] = client;
      return;
    }
  }
}

void WebSocketAppender::CheckConnectedClients() {
  for (uint8_t index = 0; index < maxClients; index++) {
    WebsocketsClient* client = _clients[index];
    if (client == NULL) {
      continue;
    }

    if (!client->available()) {
      // Serial.println("Remove disconnected websocket client.");
      _clients[index] = NULL;
      client->close();
      delete client;
    }
  }
}