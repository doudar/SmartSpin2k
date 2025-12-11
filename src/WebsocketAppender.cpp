/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// see: https://github.com/gilmaimon/ArduinoWebsockets
#include "WebsocketAppender.h"
#include "BLE_Custom_Characteristic.h"

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

  // Handle incoming messages from connected clients
  HandleIncomingMessages();
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
      // Set up message callback for this client
      client->onMessage([this](WebsocketsClient& client, WebsocketsMessage message) {
        this->OnMessageReceived(client, message);
      });
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

void WebSocketAppender::HandleIncomingMessages() {
  for (uint8_t index = 0; index < maxClients; index++) {
    WebsocketsClient* client = _clients[index];
    if (client == NULL) {
      continue;
    }

    if (client->available()) {
      // Poll for messages - the onMessage callback will be triggered
      client->poll();
    }
  }
}

void WebSocketAppender::OnMessageReceived(WebsocketsClient& client, WebsocketsMessage message) {
  // Only process binary or text messages
  if (!message.isBinary() && !message.isText()) {
    return;
  }

  // Convert message data to std::string for processing
  std::string rxValue;
  if (message.isBinary()) {
    // Binary data - directly use the raw data
    rxValue = std::string(message.c_str(), message.length());
  } else {
    // Text data - also use as-is (could be hex-encoded or similar)
    rxValue = std::string(message.c_str(), message.length());
  }

  // Process the custom characteristic command using the existing BLE processing logic
  BLE_ss2kCustomCharacteristic::process(rxValue);
}