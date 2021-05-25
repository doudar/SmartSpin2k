/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <Arduino.h>

void startHttpServer();
void webClientUpdate(void *pvParameters);
void handleSpiffsFile();
void handleIndexFile();
void settingsProcessor();
void handleHrSlider();
void FirmwareUpdate();

#ifdef USE_TELEGRAM
#define SEND_TO_TELEGRAM(message) sendTelegram(message);

void sendTelegram(String textToSend);
void telegramUpdate(void *pvParameters);
#else
#define SEND_TO_TELEGRAM(message) (void)message
#endif

// wifi Function
void startWifi();
