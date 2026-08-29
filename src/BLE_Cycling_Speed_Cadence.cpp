/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "BLE_Cycling_Speed_Cadence.h"
#include "DirConManager.h"
#include "ByteUtils.h"
#include <Constants.h>

BLE_Cycling_Speed_Cadence::BLE_Cycling_Speed_Cadence() : pCyclingSpeedCadenceService(nullptr), cscMeasurement(nullptr), cscFeature(nullptr) {}

void BLE_Cycling_Speed_Cadence::setupService(NimBLEServer *pServer, MyCharacteristicCallbacks *chrCallbacks) {
  pCyclingSpeedCadenceService = pServer->createService(CSCSERVICE_UUID);
  cscMeasurement              = pCyclingSpeedCadenceService->createCharacteristic(CSCMEASUREMENT_UUID, NIMBLE_PROPERTY::NOTIFY);
  cscFeature                  = pCyclingSpeedCadenceService->createCharacteristic(CSCFEATURE_UUID, NIMBLE_PROPERTY::READ);

  CyclingSpeedCadenceFeatureFlags::Types cscFeatureFlags =
      CyclingSpeedCadenceFeatureFlags::WheelRevolutionDataSupported | CyclingSpeedCadenceFeatureFlags::CrankRevolutionDataSupported;

  put_le16(cscFeatureBytes, static_cast<uint16_t>(cscFeatureFlags));

  cscFeature->setValue(cscFeatureBytes, sizeof(cscFeatureBytes));
  cscMeasurement->setCallbacks(chrCallbacks);

  // Register with DirCon for service discovery
  DirConManager::registerService(pCyclingSpeedCadenceService->getUUID());
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

  CscMeasurement::Buffer byteArray;
  const size_t byteArrayLength = csc.toByteArray(byteArray);

  // Notify the cycling power measurement characteristic
  // Need to set the value before notifying so that read works correctly.
  cscMeasurement->setValue(byteArray.data(), byteArrayLength);
  cscMeasurement->notify();

  const int kLogBufCapacity = 150;
  char logBuf[kLogBufCapacity];

  logCharacteristic(logBuf, kLogBufCapacity, byteArray.data(), byteArrayLength, CSCSERVICE_UUID, cscMeasurement->getUUID(),
                    "CSC(CSM)[ WheelRev(%lu) WheelTime(%u) CrankRev(%u) CrankTime(%u) ]", spinBLEClient.cscCumulativeWheelRev, spinBLEClient.cscLastWheelEvtTime,
                    spinBLEClient.cscCumulativeCrankRev, spinBLEClient.cscLastCrankEvtTime);
}
