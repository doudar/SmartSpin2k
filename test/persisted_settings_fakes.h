/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <cassert>
#include <cstring>
#include <string>
#include <ArduinoJson.h>
#include "SmartSpin_parameters.h"
#define SS2K_LOG(...) ((void)0)
#define FILE_WRITE 1
std::string fileContents;
struct File {
  size_t position=0;
  bool valid=true;
  operator bool() const { return valid; }
  int read() { return position < fileContents.size() ? static_cast<unsigned char>(fileContents[position++]) : -1; }
  size_t readBytes(char* out,size_t length) {
    size_t n=0; while(n<length && position<fileContents.size()) out[n++]=fileContents[position++]; return n;
  }
  size_t write(uint8_t c) { fileContents.push_back(c); return 1; }
  size_t write(const uint8_t* data,size_t length) { fileContents.append(reinterpret_cast<const char*>(data),length); return length; }
  void close() {}
};
struct MemoryFS {
  void remove(const char*) { fileContents.clear(); }
  bool failFormat = false;
  bool format() { if (failFormat) return false; fileContents.clear(); return true; }
  File open(const char*,int mode=0) { File file; file.valid=mode || !fileContents.empty(); return file; }
} LittleFS;
