/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <WebSocketsServer.h>

#define WEBSOCKET_LOGGER_TAG "Websocket"

class WebSocket {
 private:
  void log_internal(const char *format, va_list args);
  static void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

 public:
  static WebSocket INSTANCE;
  static void log(const char *format, va_list args);
  void start();
  void loop();
};
