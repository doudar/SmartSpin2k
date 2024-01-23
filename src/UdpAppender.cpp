/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "UdpAppender.h"
#include "Main.h"

void UdpAppender::Initialize() {}

void UdpAppender::Log(const char *message) {
  if (WiFi.status() == WL_CONNECTED && userConfig->getUdpLogEnabled()) {
    this->udp.beginPacket("255.255.255.255", this->port);
    this->udp.write((uint8_t *)message, strlen(message));
    this->udp.endPacket();
  }
}