#pragma once

#include <WiFi.h>
#include <WiFiUdp.h>

#define UDP_LOGGER_TAG "UdpLogger"

class UdpLogger {
 public:
  static void log(const char *format, va_list args);

 private:
  static UdpLogger INSTANCE;
  static const uint16_t port = 10000;
  WiFiUDP udp;
  void log_internal(const char *format, va_list args);
};
