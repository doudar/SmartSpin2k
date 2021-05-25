/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "sdkconfig.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <SPIFFS.h>
#include <freertos/semphr.h>

#ifndef DEBUG_LOG_BUFFER_SIZE
#define DEBUG_LOG_BUFFER_SIZE 1000
#endif

#ifndef DEBUG_FILE_CHARS_PER_LINE
#define DEBUG_FILE_CHARS_PER_LINE 64
#endif

#ifndef CORE_DEBUG_LEVEL
#define CORE_DEBUG_LEVEL CONFIG_ARDUHAL_LOG_DEFAULT_LEVEL
#endif

#if CORE_DEBUG_LEVEL >= 4
#define SS2K_LOGD(tag, format, ...) SS2K_MODLOG_DFLT(ERROR, "D %lu %s: " #format "\n", millis(), tag, ##__VA_ARGS__)
#else
#define SS2K_LOGD(tag, format, ...) (void)tag
#endif

#if CORE_DEBUG_LEVEL >= 3
#define SS2K_LOGI(tag, format, ...) SS2K_MODLOG_DFLT(ERROR, "I %lu %s: " #format "\n", millis(), tag, ##__VA_ARGS__)
#else
#define SS2K_LOGI(tag, format, ...) (void)tag
#endif

#if CORE_DEBUG_LEVEL >= 2
#define SS2K_LOGW(tag, format, ...) SS2K_MODLOG_DFLT(ERROR, "W %lu %s: " #format "\n", millis(), tag, ##__VA_ARGS__)
#else
#define SS2K_LOGW(tag, format, ...) (void)tag
#endif

#if CORE_DEBUG_LEVEL >= 1
#define SS2K_LOGE(tag, format, ...) SS2K_MODLOG_DFLT(ERROR, "E %lu %s: " #format "\n", millis(), tag, ##__VA_ARGS__)
#else
#define SS2K_LOGE(tag, format, ...) (void)tag
#endif

#define SS2K_LOGC(tag, format, ...) SS2K_MODLOG_DFLT(CRITICAL, "C %lu %s: " #format "\n", millis(), tag, ##__VA_ARGS__)
#define SS2K_LOG(tag, format, ...)  SS2K_MODLOG_DFLT(CRITICAL, "N %lu %s: " #format "\n", millis(), tag, ##__VA_ARGS__)

class DebugInfo {
 public:
  static void appendLog(const char *format, ...);

  static const std::string getAndClearLogs();

 private:
  static DebugInfo INSTANCE;
#if DEBUG_LOG_BUFFER_SIZE > 0
  DebugInfo() : logBufferLength(0), logBufferMutex(xSemaphoreCreateMutex()) { logBuffer[0] = '\0'; }
  int logBufferLength;
  char logBuffer[DEBUG_LOG_BUFFER_SIZE];
  SemaphoreHandle_t logBufferMutex;
  void appendLog_internal(const char *format, va_list args);
  const std::string getAndClearLogs_internal();
#else
  DebugInfo() {}
#endif
};

#if DEBUG_LOG_BUFFER_SIZE > 0
#define SS2K_MODLOG_ESP_LOCAL(level, ml_msg_, ...)          \
  do {                                                      \
    if (LOG_LOCAL_LEVEL >= level) {                         \
      esp_log_write(level, "SS2K", ml_msg_, ##__VA_ARGS__); \
      DebugInfo::appendLog(ml_msg_, ##__VA_ARGS__);         \
    }                                                       \
  } while (0)
#else
#define SS2K_MODLOG_ESP_LOCAL(level, ml_msg_, ...)                                      \
  do {                                                                                  \
    if (LOG_LOCAL_LEVEL >= level) esp_log_write(level, "SS2K", ml_msg_, ##__VA_ARGS__); \
  } while (0)
#endif

#define SS2K_MODLOG_DEBUG(ml_mod_, ml_msg_, ...) SS2K_MODLOG_ESP_LOCAL(ESP_LOG_DEBUG, ml_msg_, ##__VA_ARGS__)

#define SS2K_MODLOG_INFO(ml_mod_, ml_msg_, ...) SS2K_MODLOG_ESP_LOCAL(ESP_LOG_INFO, ml_msg_, ##__VA_ARGS__)

#define SS2K_MODLOG_WARN(ml_mod_, ml_msg_, ...) SS2K_MODLOG_ESP_LOCAL(ESP_LOG_WARN, ml_msg_, ##__VA_ARGS__)

#define SS2K_MODLOG_ERROR(ml_mod_, ml_msg_, ...) SS2K_MODLOG_ESP_LOCAL(ESP_LOG_ERROR, ml_msg_, ##__VA_ARGS__)

#define SS2K_MODLOG_CRITICAL(ml_mod_, ml_msg_, ...) SS2K_MODLOG_ESP_LOCAL(ESP_LOG_ERROR, ml_msg_, ##__VA_ARGS__)

#define SS2K_MODLOG(ml_lvl_, ml_mod_, ...) SS2K_MODLOG_##ml_lvl_((ml_mod_), __VA_ARGS__)

#define SS2K_MODLOG_DFLT(ml_lvl_, ...) SS2K_MODLOG(ml_lvl_, LOG_MODULE_DEFAULT, __VA_ARGS__);

void ss2k_remove_newlines(std::string *str);

int ss2k_log_hex_to_buffer(const byte *data, const size_t data_length, char *buffer, const int buffer_offset, const size_t buffer_length);

void ss2k_log_file_internal(const char *tag, File file);

#define SS2K_LOG_FILE(tag, file)       \
  do {                                 \
    ss2k_log_file_internal(tag, file); \
  } while (0)
