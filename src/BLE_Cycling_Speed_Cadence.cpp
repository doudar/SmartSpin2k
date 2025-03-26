/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BLE_Cycling_Speed_Cadence.h"
#include <Constants.h>

BLE_Cycling_Speed_Cadence::BLE_Cycling_Speed_Cadence() : pCyclingSpeedCadenceService(nullptr), cscMeasurement(nullptr), cscFeature(nullptr) {}

void BLE_Cycling_Speed_Cadence::setupService(NimBLEServer *pServer, MyCharacteristicCallbacks *chrCallbacks) {
  pCyclingSpeedCadenceService = pServer->createService(CSCSERVICE_UUID);
  cscMeasurement              = pCyclingSpeedCadenceService->createCharacteristic(CSCMEASUREMENT_UUID, NIMBLE_PROPERTY::NOTIFY);
  cscFeature                  = pCyclingSpeedCadenceService->createCharacteristic(CSCFEATURE_UUID, NIMBLE_PROPERTY::READ);
  
  CyclingSpeedCadenceFeatureFlags::Types cscFeatureFlags = CyclingSpeedCadenceFeatureFlags::WheelRevolutionDataSupported | 
                                                           CyclingSpeedCadenceFeatureFlags::CrankRevolutionDataSupported;

  cscFeatureBytes[0] = static_cast<uint8_t>(cscFeatureFlags & 0xFF);
  cscFeatureBytes[1] = static_cast<uint8_t>((cscFeatureFlags >> 8) & 0xFF);

  cscFeature->setValue(cscFeatureBytes, sizeof(cscFeatureBytes));
  cscMeasurement->setCallbacks(chrCallbacks);
  pCyclingSpeedCadenceService->start();
  //spinBLEServer.pServer->getAdvertising()->addServiceUUID(pCyclingSpeedCadenceService->getUUID());
}

void BLE_Cycling_Speed_Cadence::update() {
  /*if (!spinBLEServer.clientSubscribed.CyclingSpeedCadence) {
    return;
  }*/

  CscMeasurement csc;

  // Clear all flags initially
  *(reinterpret_cast<uint8_t *>(&(csc.flags))) = 0;

  // Set flags based on data presence
  csc.flags.wheelRevolutionDataPresent = 1;  // Wheel Revolution Data Present
  csc.flags.crankRevolutionDataPresent = 1;  // Crank Revolution Data Present

  // Set data fields
  csc.cumulativeWheelRevolutions = spinBLEClient.cscCumulativeWheelRev;
  csc.lastWheelEventTime         = spinBLEClient.cscLastWheelEvtTime;
  csc.cumulativeCrankRevolutions = spinBLEClient.cscCumulativeCrankRev;
  csc.lastCrankEventTime         = spinBLEClient.cscLastCrankEvtTime;

  auto byteArray = csc.toByteArray();

  cscMeasurement->setValue(&byteArray[0], byteArray.size());
  cscMeasurement->notify();

  const int kLogBufCapacity = 150;
  char logBuf[kLogBufCapacity];
  const size_t byteArrayLength = byteArray.size();

  logCharacteristic(logBuf, kLogBufCapacity, &byteArray[0], byteArrayLength, CSCSERVICE_UUID, cscMeasurement->getUUID(),
                    "CSC(CSM)[ WheelRev(%lu) WheelTime(%u) CrankRev(%u) CrankTime(%u) ]", spinBLEClient.cscCumulativeWheelRev, spinBLEClient.cscLastWheelEvtTime,
                    spinBLEClient.cscCumulativeCrankRev, spinBLEClient.cscLastCrankEvtTime);
}
