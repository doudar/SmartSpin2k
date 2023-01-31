/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "SS2KLog.h"

#include <sensors/SensorData.h>
#include <sensors/SensorDataFactory.h>

SensorDataFactory sensorDataFactory;

void collectAndSet(NimBLEUUID charUUID, NimBLEUUID serviceUUID, NimBLEAddress address, uint8_t *pData, size_t length) {
  const int kLogBufMaxLength = 250;
  char logBuf[kLogBufMaxLength];
  SS2K_LOGD(BLE_COMMON_LOG_TAG, "Data length: %d", length);
  int logBufLength = ss2k_log_hex_to_buffer(pData, length, logBuf, 0, kLogBufMaxLength);

  logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, "<- %.8s | %.8s", serviceUUID.toString().c_str(), charUUID.toString().c_str());

  std::shared_ptr<SensorData> sensorData = sensorDataFactory.getSensorData(charUUID, (uint64_t)address, pData, length);

  logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " | %s[", sensorData->getId().c_str());
  if (sensorData->hasHeartRate() && !rtConfig.hr.getSimulate()) {
    int heartRate = sensorData->getHeartRate();
    rtConfig.hr.setValue(heartRate);
    spinBLEClient.connectedHR |= true;
    logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " HR(%d)", heartRate % 1000);
  }
  if (sensorData->hasCadence() && !rtConfig.cad.getSimulate()) {
    float cadence = sensorData->getCadence();
    rtConfig.cad.setValue(cadence);
    spinBLEClient.connectedCD |= true;
    logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " CD(%.2f)", fmodf(cadence, 1000.0));
  }
  if (sensorData->hasPower() && !rtConfig.watts.getSimulate()) {
    int power = sensorData->getPower() * userConfig.getPowerCorrectionFactor();
    rtConfig.watts.setValue(power);
    spinBLEClient.connectedPM |= true;
    logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " PW(%d)", power % 10000);
  }
  if (sensorData->hasSpeed()) {
    float speed = sensorData->getSpeed();
    rtConfig.setSimulatedSpeed(speed);
    logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " SD(%.2f)", fmodf(speed, 1000.0));
  }
  if (sensorData->hasResistance()) {
    int resistance = sensorData->getResistance();
    rtConfig.resistance.setValue(resistance);
    logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " RS(%d)", resistance % 1000);
  }
  strncat(logBuf + logBufLength, " ]", kLogBufMaxLength - logBufLength);
  SS2K_LOG(BLE_COMMON_LOG_TAG, "%s", logBuf);
  #ifdef USE_TELEGRAM
  SEND_TO_TELEGRAM(String(logBuf));
  #endif
}