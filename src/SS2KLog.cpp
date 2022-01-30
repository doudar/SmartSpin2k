/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "SS2KLog.h"
#include "Main.h"

DebugInfo DebugInfo::INSTANCE = DebugInfo();
LogHandler logHandler         = LogHandler();

uint8_t LogHandler::_messageBuffer[LOG_BUFFER_SIZE_BYTES];

LogHandler::LogHandler() { _messageBufferHandle = xMessageBufferCreateStatic(LOG_BUFFER_SIZE_BYTES, _messageBuffer, &_messageBufferStruct); }

void LogHandler::addAppender(ILogAppender *appender) { _appenders.push_back(appender); }

void LogHandler::initialize() {
  for (ILogAppender *appender : _appenders) {
    appender->Initialize();
  }
}

void LogHandler::writeLogs() {
  const size_t buffer_size = 512;
  char buffer[buffer_size];

  for (int index = 0; index < 100; index++) {
    size_t receivedBytes = xMessageBufferReceive(_messageBufferHandle, &buffer, buffer_size - 1, 0);
    if (receivedBytes == 0) {
      return;
    }
    buffer[receivedBytes] = '\0';

    ESP_LOGE("LogHandler", "Received message from buffer %s", buffer);

    for (ILogAppender *appender : _appenders) {
      appender->Log(buffer);
    }
  }
  ESP_LOGE("LogHandler", "Return write logs. Messages remaining in buffer");
}

void LogHandler::writev(esp_log_level_t level, const char *format, va_list args) {
  if (_messageBufferHandle == NULL) {
    ESP_LOGE("LogHandler", "Can not send log message. Message Buffer is NULL");
  }

  const size_t buffer_size = 512;
  char buffer[buffer_size];

  vsnprintf(buffer, buffer_size, format, args);

  size_t bytesSent = xMessageBufferSend(_messageBufferHandle, buffer, strnlen(buffer, buffer_size), 0);
  if (bytesSent < strnlen(buffer, buffer_size)) {
    ESP_LOGE("LogHandler", "Can not send log message. Not enough free space left in buffer.");
  }
}

#if DEBUG_LOG_BUFFER_SIZE > 0
void DebugInfo::append_logv(const char *format, va_list args) { DebugInfo::INSTANCE.append_logv_internal(format, args); }

const std::string DebugInfo::get_and_clear_logs() { return DebugInfo::INSTANCE.get_and_clear_logs_internal(); }

void DebugInfo::append_logv_internal(const char *format, va_list args) {
  if (xSemaphoreTake(logBufferMutex, 0) == pdTRUE) {
    int written = vsnprintf(logBuffer + logBufferLength, DEBUG_LOG_BUFFER_SIZE - logBufferLength, format, args);
    SS2K_LOGD(DEBUG_INFO_LOG_TAG, "Wrote %d bytes to log", written);
    if (written < 0 || logBufferLength + written > DEBUG_LOG_BUFFER_SIZE) {
      logBufferLength = snprintf(logBuffer, DEBUG_LOG_BUFFER_SIZE, "...\n");
    } else {
      logBufferLength += written;
    }
    SS2K_LOGD(DEBUG_INFO_LOG_TAG, "Log buffer length %d of %d bytes", logBufferLength, DEBUG_LOG_BUFFER_SIZE);
    xSemaphoreGive(logBufferMutex);
  }
}

const std::string DebugInfo::get_and_clear_logs_internal() {
  if (xSemaphoreTake(logBufferMutex, 0) == pdTRUE) {
    const std::string debugLog = std::string(logBuffer, logBufferLength);
    logBufferLength            = 0;
    logBuffer[0]               = '\0';
    xSemaphoreGive(logBufferMutex);
    SS2K_LOGD(DEBUG_INFO_LOG_TAG, "Log buffer read %d bytes and cleared", logBufferLength);
    return debugLog;
  }
  return "";
}
#else
const std::string DebugInfo::get_and_clear_logs() { return ""; }
#endif  // DEBUG_LOG_BUFFER_SIZE > 0

void ss2k_remove_newlines(std::string *str) {
  std::string::size_type pos = 0;
  while ((pos = (*str).find("\n", pos)) != std::string::npos) {
    (*str).replace(pos, 1, " ");
  }
}

int ss2k_log_hex_to_buffer(const byte *data, const size_t data_length, char *buffer, const int buffer_offset, const size_t buffer_length) {
  int written = 0;
  for (int data_offset = 0; data_offset < data_length; data_offset++) {
    written += snprintf(buffer + buffer_offset + written, buffer_length - written + buffer_offset, "%02x ", *(data + data_offset));
  }
  return written;
}

void ss2k_log_write(esp_log_level_t level, const char *format, ...) {
  va_list args;
  va_start(args, format);
  ss2k_log_writev(level, format, args);
  va_end(args);
}

void ss2k_log_writev(esp_log_level_t level, const char *format, va_list args) {
  esp_log_writev(level, SS2K_LOG_TAG, format, args);
  logHandler.writev(level, format, args);

  // if (userConfig.getUdpLogEnabled()) {
  //   UdpLogger::log(format, args);
  // } else {
  //   WebSocket::log(format, args);
  // }

  // #if DEBUG_LOG_BUFFER_SIZE > 0
  //   DebugInfo::append_logv(format, args);
  // #endif  // DEBUG_LOG_BUFFER_SIZE > 0
}
