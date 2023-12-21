/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "SS2KLog.h"
#include "Main.h"

LogHandler logHandler = LogHandler();

uint8_t LogHandler::_messageBuffer[LOG_BUFFER_SIZE_BYTES];

LogHandler::LogHandler() {
  _logBufferMutex      = xSemaphoreCreateMutex();
  _messageBufferHandle = xMessageBufferCreateStatic(LOG_BUFFER_SIZE_BYTES, _messageBuffer, &_messageBufferStruct);
}

void LogHandler::addAppender(ILogAppender *appender) { _appenders.push_back(appender); }

void LogHandler::initialize() {
  for (ILogAppender *appender : _appenders) {
    try {
      appender->Initialize();
    } catch (...) {
      SS2K_LOG(LOG_HANDLER_TAG, "Fatal error during initialize of log appender.");
    }
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

    for (ILogAppender *appender : _appenders) {
      try {
        appender->Log(buffer);
      } catch (...) {
        SS2K_LOG(LOG_HANDLER_TAG, "Fatal error during writing to log appender.");
      }
    }
  }
  SS2K_LOG(LOG_HANDLER_TAG, "Exit writeLogs(). Messages remaining in buffer.");
}

void LogHandler::writev(esp_log_level_t level, const char *module, const char *format, va_list args) {
  if (xSemaphoreTake(_logBufferMutex, 10) == pdFALSE) {
    // Must use ESP_LOG here using of SSK_LOG creates dead lock
    ESP_LOGE(LOG_HANDLER_TAG, "Can not write log message. Write is blocked by other task.");
    return;
  }

  if (_messageBufferHandle == NULL) {
    ESP_LOGE(LOG_HANDLER_TAG, "Can not send log message. Message Buffer is NULL");
    return;
  }

  char formatString[256];
  sprintf(formatString, "[%6lu][%c](%s): %s", millis(), _logLevelToLetter(level), module, format);

  const size_t buffer_size = 512;
  char buffer[buffer_size];
  int written = vsnprintf(buffer, buffer_size, formatString, args);

  // Default logger -> write all to serial if connected
  if (Serial) {
    Serial.println(buffer);
  }

  size_t bytesSent = xMessageBufferSend(_messageBufferHandle, buffer, written, 0);

  if (bytesSent < written) {
    ESP_LOGE(LOG_HANDLER_TAG, "Can not send log message. Not enough free space left in buffer.");
  }

  xSemaphoreGive(_logBufferMutex);
}

char LogHandler::_logLevelToLetter(esp_log_level_t level) {
  switch (level) {
    case ESP_LOG_ERROR:
      return 'E';
    case ESP_LOG_WARN:
      return 'W';
    case ESP_LOG_INFO:
      return 'I';
    case ESP_LOG_DEBUG:
      return 'D';
    case ESP_LOG_VERBOSE:
      return 'V';
  }
  return ' ';
}

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

int ss2k_log_hex_to_buffer(const char *data, const size_t data_length, char *buffer, const int buffer_offset, const size_t buffer_length) {
  int written = 0;
  for (int data_offset = 0; data_offset < data_length; data_offset++) {
    written += snprintf(buffer + buffer_offset + written, buffer_length - written + buffer_offset, "%02x ", *(data + data_offset));
  }
  return written;
}

void ss2k_log_write(esp_log_level_t level, const char *module, const char *format, ...) {
  va_list args;
  va_start(args, format);
  logHandler.writev(level, module, format, args);
  va_end(args);
}
