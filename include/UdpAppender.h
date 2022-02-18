/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <WiFi.h>
#include <WiFiUdp.h>
#include "LogAppender.h"
class UdpAppender : public ILogAppender {
 public:
  void Log(const char *message);
  void Initialize();

 private:
  static const uint16_t port = 10000;
  WiFiUDP udp;
};
