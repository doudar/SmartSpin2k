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
 public:
  bool internetConnection;

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

// wifi Function
void startWifi();
void stopWifi();


extern HTTP_Server httpServer;
