/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <WiFi.h>

const uint dirconPort = 36866;

class DirconServer {
 private:
 public:
  void checkForConnections();
  void sendValue();
  void start();
};

extern DirconServer dirconServer;