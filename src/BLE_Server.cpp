/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "SS2KLog.h"
#include "BLE_Common.h"

#include <ArduinoJson.h>
#include <Constants.h>
#include <NimBLEDevice.h>

// BLE Server Settings
SpinBLEServer spinBLEServer;

NimBLEServer *pServer = nullptr;

static MyCallbacks chrCallbacks;

BLEService *pHeartService;
BLECharacteristic *heartRateMeasurementCharacteristic;

BLEService *pPowerMonitor;
BLECharacteristic *cyclingPowerMeasurementCharacteristic;
BLECharacteristic *cyclingPowerFeatureCharacteristic;
BLECharacteristic *sensorLocationCharacteristic;

BLEService *pFitnessMachineService;
BLECharacteristic *fitnessMachineFeature;
BLECharacteristic *fitnessMachineIndoorBikeData;
BLECharacteristic *fitnessMachineStatusCharacteristic;
BLECharacteristic *fitnessMachineControlPoint;
BLECharacteristic *fitnessMachineResistanceLevelRange;
BLECharacteristic *fitnessMachinePowerRange;
BLECharacteristic *fitnessMachineInclinationRange;
BLECharacteristic *fitnessMachineTrainingStatus;

BLEService *pSmartSpin2kService;
BLECharacteristic *smartSpin2kCharacteristic;
std::string FTMSWrite = "";

/******** Bit field Flag Example ********/
// 00000000000000000001 - 1   - 0x001 - Pedal Power Balance Present
// 00000000000000000010 - 2   - 0x002 - Pedal Power Balance Reference
// 00000000000000000100 - 4   - 0x004 - Accumulated Torque Present
// 00000000000000001000 - 8   - 0x008 - Accumulated Torque Source
// 00000000000000010000 - 16  - 0x010 - Wheel Revolution Data Present
// 00000000000000100000 - 32  - 0x020 - Crank Revolution Data Present
// 00000000000001000000 - 64  - 0x040 - Extreme Force Magnitudes Present
// 00000000000010000000 - 128 - 0x080 - Extreme Torque Magnitudes Present
// 00000000000100000000 - Extreme Angles Present (bit8)
// 00000000001000000000 - Top Dead Spot Angle Present (bit 9)
// 00000000010000000000 - Bottom Dead Spot Angle Present (bit 10)
// 00000000100000000000 - Accumulated Energy Present (bit 11)
// 00000001000000000000 - Offset Compensation Indicator (bit 12)
// 98765432109876543210 - bit placement helper :)
// 00000000001001000000
// 00000101000010000110
// 00000000100001010100
//               100000
byte heartRateMeasurement[2]    = {0x00, 0x00};
byte cyclingPowerMeasurement[9] = {0b0000000100011, 0, 200, 0, 0, 0, 0, 0, 0};
byte cpsLocation[1]             = {0b000};       // sensor location 5 == left crank
byte cpFeature[1]               = {0b00100000};  // crank information present // 3rd & 2nd
                                                 // byte is reported power

// byte ftmsService[6]       = {0x00, 0x00, 0x00, 0b01, 0b0100000, 0x00};

struct FitnessMachineFeature ftmsFeature = {
    FitnessMachineFeatureFlags::Types::CadenceSupported | FitnessMachineFeatureFlags::Types::HeartRateMeasurementSupported |
        FitnessMachineFeatureFlags::Types::PowerMeasurementSupported | FitnessMachineFeatureFlags::Types::InclinationSupported |
        FitnessMachineFeatureFlags::Types::ResistanceLevelSupported,
    FitnessMachineTargetFlags::PowerTargetSettingSupported | FitnessMachineTargetFlags::Types::InclinationTargetSettingSupported |
        FitnessMachineTargetFlags::Types::ResistanceTargetSettingSupported | FitnessMachineTargetFlags::Types::IndoorBikeSimulationParametersSupported |
        FitnessMachineTargetFlags::Types::SpinDownControlSupported | FitnessMachineTargetFlags::Types::TargetedCadenceConfigurationSupported};

uint8_t ftmsIndoorBikeData[14] = {0x44, 0x02, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};  // 00000000100001010100 ISpeed, ICAD,
                                                                                                            // TDistance, IPower, ETime
uint8_t ftmsResistanceLevelRange[6]      = {0x00, 0x01, 0x25, 0x00, 0x01, 0x00};                            // 1:37 increment 1
uint8_t ftmsPowerRange[6]                = {0x00, 0x01, 0xA0, 0x0F, 0x01, 0x00};                            // 1:4000 watts increment 1
uint8_t ftmsInclinationRange[6]          = {0x38, 0xff, 0xc8, 0x00, 0x01, 0x00};                            // -20.0:20.0 increment .1
uint8_t ftmsTrainingStatus[2]            = {0x08, 0x00};
uint8_t ss2kCustomCharacteristicValue[3] = {0x00, 0x00, 0x00};

void logCharacteristic(char *buffer, const size_t bufferCapacity, const byte *data, const size_t dataLength, const NimBLEUUID serviceUUID, const NimBLEUUID charUUID,
                       const char *format, ...) {
  int bufferLength = ss2k_log_hex_to_buffer(data, dataLength, buffer, 0, bufferCapacity);
  bufferLength += snprintf(buffer + bufferLength, bufferCapacity - bufferLength, "-> %s | %s | ", serviceUUID.toString().c_str(), charUUID.toString().c_str());
  va_list args;
  va_start(args, format);
  bufferLength += vsnprintf(buffer + bufferLength, bufferCapacity - bufferLength, format, args);
  va_end(args);

  SS2K_LOG(BLE_SERVER_LOG_TAG, "%s", buffer);
  SEND_TO_TELEGRAM(String(buffer));
}

void startBLEServer() {
  // Server Setup
  SS2K_LOG(BLE_SERVER_LOG_TAG, "Starting BLE Server");
  pServer = BLEDevice::createServer();

  // HEART RATE MONITOR SERVICE SETUP
  pHeartService                      = pServer->createService(HEARTSERVICE_UUID);
  heartRateMeasurementCharacteristic = pHeartService->createCharacteristic(HEARTCHARACTERISTIC_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  // Power Meter MONITOR SERVICE SETUP
  pPowerMonitor                         = pServer->createService(CYCLINGPOWERSERVICE_UUID);
  cyclingPowerMeasurementCharacteristic = pPowerMonitor->createCharacteristic(CYCLINGPOWERMEASUREMENT_UUID, NIMBLE_PROPERTY::NOTIFY);
  cyclingPowerFeatureCharacteristic     = pPowerMonitor->createCharacteristic(CYCLINGPOWERFEATURE_UUID, NIMBLE_PROPERTY::READ);
  sensorLocationCharacteristic          = pPowerMonitor->createCharacteristic(SENSORLOCATION_UUID, NIMBLE_PROPERTY::READ);

  // Fitness Machine service setup
  pFitnessMachineService             = pServer->createService(FITNESSMACHINESERVICE_UUID);
  fitnessMachineFeature              = pFitnessMachineService->createCharacteristic(FITNESSMACHINEFEATURE_UUID, NIMBLE_PROPERTY::READ);
  fitnessMachineControlPoint         = pFitnessMachineService->createCharacteristic(FITNESSMACHINECONTROLPOINT_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE);
  fitnessMachineStatusCharacteristic = pFitnessMachineService->createCharacteristic(FITNESSMACHINESTATUS_UUID, NIMBLE_PROPERTY::NOTIFY);
  fitnessMachineIndoorBikeData       = pFitnessMachineService->createCharacteristic(FITNESSMACHINEINDOORBIKEDATA_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  fitnessMachineResistanceLevelRange = pFitnessMachineService->createCharacteristic(FITNESSMACHINERESISTANCELEVELRANGE_UUID, NIMBLE_PROPERTY::READ);
  fitnessMachinePowerRange           = pFitnessMachineService->createCharacteristic(FITNESSMACHINEPOWERRANGE_UUID, NIMBLE_PROPERTY::READ);
  fitnessMachineInclinationRange     = pFitnessMachineService->createCharacteristic(FITNESSMACHINEINCLINATIONRANGE_UUID, NIMBLE_PROPERTY::READ);
  fitnessMachineTrainingStatus       = pFitnessMachineService->createCharacteristic(FITNESSMACHINETRAININGSTATUS_UUID, NIMBLE_PROPERTY::NOTIFY);

  pSmartSpin2kService = pServer->createService(SMARTSPIN2K_SERVICE_UUID);
  smartSpin2kCharacteristic =
      pSmartSpin2kService->createCharacteristic(SMARTSPIN2K_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE | NIMBLE_PROPERTY::NOTIFY);

  pServer->setCallbacks(new MyServerCallbacks());

  // Creating Characteristics
  heartRateMeasurementCharacteristic->setValue(heartRateMeasurement, 2);

  cyclingPowerMeasurementCharacteristic->setValue(cyclingPowerMeasurement, 9);
  cyclingPowerFeatureCharacteristic->setValue(cpFeature, 1);
  sensorLocationCharacteristic->setValue(cpsLocation, 1);

  fitnessMachineFeature->setValue(ftmsFeature.bytes, sizeof(ftmsFeature));

  fitnessMachineIndoorBikeData->setValue(ftmsIndoorBikeData, 14);
  fitnessMachineResistanceLevelRange->setValue(ftmsResistanceLevelRange, 6);
  fitnessMachinePowerRange->setValue(ftmsPowerRange, 6);
  fitnessMachineInclinationRange->setValue(ftmsInclinationRange, 6);

  smartSpin2kCharacteristic->setValue(ss2kCustomCharacteristicValue, 3);

  cyclingPowerMeasurementCharacteristic->setCallbacks(&chrCallbacks);
  heartRateMeasurementCharacteristic->setCallbacks(&chrCallbacks);
  fitnessMachineIndoorBikeData->setCallbacks(&chrCallbacks);
  fitnessMachineControlPoint->setCallbacks(&chrCallbacks);
  smartSpin2kCharacteristic->setCallbacks(new ss2kCustomCharacteristicCallbacks());

  pHeartService->start();
  pPowerMonitor->start();
  pFitnessMachineService->start();
  pSmartSpin2kService->start();

  // const std::string fitnessData = {0b00000001, 0b00100000, 0b00000000};
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  // pAdvertising->setServiceData(FITNESSMACHINESERVICE_UUID, fitnessData);
  pAdvertising->addServiceUUID(FITNESSMACHINESERVICE_UUID);
  pAdvertising->addServiceUUID(CYCLINGPOWERSERVICE_UUID);
  pAdvertising->addServiceUUID(HEARTSERVICE_UUID);
  pAdvertising->addServiceUUID(SMARTSPIN2K_SERVICE_UUID);
  pAdvertising->setMaxInterval(250);
  pAdvertising->setMinInterval(160);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  SS2K_LOG(BLE_SERVER_LOG_TAG, "Bluetooth Characteristic defined!");
}

bool spinDown() {
  std::string rxValue = fitnessMachineStatusCharacteristic->getValue();
  if (rxValue[0] != 0x14) {
    return false;
  }
  uint8_t spinStatus[2] = {0x14, 0x01};

  if (rxValue[1] == 0x01) {
    // debugDirector("Spin Down Initiated", true);
    vTaskDelay(1000 / portTICK_RATE_MS);
    spinStatus[1] = 0x04;  // send Stop Pedaling
    fitnessMachineStatusCharacteristic->setValue(spinStatus, 2);
  }
  if (rxValue[1] == 0x04) {
    // debugDirector("Stop Pedaling", true);
    vTaskDelay(1000 / portTICK_RATE_MS);
    spinStatus[1] = 0x02;  // Success
    fitnessMachineStatusCharacteristic->setValue(spinStatus, 2);
  }
  if (rxValue[1] == 0x02) {
    // debugDirector("Success", true);
    spinStatus[0] = 0x00;
    spinStatus[1] = 0x00;  // Success
    fitnessMachineStatusCharacteristic->setValue(spinStatus, 2);
    uint8_t returnValue[3] = {0x00, 0x00, 0x00};
    fitnessMachineControlPoint->setValue(returnValue, 3);
    fitnessMachineControlPoint->indicate();
  }

  fitnessMachineStatusCharacteristic->notify();

  return true;
}

void updateIndoorBikeDataChar() {
  if (!spinBLEServer.clientSubscribed.IndoorBikeData) {
    return;
  }
  float cadRaw = rtConfig.getSimulatedCad();
  int cad      = static_cast<int>(cadRaw * 2);

  int watts = rtConfig.getSimulatedWatts().value;
  int hr    = rtConfig.getSimulatedHr();

  int speed      = 0;
  float speedRaw = rtConfig.getSimulatedSpeed();
  if (speedRaw <= 0) {
    float gearRatio = 1;
    speed           = ((cad * 2.75 * 2.08 * 60 * gearRatio) / 10);
  } else {
    speed = static_cast<int>(speedRaw * 100);
  }
  ftmsIndoorBikeData[2] = (uint8_t)(speed & 0xff);
  ftmsIndoorBikeData[3] = (uint8_t)(speed >> 8);
  ftmsIndoorBikeData[4] = (uint8_t)(cad & 0xff);
  ftmsIndoorBikeData[5] = (uint8_t)(cad >> 8);  // cadence value
  ftmsIndoorBikeData[6] = (uint8_t)(watts & 0xff);
  ftmsIndoorBikeData[7] = (uint8_t)(watts >> 8);  // power value, constrained to avoid negative values,
                                                  // although the specification allows for a sint16
  ftmsIndoorBikeData[8] = (uint8_t)hr;

  fitnessMachineIndoorBikeData->setValue(ftmsIndoorBikeData, 9);
  fitnessMachineIndoorBikeData->notify();

  const int kLogBufCapacity = 200;  // Data(30), Sep(data/2), Arrow(3), CharId(37), Sep(3), CharId(37), Sep(3), Name(10), Prefix(2), HR(7), SEP(1), CD(10), SEP(1), PW(8), SEP(1),
                                    // SD(7), Suffix(2), Nul(1), rounded up
  char logBuf[kLogBufCapacity];
  const size_t ftmsIndoorBikeDataLength = sizeof(ftmsIndoorBikeData) / sizeof(ftmsIndoorBikeData[0]);
  logCharacteristic(logBuf, kLogBufCapacity, ftmsIndoorBikeData, ftmsIndoorBikeDataLength, FITNESSMACHINESERVICE_UUID, fitnessMachineIndoorBikeData->getUUID(),
                    "FTMS(IBD)[ HR(%d) CD(%.2f) PW(%d) SD(%.2f) ]", hr % 1000, fmodf(cadRaw, 1000.0), watts % 10000, fmodf(speed, 1000.0));
}

void updateCyclingPowerMeasurementChar() {
  if (!spinBLEServer.clientSubscribed.CyclingPowerMeasurement) {
    return;
  }
  int power = rtConfig.getSimulatedWatts().value;
  int remainder, quotient;
  quotient                   = power / 256;
  remainder                  = power % 256;
  cyclingPowerMeasurement[2] = remainder;
  cyclingPowerMeasurement[3] = quotient;
  cyclingPowerMeasurementCharacteristic->setValue(cyclingPowerMeasurement, 9);

  float cadence = rtConfig.getSimulatedCad();
  if (cadence > 0) {
    float crankRevPeriod = (60 * 1024) / cadence;
    spinBLEClient.cscCumulativeCrankRev++;
    spinBLEClient.cscLastCrankEvtTime += crankRevPeriod;
    int remainder, quotient;
    quotient                   = spinBLEClient.cscCumulativeCrankRev / 256;
    remainder                  = spinBLEClient.cscCumulativeCrankRev % 256;
    cyclingPowerMeasurement[5] = remainder;
    cyclingPowerMeasurement[6] = quotient;
    quotient                   = spinBLEClient.cscLastCrankEvtTime / 256;
    remainder                  = spinBLEClient.cscLastCrankEvtTime % 256;
    cyclingPowerMeasurement[7] = remainder;
    cyclingPowerMeasurement[8] = quotient;
  }

  cyclingPowerMeasurementCharacteristic->notify();

  const int kLogBufCapacity =
      150;  // Data(18), Sep(data/2), Arrow(3), CharId(37), Sep(3), CharId(37), Sep(3),Name(8), Prefix(2), CD(10), SEP(1), PW(8), Suffix(2), Nul(1), rounded up
  char logBuf[kLogBufCapacity];
  const size_t cyclingPowerMeasurementLength = sizeof(cyclingPowerMeasurement) / sizeof(cyclingPowerMeasurement[0]);
  logCharacteristic(logBuf, kLogBufCapacity, cyclingPowerMeasurement, cyclingPowerMeasurementLength, FITNESSMACHINESERVICE_UUID, fitnessMachineIndoorBikeData->getUUID(),
                    "CPS(CPM)[ CD(%.2f) PW(%d) ]", cadence > 0 ? fmodf(cadence, 1000.0) : 0, power % 10000);
}

void updateHeartRateMeasurementChar() {
  if (!spinBLEServer.clientSubscribed.Heartrate) {
    return;
  }
  int hr                  = rtConfig.getSimulatedHr();
  heartRateMeasurement[1] = hr;
  heartRateMeasurementCharacteristic->setValue(heartRateMeasurement, 2);
  heartRateMeasurementCharacteristic->notify();

  const int kLogBufCapacity = 125;  // Data(10), Sep(data/2), Arrow(3), CharId(37), Sep(3), CharId(37), Sep(3), Name(8), Prefix(2), HR(7), Suffix(2), Nul(1), rounded up
  char logBuf[kLogBufCapacity];
  const size_t heartRateMeasurementLength = sizeof(heartRateMeasurement) / sizeof(heartRateMeasurement[0]);
  logCharacteristic(logBuf, kLogBufCapacity, heartRateMeasurement, heartRateMeasurementLength, HEARTSERVICE_UUID, heartRateMeasurementCharacteristic->getUUID(),
                    "HRS(HRM)[ HR(%d) ]", hr % 1000);
}

// Creating Server Connection Callbacks
void MyServerCallbacks::onConnect(BLEServer *pServer, ble_gap_conn_desc *desc) {
  SS2K_LOG(BLE_SERVER_LOG_TAG, "Bluetooth Remote Client Connected: %s Connected Clients: %d", NimBLEAddress(desc->peer_ota_addr).toString().c_str(), pServer->getConnectedCount());

  if (pServer->getConnectedCount() < CONFIG_BT_NIMBLE_MAX_CONNECTIONS - NUM_BLE_DEVICES) {
    BLEDevice::startAdvertising();
  } else {
    SS2K_LOG(BLE_SERVER_LOG_TAG, "Max Remote Client Connections Reached");
    BLEDevice::stopAdvertising();
  }
}

void MyServerCallbacks::onDisconnect(BLEServer *pServer) {
  SS2K_LOG(BLE_SERVER_LOG_TAG, "Bluetooth Remote Client Disconnected. Remaining Clients: %d", pServer->getConnectedCount());
  BLEDevice::startAdvertising();
}

void MyCallbacks::onWrite(BLECharacteristic *pCharacteristic) { FTMSWrite = pCharacteristic->getValue(); }

void MyCallbacks::onSubscribe(NimBLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc, uint16_t subValue) {
  String str        = "Client ID: ";
  NimBLEUUID pUUID = pCharacteristic->getUUID();
  str += desc->conn_handle;
  str += " Address: ";
  str += std::string(NimBLEAddress(desc->peer_ota_addr)).c_str();
  if (subValue == 0) {
    str += " Unsubscribed to ";
    spinBLEServer.setClientSubscribed(pUUID, false);
  } else if (subValue == 1) {
    str += " Subscribed to notifications for ";
    spinBLEServer.setClientSubscribed(pUUID, true);
  } else if (subValue == 2) {
    str += " Subscribed to indications for ";
    spinBLEServer.setClientSubscribed(pUUID, true);
  } else if (subValue == 3) {
    str += " Subscribed to notifications and indications for ";
    spinBLEServer.setClientSubscribed(pUUID, true);
  }
  str += std::string(pCharacteristic->getUUID()).c_str();

  SS2K_LOG(BLE_SERVER_LOG_TAG, "%s", str.c_str());
}

void SpinBLEServer::setClientSubscribed(NimBLEUUID pUUID, bool subscribe) {
  if (pUUID == HEARTCHARACTERISTIC_UUID) {
    spinBLEServer.clientSubscribed.Heartrate = subscribe;
  } else if (pUUID == CYCLINGPOWERMEASUREMENT_UUID) {
    spinBLEServer.clientSubscribed.CyclingPowerMeasurement = subscribe;
  } else if (pUUID == FITNESSMACHINEINDOORBIKEDATA_UUID) {
    spinBLEServer.clientSubscribed.IndoorBikeData = subscribe;
  }
}

void processFTMSWrite() {
  if (FTMSWrite == "") {
    return;
  }
  BLECharacteristic *pCharacteristic = NimBLEDevice::getServer()->getServiceByUUID(FITNESSMACHINESERVICE_UUID)->getCharacteristic(FITNESSMACHINECONTROLPOINT_UUID);

  std::string rxValue = FTMSWrite;
  std::vector<uint8_t> ftmsStatus;
  if (rxValue.length() >= 1) {
    uint8_t *pData = reinterpret_cast<uint8_t *>(&rxValue[0]);
    int length     = rxValue.length();

    const int kLogBufCapacity = (rxValue.length() * 2) + 60;  // largest comment is 48 VV
    char logBuf[kLogBufCapacity];
    int logBufLength       = ss2k_log_hex_to_buffer(pData, length, logBuf, 0, kLogBufCapacity);
    int port               = 0;
    uint8_t returnValue[3] = {FitnessMachineControlPointProcedure::ResponseCode, (uint8_t)rxValue[0], FitnessMachineControlPointResultCode::OpCodeNotSupported};

    ftmsStatus = {FitnessMachineStatus::ReservedForFutureUse};

    switch ((uint8_t)rxValue[0]) {
      case FitnessMachineControlPointProcedure::RequestControl:
        returnValue[2] = FitnessMachineControlPointResultCode::Success;  // 0x01;
        pCharacteristic->setValue(returnValue, 3);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Control Request");
        ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Idle;  // 0x01;
        fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
        ftmsStatus = {FitnessMachineStatus::StartedOrResumedByUser};
        break;

      case FitnessMachineControlPointProcedure::Reset: {
        returnValue[2] = FitnessMachineControlPointResultCode::Success;  // 0x01;
        pCharacteristic->setValue(returnValue, 3);

        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Reset");
        ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Idle;  // 0x01;
        ftmsStatus            = {FitnessMachineStatus::Reset};
        fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
      } break;

      case FitnessMachineControlPointProcedure::SetTargetInclination: {
        returnValue[2] = FitnessMachineControlPointResultCode::Success;  // 0x01;
        pCharacteristic->setValue(returnValue, 3);

        port = (rxValue[2] << 8) + rxValue[1];
        port *= 10;

        rtConfig.setTargetIncline(port);
        rtConfig.setERGMode(false);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Incline Mode: %2f", rtConfig.getTargetIncline() / 100);

        ftmsStatus            = {FitnessMachineStatus::TargetInclineChanged, (uint8_t)rxValue[1], (uint8_t)rxValue[2]};
        ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x00;
        fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
      } break;

      case FitnessMachineControlPointProcedure::SetTargetResistanceLevel: {
        returnValue[2] = FitnessMachineControlPointResultCode::Success;  // 0x01;
        pCharacteristic->setValue(returnValue, 3);

        int targetResistance = rxValue[1];
        rtConfig.setShifterPosition(targetResistance);
        rtConfig.setERGMode(false);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Resistance Mode: %d", rtConfig.getShifterPosition());

        ftmsStatus            = {FitnessMachineStatus::TargetResistanceLevelChanged, rxValue[1]};
        ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x00;
        fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
      } break;

      case FitnessMachineControlPointProcedure::SetTargetPower: {
        if (spinBLEClient.connectedPM || rtConfig.getSimulateWatts()) {
          returnValue[2] = FitnessMachineControlPointResultCode::Success;  // 0x01;

          int targetWatts = bytes_to_u16(rxValue[2], rxValue[1]);
          rtConfig.setERGMode(true);
          rtConfig.setTargetWatts(targetWatts);
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> ERG Mode Target: %d Current: %d Incline: %2f", targetWatts,
                                   rtConfig.getSimulatedWatts().value, rtConfig.getTargetIncline() / 100);

          ftmsStatus            = {FitnessMachineStatus::TargetPowerChanged, (uint8_t)rxValue[1], (uint8_t)rxValue[2]};
          ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::WattControl;  // 0x0C;
          fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
        } else {
          returnValue[2] = FitnessMachineControlPointResultCode::OpCodeNotSupported;  // 0x02; no power meter connected, so no ERG
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> ERG Mode: No Power Meter Connected");
        }
        pCharacteristic->setValue(returnValue, 3);
      } break;

      case FitnessMachineControlPointProcedure::StartOrResume: {
        returnValue[2] = FitnessMachineControlPointResultCode::Success;  // 0x01;
        pCharacteristic->setValue(returnValue, 3);

        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Start Training");
        ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x00;
        fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
        ftmsStatus = {FitnessMachineStatus::StartedOrResumedByUser};
      } break;

      case FitnessMachineControlPointProcedure::StopOrPause: {
        returnValue[2] = FitnessMachineControlPointResultCode::Success;  // 0x01;
        pCharacteristic->setValue(returnValue, 3);
        // rxValue[1] == 1 -> Stop, 2 -> Pause
        // TODO: Move stepper to Min Position
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Stop Training");

        ftmsStatus            = {FitnessMachineStatus::StoppedOrPausedByUser};
        ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x00;
        fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);

      } break;

      case FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters: {  // sim mode
        returnValue[2] = FitnessMachineControlPointResultCode::Success;               // 0x01;
        pCharacteristic->setValue(returnValue, 3);

        signed char buf[2];
        // int16_t windSpeed        = (rxValue[2] << 8) + rxValue[1];
        buf[0] = rxValue[3];  // (Least significant byte)
        buf[1] = rxValue[4];  // (Most significant byte)
        // int8_t rollingResistance = rxValue[5];
        // int8_t windResistance    = rxValue[6];
        port = bytes_to_u16(buf[1], buf[0]);
        rtConfig.setTargetIncline(port);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Sim Mode Incline %2f", rtConfig.getTargetIncline() / 100);
        rtConfig.setERGMode(false);

        ftmsStatus = {FitnessMachineStatus::IndoorBikeSimulationParametersChanged,
                      (uint8_t)rxValue[1],
                      (uint8_t)rxValue[2],
                      (uint8_t)rxValue[3],
                      (uint8_t)rxValue[4],
                      (uint8_t)rxValue[5],
                      (uint8_t)rxValue[6]};

        ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x00;
        fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
      } break;

      case FitnessMachineControlPointProcedure::SpinDownControl: {
        uint8_t controlPoint[6] = {FitnessMachineControlPointProcedure::ResponseCode, 0x01, 0x24, 0x03, 0x96, 0x0e};  // send low and high speed targets
        pCharacteristic->setValue(controlPoint, 6);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Spin Down Requested");

        ftmsStatus = {FitnessMachineStatus::SpinDownStatus, 0x01};  // send low and high speed targets

        ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x00;
        fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
      } break;

      case FitnessMachineControlPointProcedure::SetTargetedCadence: {
        returnValue[2] = FitnessMachineControlPointResultCode::Success;  // 0x01;
        pCharacteristic->setValue(returnValue, 3);

        int targetCadence = bytes_to_u16(rxValue[2], rxValue[1]);
        // rtConfig.setTargetCadence(targetCadence);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Target Cadence: %d ", targetCadence);

        ftmsStatus = {FitnessMachineStatus::TargetedCadenceChanged, (uint8_t)rxValue[1], (uint8_t)rxValue[2]};

        ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x00;
        fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
      } break;

      default: {
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Unsupported FTMS Request");
        pCharacteristic->setValue(returnValue, 3);
      }
    }
    SS2K_LOG(FMTS_SERVER_LOG_TAG, "%s", logBuf);

  } else {
    SS2K_LOG(FMTS_SERVER_LOG_TAG, "App wrote nothing ");
    SS2K_LOG(FMTS_SERVER_LOG_TAG, "assuming it's a Control request");

    uint8_t controlPoint[3] = {FitnessMachineControlPointProcedure::ResponseCode, 0x00, FitnessMachineControlPointResultCode::Success};
    pCharacteristic->setValue(controlPoint, 3);
    ftmsStatus            = {FitnessMachineStatus::StartedOrResumedByUser};
    ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x00;
    fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
    fitnessMachineTrainingStatus->notify(false);
  }
  for (int i = 0; i < ftmsStatus.size(); i++) {
  }
  fitnessMachineStatusCharacteristic->setValue(ftmsStatus.data(), ftmsStatus.size());
  pCharacteristic->indicate();
  fitnessMachineTrainingStatus->notify(false);
  fitnessMachineStatusCharacteristic->notify(false);
  FTMSWrite = "";
}

void controlPointIndicate() { fitnessMachineControlPoint->indicate(); }

// Return number of clients connected to our server.
int connectedClientCount() {
  if (BLEDevice::getServer()) {
    return BLEDevice::getServer()->getConnectedCount();
  } else {
    return 0;
  }
}

void calculateInstPwrFromHR() {
  static int oldHR    = rtConfig.getSimulatedHr();
  static int newHR    = rtConfig.getSimulatedHr();
  static double delta = 0;
  oldHR               = newHR;  // Copying HR from Last loop
  newHR               = rtConfig.getSimulatedHr();

  delta = (newHR - oldHR) / (BLE_CLIENT_DELAY / 1000);

  // userConfig.setSimulatedWatts((s1Pwr*s2HR)-(s2Pwr*S1HR))/(S2HR-s1HR)+(userConfig.getSimulatedHr(*((s1Pwr-s2Pwr)/(s1HR-s2HR)));
  int avgP = ((userPWC.session1Pwr * userPWC.session2HR) - (userPWC.session2Pwr * userPWC.session1HR)) / (userPWC.session2HR - userPWC.session1HR) +
             (newHR * ((userPWC.session1Pwr - userPWC.session2Pwr) / (userPWC.session1HR - userPWC.session2HR)));

  if (avgP < 50) {
    avgP = 50;
  }

  if (delta < 0) {
    // magic math here for inst power
  }

  if (delta > 0) {
    // magic math here for inst power
  }

#ifndef DEBUG_HR_TO_PWR
  rtConfig.setSimulatedWatts(avgP);
  rtConfig.setSimulatedCad(90);
#endif  // DEBUG_HR_TO_PWR

  SS2K_LOG(BLE_SERVER_LOG_TAG, "Power From HR: %d", avgP);
}

/*
Custom Characteristic for userConfig Variable manipulation via BLE

An example follows to read/write 26.3kph to simulatedSpeed:
simulatedSpeed is a float and first needs to be converted to int by *10 for transmission, so convert 26.3kph to 263 (multiply by 10)
Decimal 263 == hexidecimal 0107 but the data needs to be converted to LSO, MSO to match the rest of the BLE spec so 263 == 0x07, 0x01 (LSO,MSO)

So,

If client wants to write (0x02) int value 263 (0x07 0x01) to simulatedSpeed(0x06):

Client Writes:
0x02, 0x06, 0x07, 0x01
(operator, variable, LSO, MSO)

Server will then indicate:
0x80, 0x06, 0x07, 0x01
(status, variable, LSO, MSO)

Example to read (0x01) from simulatedSpeed (0x06)

Client Writes:
0x01, 0x06
Server will then indicate:
0x80, 0x06, 0x07, 0x01
success, simulatedSpeed,0x07,0x01

Pay special attention to the float values below. Since they have to be transmitted as an int, some are converted *100, others are converted *10.
True values are >00. False are 00.
*/

void ss2kCustomCharacteristicCallbacks::onWrite(BLECharacteristic *pCharacteristic) {
  std::string rxValue = pCharacteristic->getValue();
  uint8_t read        = 0x01;  // value to request read operation
  uint8_t write       = 0x02;  // Value to request write operation
  uint8_t error       = 0xff;  // value server error/unable
  uint8_t success     = 0x80;  // value for success

  uint8_t *pData = reinterpret_cast<uint8_t *>(&rxValue[0]);
  int length     = rxValue.length();

  const int kLogBufCapacity = (rxValue.length() * 2) + 60;  // needs to be bigger than the largest message.
  char logBuf[kLogBufCapacity];
  int logBufLength = ss2k_log_hex_to_buffer(pData, length, logBuf, 0, kLogBufCapacity);

  size_t returnLength = rxValue.length();
  uint8_t returnValue[returnLength];
  returnValue[0] = error;
  for (size_t i = 1; i < returnLength; i++) {
    returnValue[i] = rxValue[i];
  }

  switch (rxValue[1]) {
    case BLE_firmwareUpdateURL:  // 0x01
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-Firmware Update URL");
      returnValue[0] = error;
      break;

    case BLE_incline: {  // 0x02
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-incline");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        int inc        = rtConfig.getTargetIncline() * 100;
        returnValue[2] = (uint8_t)(inc & 0xff);
        returnValue[3] = (uint8_t)(inc >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        rtConfig.setTargetIncline(bytes_to_u16(rxValue[3], rxValue[2]) / 100);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%f)", rtConfig.getTargetIncline());
      }
    } break;

    case BLE_simulatedWatts:  // 0x03
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-simulatedWatts");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(rtConfig.getSimulatedWatts().value & 0xff);
        returnValue[3] = (uint8_t)(rtConfig.getSimulatedWatts().value >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        rtConfig.setSimulatedWatts(bytes_to_u16(rxValue[3], rxValue[2]));
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%d)", rtConfig.getSimulatedWatts());
      }
      break;

    case BLE_simulatedHr:  // 0x04
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-simulatedHr");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(rtConfig.getSimulatedHr() & 0xff);
        returnValue[3] = (uint8_t)(rtConfig.getSimulatedHr() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        rtConfig.setSimulatedHr(bytes_to_u16(rxValue[3], rxValue[2]));
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%d)", rtConfig.getSimulatedHr());
      }
      break;

    case BLE_simulatedCad:  // 0x05
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-simulatedCad");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(rtConfig.getSimulatedCad() & 0xff);
        returnValue[3] = (uint8_t)(rtConfig.getSimulatedCad() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        rtConfig.setSimulatedCad(bytes_to_u16(rxValue[3], rxValue[2]));
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%d)", rtConfig.getSimulatedCad());
      }
      break;

    case BLE_simulatedSpeed: {  // 0x06
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-simulatedSpeed");
      returnValue[0] = success;
      int spd        = rtConfig.getSimulatedSpeed() * 10;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(spd & 0xff);
        returnValue[3] = (uint8_t)(spd >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        rtConfig.setSimulatedSpeed(bytes_to_u16(rxValue[3], rxValue[2]) / 10);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%d)", rtConfig.getSimulatedSpeed());
      }
    } break;

    case BLE_deviceName:  // 0x07
      logBufLength   = snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-deviceName");
      returnValue[0] = error;
      break;

    case BLE_shiftStep:  // 0x08
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-shiftStep");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig.getShiftStep() & 0xff);
        returnValue[3] = (uint8_t)(userConfig.getShiftStep() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        userConfig.setShiftStep(bytes_to_u16(rxValue[3], rxValue[2]));
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%d)", userConfig.getShiftStep());
      }
      break;

    case BLE_stepperPower:  // 0x09
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-stepperPower");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig.getStepperPower() & 0xff);
        returnValue[3] = (uint8_t)(userConfig.getStepperPower() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        userConfig.setStepperPower(bytes_to_u16(rxValue[3], rxValue[2]));
        ss2k.updateStepperPower();
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%d)", userConfig.getStepperPower());
      }
      break;

    case BLE_stealthchop:  // 0x0A
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-stealthchop");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig.getStealthchop());
        returnLength += 1;
      }
      if (rxValue[0] == write) {
        userConfig.setStealthChop(rxValue[2]);
        ss2k.updateStealthchop();
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", userConfig.getStealthchop() ? "true" : "false");
      }
      break;

    case BLE_inclineMultiplier: {  // 0x0B
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-inclineMultiplier");
      returnValue[0] = success;
      int inc        = userConfig.getInclineMultiplier();
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(inc & 0xff);
        returnValue[3] = (uint8_t)(inc >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        userConfig.setInclineMultiplier(bytes_to_u16(rxValue[3], rxValue[2]));
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%f)", userConfig.getInclineMultiplier());
      }
    } break;

    case BLE_powerCorrectionFactor: {  // 0x0C
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-powerCorrectionFactor");
      returnValue[0] = success;
      int pcf        = userConfig.getPowerCorrectionFactor() * 10;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(pcf & 0xff);
        returnValue[3] = (uint8_t)(pcf >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        userConfig.setPowerCorrectionFactor(bytes_to_u16(rxValue[3], rxValue[2]) / 10);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%f)", userConfig.getPowerCorrectionFactor());
      }
    } break;

    case BLE_simulateHr:  // 0x0D
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-simulateHr");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(rtConfig.getSimulateHr());
        returnLength += 1;
      }
      if (rxValue[0] == write) {
        rtConfig.setSimulateHr(rxValue[2]);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", rtConfig.getSimulateHr() ? "true" : "false");
      }
      break;

    case BLE_simulateWatts:  // 0x0E
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-simulateWatts");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(rtConfig.getSimulateWatts());
        returnLength += 1;
      }
      if (rxValue[0] == write) {
        rtConfig.setSimulateWatts(rxValue[2]);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", rtConfig.getSimulateWatts() ? "true" : "false");
      }
      break;

    case BLE_simulateCad:  // 0x0F
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-simulateCad");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(rtConfig.getSimulateCad());
        returnLength += 1;
      }
      if (rxValue[0] == write) {
        rtConfig.setSimulateCad(rxValue[2]);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", rtConfig.getSimulateCad() ? "true" : "false");
      }
      break;

    case BLE_ERGMode:  // 0x10
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-ERGMode");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(rtConfig.getERGMode());
        returnLength += 1;
      }
      if (rxValue[0] == write) {
        rtConfig.setERGMode(rxValue[2]);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", rtConfig.getERGMode() ? "true" : "false");
      }
      break;

    case BLE_autoUpdate:  // 0x11
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-autoUpdate");

      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig.getAutoUpdate());
        returnLength += 1;
      }
      if (rxValue[0] == write) {
        userConfig.setAutoUpdate(rxValue[2]);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", userConfig.getAutoUpdate() ? "true" : "false");
      }
      break;

    case BLE_ssid:  // 0x12
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-ssid");

      returnValue[0] = error;
      break;

    case BLE_password:  // 0x13
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-password");
      returnValue[0] = error;
      break;

    case BLE_foundDevices:  // 0x14
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-foundDevices");
      returnValue[0] = error;
      break;

    case BLE_connectedPowerMeter:  // 0x15
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-connectedPowerMete");
      returnValue[0] = error;
      break;

    case BLE_connectedHeartMonitor:  // 0x16
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-connectedHeartMonitor");
      returnValue[0] = error;
      break;

    case BLE_shifterPosition:  // 0x17
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-shifterPosition");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(rtConfig.getShifterPosition() & 0xff);
        returnValue[3] = (uint8_t)(rtConfig.getShifterPosition() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        rtConfig.setShifterPosition(bytes_to_u16(rxValue[3], rxValue[2]));
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%d)", rtConfig.getShifterPosition());
        SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "%s", logBuf);
        return;  // Return here and let SpinBLEServer::notifyShift() handle the return to prevent duplicate notifications.
      }
      break;

    case BLE_saveToLittleFS:  // 0x18
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-saveToLittleFS");
      userConfig.saveToLittleFS();
      returnValue[0] = success;
      break;

    case BLE_targetPosition:  // 0x19
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-targetPosition");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(ss2k.targetPosition & 0xff);
        returnValue[3] = (uint8_t)(ss2k.targetPosition >> 8);
        returnValue[4] = (uint8_t)(ss2k.targetPosition >> 16);
        returnValue[5] = (uint8_t)(ss2k.targetPosition >> 24);
        returnLength += 4;
      }
      if (rxValue[0] == write) {
        ss2k.targetPosition = (int32_t((uint8_t)(rxValue[2]) << 0 | (uint8_t)(rxValue[3]) << 8 | (uint8_t)(rxValue[4]) << 16 | (uint8_t)(rxValue[5]) << 24));
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, " (%f)", ss2k.targetPosition);
      }
      break;

    case BLE_externalControl:  // 0x1A
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-externalControl");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(ss2k.externalControl);
        returnLength += 1;
      }
      if (rxValue[0] == write) {
        ss2k.externalControl = static_cast<bool>(rxValue[2]);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%B)", ss2k.externalControl);
      }
      break;

    case BLE_syncMode:  // 0x1B
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-syncMode");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(ss2k.syncMode);
        returnLength += 1;
      }
      if (rxValue[0] == write) {
        ss2k.syncMode = static_cast<bool>(rxValue[2]);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", ss2k.syncMode ? "true" : "false");
      }
      break;
  }
  SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "%s", logBuf);
  pCharacteristic->setValue(returnValue, returnLength);
  pCharacteristic->indicate();
}

void SpinBLEServer::notifyShift() {
  uint8_t returnValue[4];
  returnValue[0] = 0x80;
  returnValue[1] = BLE_shifterPosition;
  returnValue[2] = (uint8_t)(rtConfig.getShifterPosition() & 0xff);
  returnValue[3] = (uint8_t)(rtConfig.getShifterPosition() >> 8);
  smartSpin2kCharacteristic->setValue(returnValue, 4);
  smartSpin2kCharacteristic->notify(true);
}
