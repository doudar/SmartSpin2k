/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "UdpLogger.h"

UdpLogger UdpLogger::INSTANCE = UdpLogger();

void UdpLogger::log(const char *format, va_list args) { UdpLogger::INSTANCE.log_internal(format, args); }

void UdpLogger::log_internal(const char *format, va_list args) {
  if (WiFi.status() == WL_CONNECTED) {
    const size_t buffer_size = 255;
    char buffer[buffer_size];

    vsnprintf(buffer, buffer_size, format, args);

    this->udp.beginPacket("255.255.255.255", this->port);
    this->udp.write((uint8_t *)buffer, strlen(buffer));
    this->udp.endPacket();
  } else {
    ESP_LOGE(UDP_LOGGER_TAG, "UPD not logging. No WIFI connection.");
  }
}