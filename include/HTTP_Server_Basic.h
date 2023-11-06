/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <Arduino.h>

#define HTTP_SERVER_LOG_TAG "HTTP_Server"

class HTTP_Server {
 private:
  String processWiFiHTML();

 public:
  bool internetConnection = false;
  bool isServing = false;
  void start();
  void stop();
  static void handleLittleFSFile();
  static void handleIndexFile();
  static void settingsProcessor();
  static void handleHrSlider();
  static void FirmwareUpdate();
  static void webClientUpdate(void *pvParameters);
  HTTP_Server() { internetConnection = false; }
};

#ifdef USE_TELEGRAM
#define SEND_TO_TELEGRAM(message) sendTelegram(message);

void sendTelegram(String textToSend);
void telegramUpdate(void *pvParameters);
#else
#define SEND_TO_TELEGRAM(message) (void)message
#endif

void startWifi();
void stopWifi();
String getScanItemOut();
String encryptionTypeStr(uint8_t authmode);
bool WiFi_scanNetworks();
String getHTTPHead();
int getRSSIasQuality(int RSSI);
String htmlEntities(String str, bool whitespace = false);

extern HTTP_Server httpServer;
