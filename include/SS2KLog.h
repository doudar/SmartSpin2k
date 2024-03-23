/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#ifndef UNIT_TEST

#include "sdkconfig.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <FS.h>
#include <freertos/semphr.h>
#include <freertos/message_buffer.h>
#include "LogAppender.h"
#include <vector>

#define SS2K_LOG_TAG    "SS2K"
#define LOG_HANDLER_TAG "Log_Handler"

#ifndef DEBUG_FILE_CHARS_PER_LINE
#define DEBUG_FILE_CHARS_PER_LINE 64
#endif

#ifndef CORE_DEBUG_LEVEL
#define CORE_DEBUG_LEVEL CONFIG_ARDUHAL_LOG_DEFAULT_LEVEL
#endif

#if CORE_DEBUG_LEVEL >= 4
#define SS2K_LOGD(tag, format, ...) ss2k_log_write(ESP_LOG_DEBUG, tag, format, ##__VA_ARGS__);
#else
#define SS2K_LOGD(tag, format, ...) (void)tag
#endif

#if CORE_DEBUG_LEVEL >= 3
#define SS2K_LOGI(tag, format, ...) ss2k_log_write(ESP_LOG_INFO, tag, format, ##__VA_ARGS__);
#else
#define SS2K_LOGI(tag, format, ...) (void)tag
#endif

#if CORE_DEBUG_LEVEL >= 2
#define SS2K_LOGW(tag, format, ...) ss2k_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__);
#else
#define SS2K_LOGW(tag, format, ...) (void)tag
#endif

#if CORE_DEBUG_LEVEL >= 1
#define SS2K_LOGE(tag, format, ...) ss2k_log_write(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__);

#else
#define SS2K_LOGE(tag, format, ...) (void)tag
#endif
#define SS2K_LOG(tag, format, ...) ss2k_log_write(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__);

#define LOG_BUFFER_SIZE_BYTES 6000
class LogHandler {
 public:
  LogHandler();
  void writev(esp_log_level_t level, const char *module, const char *format, va_list args);
  void addAppender(ILogAppender *appender);
  void initialize();
  void writeLogs();

 private:
  static uint8_t _messageBuffer[];
  StaticMessageBuffer_t _messageBufferStruct;
  MessageBufferHandle_t _messageBufferHandle;
  SemaphoreHandle_t _logBufferMutex;
  std::vector<ILogAppender *> _appenders;

  char _logLevelToLetter(esp_log_level_t level);
};

extern LogHandler logHandler;

void ss2k_remove_newlines(std::string *str);

int ss2k_log_hex_to_buffer(const byte *data, const size_t data_length, char *buffer, const int buffer_offset, const size_t buffer_length);
int ss2k_log_hex_to_buffer(const char *data, const size_t data_length, char *buffer, const int buffer_offset, const size_t buffer_length);

void ss2k_log_write(esp_log_level_t level, const char *module, const char *format, ...);

#else

#endif