/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <LittleFS.h>
#include <SPIFFS.h>
#include <Arduino.h>

#define FS_UPGRADER_LOG_TAG "FS_Upgrader"

class FSUpgrader {
 private:
  bool spiffsExists = false;
  File file;
  void loadFromSPIFFS();

 public:
  void upgradeFS();
};