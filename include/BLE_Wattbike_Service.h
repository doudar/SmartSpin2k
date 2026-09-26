/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <NimBLEDevice.h>

class BLE_Wattbike_Service {
 public:
  BLE_Wattbike_Service();
  void setupService(NimBLEServer *pServer);
  void update();
  void parseNemit();

 private:
  NimBLECharacteristic *wattbikeReadCharacteristic;
};
