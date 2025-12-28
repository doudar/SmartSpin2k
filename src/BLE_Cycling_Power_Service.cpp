/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "BLE_Cycling_Power_Service.h"
#include "DirConManager.h"
#include <Constants.h>

BLE_Cycling_Power_Service::BLE_Cycling_Power_Service() : pPowerMonitor(nullptr), cyclingPowerFeatureCharacteristic(nullptr), sensorLocationCharacteristic(nullptr) {}
void BLE_Cycling_Power_Service::setupService(NimBLEServer *pServer, MyCharacteristicCallbacks *chrCallbacks) {
  // Power Meter service setup
  pPowerMonitor                         = spinBLEServer.pServer->createService(CYCLINGPOWERSERVICE_UUID);
  cyclingPowerMeasurementCharacteristic = pPowerMonitor->createCharacteristic(CYCLINGPOWERMEASUREMENT_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  cyclingPowerFeatureCharacteristic     = pPowerMonitor->createCharacteristic(CYCLINGPOWERFEATURE_UUID, NIMBLE_PROPERTY::READ);
  sensorLocationCharacteristic          = pPowerMonitor->createCharacteristic(SENSORLOCATION_UUID, NIMBLE_PROPERTY::READ);
  byte cpsLocation[1]                   = {0b0101};  // sensor location 5 == left crank

  CyclingPowerFeatureFlags::Types cpFeatureFlags = CyclingPowerFeatureFlags::WheelRevolutionDataSupported | CyclingPowerFeatureFlags::CrankRevolutionDataSupported;

  cpFeature[0] = static_cast<uint8_t>(cpFeatureFlags & 0xFF);
  cpFeature[1] = static_cast<uint8_t>((cpFeatureFlags >> 8) & 0xFF);
  cpFeature[2] = static_cast<uint8_t>((cpFeatureFlags >> 16) & 0xFF);
  cpFeature[3] = static_cast<uint8_t>((cpFeatureFlags >> 24) & 0xFF);

  cyclingPowerFeatureCharacteristic->setValue(cpFeature, sizeof(cpFeature));
  sensorLocationCharacteristic->setValue(cpsLocation, sizeof(cpsLocation));
  cyclingPowerMeasurementCharacteristic->setCallbacks(chrCallbacks);
  pPowerMonitor->start();

  // Add service UUID to DirCon MDNS
  //DirConManager::addBleServiceUuid(pPowerMonitor->getUUID());
}

void BLE_Cycling_Power_Service::update() {
  int power     = rtConfig->watts.getValue();
  float cadence = rtConfig->cad.getValue();

  CyclingPowerMeasurement cpm;

  // Clear all flags initially
  memset(&cpm.flags, 0, sizeof(cpm.flags));

  // Set flags based on available data
  cpm.flags.crankRevolutionDataPresent = 1;  // Crank Revolution Data Present
  cpm.flags.wheelRevolutionDataPresent = 1;  // Wheel Revolution Data Present

  // Set data fields
  cpm.instantaneousPower         = power;
  cpm.cumulativeCrankRevolutions = spinBLEClient.cscCumulativeCrankRev;
  cpm.lastCrankEventTime         = spinBLEClient.cscLastCrankEvtTime;
  cpm.cumulativeWheelRevolutions = spinBLEClient.cscCumulativeWheelRev;
  cpm.lastWheelEventTime         = spinBLEClient.cscLastWheelEvtTime;

  auto byteArray = cpm.toByteArray();

  // Notify the cycling power measurement characteristic
  spinBLEServer.notifyBleAndDircon(cyclingPowerMeasurementCharacteristic, &byteArray[0], byteArray.size());

  const int kLogBufCapacity = 150;
  char logBuf[kLogBufCapacity];
  const size_t byteArrayLength = byteArray.size();

  logCharacteristic(logBuf, kLogBufCapacity, &byteArray[0], byteArrayLength, CYCLINGPOWERSERVICE_UUID, cyclingPowerMeasurementCharacteristic->getUUID(),
                    "CPS(CPM)[ CD(%.2f) PW(%d) ]", cadence > 0 ? fmodf(cadence, 1000.0) : 0, power % 10000);
}
