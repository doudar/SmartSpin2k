/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "SS2KLog.h"

DebugInfo DebugInfo::INSTANCE = DebugInfo();

#if DEBUG_LOG_BUFFER_SIZE > 0
void DebugInfo::appendLog(const char *format, ...) {
  va_list args;
  va_start(args, format);
  DebugInfo::INSTANCE.appendLog_internal(format, args);
  va_end(args);
}

const std::string DebugInfo::getAndClearLogs() { return DebugInfo::INSTANCE.getAndClearLogs_internal(); }

void DebugInfo::appendLog_internal(const char *format, va_list args) {
  if (xSemaphoreTake(logBufferMutex, 500) == pdTRUE) {
    int written = vsnprintf(logBuffer + logBufferLength, DEBUG_LOG_BUFFER_SIZE - logBufferLength, format, args);
    SS2K_LOGD("DebugInfo", "Wrote %d bytes to log", written);
    if (written < 0 || logBufferLength + written > DEBUG_LOG_BUFFER_SIZE) {
      logBufferLength = snprintf(logBuffer, DEBUG_LOG_BUFFER_SIZE, "...\n");
    } else {
      logBufferLength += written;
    }
    SS2K_LOGD("DebugInfo", "Log buffer length %d of %d bytes", logBufferLength, DEBUG_LOG_BUFFER_SIZE);
    xSemaphoreGive(logBufferMutex);
  }
}

const std::string DebugInfo::getAndClearLogs_internal() {
  if (xSemaphoreTake(logBufferMutex, 500) == pdTRUE) {
    const std::string debugLog = std::string(logBuffer, logBufferLength);
    logBufferLength = 0;
    logBuffer[0]    = '\0';
    xSemaphoreGive(logBufferMutex);
    SS2K_LOGD("DebugInfo", "Log buffer read %d bytes and cleared", logBufferLength);
    return debugLog;
  }
  return "";
}
#else
void DebugInfo::appendLog(const char *format, ...) {}

String DebugInfo::getAndClearLogs() { return ""; }
#endif

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

void ss2k_log_file_internal(const char *tag, File file) {
  if (!file.available()) {
    return;
  }
  char char_buffer[DEBUG_FILE_CHARS_PER_LINE + 1];
  int bytes_cur_line;

  while (file.available()) {
    if (file.available() > DEBUG_FILE_CHARS_PER_LINE) {
      bytes_cur_line = DEBUG_FILE_CHARS_PER_LINE;
    } else {
      bytes_cur_line = file.available();
    }
    for (int i = 0; i < bytes_cur_line; i++) {
      sprintf(char_buffer + i, "%c", file.read());
    }
    SS2K_LOG(tag, "%s", char_buffer);
  }
}
