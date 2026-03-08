/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <NimBLEDevice.h>
#include "BLE_Common.h"

class BLE_Fitness_Machine_Service {
 public:
  BLE_Fitness_Machine_Service();
  void setupService(NimBLEServer *pServer, MyCharacteristicCallbacks *chrCallbacks);
  void update();
  bool spinDown(uint8_t response);
  void processFTMSWrite();
  
 private:
  int calculateResistanceFromPosition();
  BLEService *pFitnessMachineService;
  BLECharacteristic *fitnessMachineFeature;
  BLECharacteristic *fitnessMachineIndoorBikeData;
  BLECharacteristic *fitnessMachineStatusCharacteristic;
  BLECharacteristic *fitnessMachineControlPoint;
  BLECharacteristic *fitnessMachineResistanceLevelRange;
  BLECharacteristic *fitnessMachinePowerRange;
  BLECharacteristic *fitnessMachineInclinationRange;
  BLECharacteristic *fitnessMachineTrainingStatus;
};

extern BLE_Fitness_Machine_Service fitnessMachineService;
