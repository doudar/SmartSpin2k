/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "SS2KLog.h"
#include "Constants.h"
#include "BLE_Common.h"

#include <sensors/SensorData.h>
#include <sensors/SensorDataFactory.h>

SensorDataFactory sensorDataFactory;

void collectAndSet(const NimBLEUUID& charUUID, const NimBLEUUID& serviceUUID, const std::string& uniqueName, uint8_t* pData, size_t length) {
  // Update the timestamp for disconnect detection
  for (size_t i = 0; i < NUM_BLE_DEVICES; i++) {
    if (spinBLEClient.myBLEDevices[i].uniqueName == uniqueName) {
      spinBLEClient.myBLEDevices[i].lastDataUpdateTime = millis();
      break;
    }
  }

  SS2K_LOGD(BLE_COMMON_LOG_TAG, "Data length: %d", length);
#ifdef DEBUG_BLE_TX_RX
  const int kLogBufMaxLength = 250;
  char logBuf[kLogBufMaxLength];
  int logBufLength = ss2k_log_hex_to_buffer(pData, length, logBuf, 0, kLogBufMaxLength);
#define SENSOR_LOG_APPEND(...) logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, __VA_ARGS__)
#else
#define SENSOR_LOG_APPEND(...)
#endif

  SENSOR_LOG_APPEND("<- %.8s | %.8s", serviceUUID.toString().c_str(), charUUID.toString().c_str());

  std::shared_ptr<SensorData> sensorData = sensorDataFactory.getSensorData(charUUID, uniqueName, pData, length);

  SENSOR_LOG_APPEND(" | %s[", sensorData->getId().c_str());
  if (sensorData->hasHeartRate() && !rtConfig->hr.getSimulate()) {
    int heartRate        = sensorData->getHeartRate();
    static int zeroCount = 0;
    zeroCount++;
    if (heartRate > 0) {
      rtConfig->hr.setValue(heartRate);
      SENSOR_LOG_APPEND(" HR(%d)", heartRate % 1000);
      spinBLEClient.connectedHRM = true;
      zeroCount                  = 0;
    } else {
      // require 10 readings in a row before setting the HR to 0
      SENSOR_LOG_APPEND(" HR IGNORED");
      if (zeroCount > 10) {
        rtConfig->hr.setValue(0);
        spinBLEClient.connectedHRM = false;
        zeroCount                  = 0;
      }
    }
  }

  if (sensorData->hasCadence() && !rtConfig->cad.getSimulate()) {
    if ((charUUID == PELOTON_DATA_UUID) && !(strcmp(userConfig->getConnectedPowerMeter(), NONE) == 0 || strcmp(userConfig->getConnectedPowerMeter(), ANY) == 0)) {
      // Peloton connected but using BLE Power Meter. So skip cad for Peloton UUID.
    } else {
      int cadence = round(sensorData->getCadence());
      if (cadence > 0 && cadence < 250) {
        rtConfig->cad.setValue(cadence);
        spinBLEClient.connectedCD = true;
        SENSOR_LOG_APPEND(" CD(%.2f)", fmodf(cadence, 1000.0));
      } else {
        rtConfig->cad.setValue(0);
        // log cadence ignored
        SENSOR_LOG_APPEND(" CD IGNORED");
      }
    }
  }

  if (sensorData->hasPower() && !rtConfig->watts.getSimulate() && !userConfig->getPTab4Pwr()) {
    if ((charUUID == PELOTON_DATA_UUID) && !((strcmp(userConfig->getConnectedPowerMeter(), NONE) == 0) || (strcmp(userConfig->getConnectedPowerMeter(), ANY) == 0))) {
      // Peloton connected but using BLE Power Meter. So skip power for Peloton UUID.
    } else {
      int power = round(sensorData->getPower() * userConfig->getPowerCorrectionFactor());
      if (power > 0 && power < 3000) {
        rtConfig->watts.setValue(power);
        spinBLEClient.connectedPM = true;
        SENSOR_LOG_APPEND(" PW(%d)", power % 10000);
      } else {
        rtConfig->watts.setValue(0);
        SENSOR_LOG_APPEND(" PW IGNORED");
      }
    }
  }

  if (sensorData->hasSpeed()) {
    rtConfig->setSimulatedSpeed(sensorData->getSpeed());
    spinBLEClient.connectedSpeed = true;
    SENSOR_LOG_APPEND(" SD(%.2f)", fmodf(sensorData->getSpeed(), 1000.0));
  }

  if (sensorData->hasResistance() && uniqueName.starts_with("Grupetto")) { // Blacklist everything not Grupetto. 
    if (charUUID == PELOTON_DATA_UUID) {
      // Peloton connected but using BLE Power Meter. So skip resistance for UUID's that aren't Peloton.
    } else {
      rtConfig->resistance.setValue(sensorData->getResistance(), false);  // Publish value and real-data flag together.
      SENSOR_LOG_APPEND(" RS(%d)", sensorData->getResistance() % 1000);
    }
  }

  // adding incline so that i can plot it
  SENSOR_LOG_APPEND(" POS(%d)", ss2k->getCurrentPosition());
  SENSOR_LOG_APPEND(" ]");

// Peloton data screams, so only log one per second.
#ifdef DEBUG_BLE_TX_RX
  static long int lastTime = millis();
  if ((charUUID == PELOTON_DATA_UUID) && (millis() - lastTime < 1000)) return;

  SS2K_LOG(BLE_COMMON_LOG_TAG, "%s", logBuf);

  if (charUUID == PELOTON_DATA_UUID) lastTime = millis();
#endif
#undef SENSOR_LOG_APPEND
}
