/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "SS2KLog.h"
#include "Constants.h"

#include <sensors/SensorData.h>
#include <sensors/SensorDataFactory.h>

SensorDataFactory sensorDataFactory;

void collectAndSet(NimBLEUUID charUUID, NimBLEUUID serviceUUID, std::string& uniqueName, uint8_t *pData, size_t length) {
  const int kLogBufMaxLength = 250;
  char logBuf[kLogBufMaxLength];
  SS2K_LOGD(BLE_COMMON_LOG_TAG, "Data length: %d", length);
  int logBufLength = ss2k_log_hex_to_buffer(pData, length, logBuf, 0, kLogBufMaxLength);

  logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, "<- %.8s | %.8s", serviceUUID.toString().c_str(), charUUID.toString().c_str());

  std::shared_ptr<SensorData> sensorData = sensorDataFactory.getSensorData(charUUID, uniqueName, pData, length);

  logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " | %s[", sensorData->getId().c_str());
  if (sensorData->hasHeartRate() && !rtConfig->hr.getSimulate()) {
    int heartRate = sensorData->getHeartRate();
    static int zeroCount = 0;
    zeroCount++;
    if (heartRate > 0) {
      rtConfig->hr.setValue(heartRate);
      logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " HR(%d)", heartRate % 1000);
      spinBLEClient.connectedHRM = true;
      zeroCount = 0;
    }else{
      //require 10 readings in a row before setting the HR to 0
      logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " HR IGNORED");
      if (zeroCount > 10){
        rtConfig->hr.setValue(0);
        spinBLEClient.connectedHRM = false;
        zeroCount = 0;
      }
    }
  }

  if (sensorData->hasCadence() && !rtConfig->cad.getSimulate()) {
    if ((charUUID == PELOTON_DATA_UUID) && !(strcmp(userConfig->getConnectedPowerMeter(), NONE) == 0 || strcmp(userConfig->getConnectedPowerMeter(), ANY) == 0)) {
      // Peloton connected but using BLE Power Meter. So skip cad for Peloton UUID.
    } else {
      float cadence = sensorData->getCadence();
      rtConfig->cad.setValue(cadence);
      spinBLEClient.connectedCD = true;
      logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " CD(%.2f)", fmodf(cadence, 1000.0));
    }
  }

  if (sensorData->hasPower() && !rtConfig->watts.getSimulate() && !userConfig->getPTab4Pwr()) {
    if ((charUUID == PELOTON_DATA_UUID) && !((strcmp(userConfig->getConnectedPowerMeter(), NONE) == 0) || (strcmp(userConfig->getConnectedPowerMeter(), ANY) == 0))) {
      // Peloton connected but using BLE Power Meter. So skip power for Peloton UUID.
    } else {
      int power = sensorData->getPower() * userConfig->getPowerCorrectionFactor();
      rtConfig->watts.setValue(power);
      spinBLEClient.connectedPM = true;
      logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " PW(%d)", power % 10000);
    }
  }

  if (sensorData->hasSpeed()) {
    float speed = sensorData->getSpeed();
    rtConfig->setSimulatedSpeed(speed);
    spinBLEClient.connectedSpeed = true;
    logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " SD(%.2f)", fmodf(speed, 1000.0));
  }

  if (sensorData->hasResistance()) {
    if ((rtConfig->getMaxResistance() == MAX_PELOTON_RESISTANCE) && (charUUID != PELOTON_DATA_UUID)) {
      // Peloton connected but using BLE Power Meter. So skip resistance for UUID's that aren't Peloton.
    } else {
      int resistance = sensorData->getResistance();
      rtConfig->resistance.setValue(resistance);
      logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " RS(%d)", resistance % 1000);
    }
  }

  //adding incline so that i can plot it
  logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " POS(%d)", ss2k->getCurrentPosition());
  strncat(logBuf + logBufLength, " ]", kLogBufMaxLength - logBufLength);

// Peloton data screams, so only log one per second.
#ifdef DEBUG_BLE_TX_RX
  static long int lastTime = millis();
  if ((charUUID == PELOTON_DATA_UUID) && (millis() - lastTime < 1000)) return;

  SS2K_LOG(BLE_COMMON_LOG_TAG, "%s", logBuf);

  if (charUUID == PELOTON_DATA_UUID) lastTime = millis();
#endif

}