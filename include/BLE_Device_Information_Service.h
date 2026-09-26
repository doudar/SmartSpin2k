/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

class NimBLEServer;

class BLE_Device_Information_Service {
 public:
  void setupService(NimBLEServer* pServer);
};
