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
BLECharacteristic *fitnessMachineTrainingStatus;

BLEService *pSmartSpin2kService;
BLECharacteristic *smartSpin2kCharacteristic;

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
byte ftmsControlPoint[3]  = {0x00, 0x00, 0x00};                          // 0x08 we need to return a value of 1 for any successful change
byte ftmsMachineStatus[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};  // 0x04 means started by the user

struct FitnessMachineFeature ftmsFeature = {FitnessMachineFeatureFlags::Types::CadenceSupported | FitnessMachineFeatureFlags::Types::HeartRateMeasurementSupported |
                                                FitnessMachineFeatureFlags::Types::PowerMeasurementSupported | FitnessMachineFeatureFlags::Types::InclinationSupported |
                                                FitnessMachineFeatureFlags::Types::ResistanceLevelSupported,
                                            FitnessMachineTargetFlags::PowerTargetSettingSupported | FitnessMachineTargetFlags::Types::InclinationTargetSettingSupported |
                                                FitnessMachineTargetFlags::Types::ResistanceTargetSettingSupported |
                                                FitnessMachineTargetFlags::Types::IndoorBikeSimulationParametersSupported |
                                                FitnessMachineTargetFlags::Types::SpinDownControlSupported};

uint8_t ftmsIndoorBikeData[14] = {0x44, 0x02, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};  // 00000000100001010100 ISpeed, ICAD,
                                                                                                            // TDistance, IPower, ETime
uint8_t ftmsResistanceLevelRange[6]      = {0x00, 0x00, 0x25, 0x00, 0x01, 0x00};                            // 25 increment 1
uint8_t ftmsPowerRange[6]                = {0x00, 0x00, 0xA0, 0x0F, 0x01, 0x00};                            // 1-4000 watts
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
  fitnessMachineControlPoint->setValue(ftmsControlPoint, 3);

  fitnessMachineIndoorBikeData->setValue(ftmsIndoorBikeData, 14);
  fitnessMachineStatusCharacteristic->setValue(ftmsMachineStatus, 7);
  fitnessMachineResistanceLevelRange->setValue(ftmsResistanceLevelRange, 6);
  fitnessMachinePowerRange->setValue(ftmsPowerRange, 6);

  smartSpin2kCharacteristic->setValue(ss2kCustomCharacteristicValue, 3);

  fitnessMachineControlPoint->setCallbacks(new MyCallbacks());

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

// as a note, Trainer Road sends 50w target whenever the app is connected.
void computeERG(int newSetPoint) {

  if (userConfig.getERGMode() && spinBLEClient.connectedPM) {
    // continue
  } else {
    return;
  }
  static int setPoint       = 0;
  float incline             = userConfig.getIncline();
  int newIncline            = incline;
  int amountToChangeIncline = 0;
  const int wattChange        = userConfig.getSimulatedWatts() - setPoint;

  if (newSetPoint > 0) {  // only update the value if new value is sent
    setPoint = newSetPoint;
    return;
  }
  if (setPoint == 0) {
    return;
  }

  if (userConfig.getSimulatedCad() <= 20) {
    // Cadence too low, nothing to do here
    return;
  }
  amountToChangeIncline = wattChange * userConfig.getERGSensitivity();
  if (abs(wattChange) < WATTS_PER_SHIFT) {
    // As the desired value gets closer, make smaller changes for a smoother experience
    amountToChangeIncline *= SUB_SHIFT_SCALE;
  }
  // limit to 10 shifts at a time
  if (abs(amountToChangeIncline) > userConfig.getShiftStep() * 10) {
    if (amountToChangeIncline > 0) {
      amountToChangeIncline = userConfig.getShiftStep() * 10;
    }
    if (amountToChangeIncline < 0) {
      amountToChangeIncline = -(userConfig.getShiftStep() * 10);
    }

  } else {
    return;
  }
  // Reduce the amount per loop (don't try to oneshot it.) and reduce the movement the higher the watt target is.
  amountToChangeIncline = amountToChangeIncline / ((currentWatts / 100) + .1);  // +.1 to eliminate possible divide by zero.

  newIncline = incline - amountToChangeIncline;  //  }
  userConfig.setIncline(newIncline);
}

void updateIndoorBikeDataChar() {
  float cadRaw = userConfig.getSimulatedCad();
  int cad      = static_cast<int>(cadRaw * 2);

  int watts = userConfig.getSimulatedWatts();
  int hr    = userConfig.getSimulatedHr();

  int speed      = 0;
  float speedRaw = userConfig.getSimulatedSpeed();
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
}  // ^^Using the New Way of setting Bytes.

void updateCyclingPowerMeasurementChar() {
  int power = userConfig.getSimulatedWatts();
  int remainder, quotient;
  quotient                   = power / 256;
  remainder                  = power % 256;
  cyclingPowerMeasurement[2] = remainder;
  cyclingPowerMeasurement[3] = quotient;
  cyclingPowerMeasurementCharacteristic->setValue(cyclingPowerMeasurement, 9);

  float cadence = userConfig.getSimulatedCad();
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
  }  // ^^Using the old way of setting bytes because I like it and it makes more sense to me looking at it.

  cyclingPowerMeasurementCharacteristic->notify();

  const int kLogBufCapacity =
      150;  // Data(18), Sep(data/2), Arrow(3), CharId(37), Sep(3), CharId(37), Sep(3),Name(8), Prefix(2), CD(10), SEP(1), PW(8), Suffix(2), Nul(1), rounded up
  char logBuf[kLogBufCapacity];
  const size_t cyclingPowerMeasurementLength = sizeof(cyclingPowerMeasurement) / sizeof(cyclingPowerMeasurement[0]);
  logCharacteristic(logBuf, kLogBufCapacity, cyclingPowerMeasurement, cyclingPowerMeasurementLength, FITNESSMACHINESERVICE_UUID, fitnessMachineIndoorBikeData->getUUID(),
                    "CPS(CPM)[ CD(%.2f) PW(%d) ]", cadence > 0 ? fmodf(cadence, 1000.0) : 0, power % 10000);
}

void updateHeartRateMeasurementChar() {
  int hr                  = userConfig.getSimulatedHr();
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
  updateConnParametersFlag = true;
  bleConnDesc              = desc->conn_handle;

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

void MyCallbacks::onWrite(BLECharacteristic *pCharacteristic) {
  std::string rxValue = pCharacteristic->getValue();

  if (rxValue.length() > 1) {
    uint8_t *pData = reinterpret_cast<uint8_t *>(&rxValue[0]);
    int length     = rxValue.length();

    const int kLogBufCapacity = (rxValue.length() * 2) + 60;  // largest comment is 48 VV
    char logBuf[kLogBufCapacity];
    int logBufLength = ss2k_log_hex_to_buffer(pData, length, logBuf, 0, kLogBufCapacity);

    int port               = 0;
    uint8_t returnValue[3] = {0x80, (uint8_t)rxValue[0], 0x02};

    switch ((uint8_t)rxValue[0]) {
      case 0x00:  // request control
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Control Request");
        returnValue[2] = 0x01;
        userConfig.setERGMode(false);
        pCharacteristic->setValue(returnValue, 3);
        ftmsTrainingStatus[1] = 0x01;
        fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
        fitnessMachineTrainingStatus->notify();
        pCharacteristic->setValue(returnValue, 3);
        break;

      case 0x03: {  // inclination level setting - differs from sim mode as no negative numbers
        port = (rxValue[2] << 8) + rxValue[1];
        port *= 10;
        userConfig.setIncline(port);
        userConfig.setERGMode(false);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Incline Mode: %2f", userConfig.getIncline() / 100);
        returnValue[2]           = 0x01;
        uint8_t inclineStatus[3] = {0x06, (uint8_t)rxValue[1], (uint8_t)rxValue[2]};
        fitnessMachineStatusCharacteristic->setValue(inclineStatus, 3);
        pCharacteristic->setValue(returnValue, 3);
        ftmsTrainingStatus[1] = 0x00;
        fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
        fitnessMachineTrainingStatus->notify();
      } break;

      case 0x04: {  // Resistance level setting
        int targetResistance = rxValue[1];
        userConfig.setShifterPosition(targetResistance);
        userConfig.setERGMode(false);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Resistance Mode: %d", userConfig.getShifterPosition());
        returnValue[2]              = 0x01;
        uint8_t resistanceStatus[2] = {0x07, rxValue[1]};
        fitnessMachineStatusCharacteristic->setValue(resistanceStatus, 3);
        pCharacteristic->setValue(returnValue, 3);
        ftmsTrainingStatus[1] = 0x00;
        fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
        fitnessMachineTrainingStatus->notify();
      } break;

      case 0x05: {  // Power Level Mode
        if (spinBLEClient.connectedPM || userConfig.getSimulateWatts()) {
          int targetWatts = bytes_to_u16(rxValue[2], rxValue[1]);
          userConfig.setERGMode(true);
          computeERG(targetWatts);
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> ERG Mode Target: %d Current: %d Incline: %2f", targetWatts,
                                   userConfig.getSimulatedWatts(), userConfig.getIncline() / 100);
          returnValue[2]       = 0x01;
          uint8_t ERGStatus[3] = {0x08, (uint8_t)rxValue[1], 0x01};
          fitnessMachineStatusCharacteristic->setValue(ERGStatus, 3);
          ftmsTrainingStatus[1] = 0x0C;
          fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
          fitnessMachineTrainingStatus->notify();
        } else {
          returnValue[2] = 0x02;  // no power meter connected, so no ERG
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> ERG Mode: No Power Meter Connected");
        }
        pCharacteristic->setValue(returnValue, 3);
      } break;

      case 0x07:  // Start training
        returnValue[2] = 0x01;
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Start Training");
        pCharacteristic->setValue(returnValue, 3);
        ftmsTrainingStatus[1] = 0x00;
        fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
        fitnessMachineTrainingStatus->notify();
        break;

      case 0x11: {  // sim mode
        signed char buf[2];
        // int16_t windSpeed        = (rxValue[2] << 8) + rxValue[1];
        buf[0] = rxValue[3];  // (Least significant byte)
        buf[1] = rxValue[4];  // (Most significant byte)
        // int8_t rollingResistance = rxValue[5];
        // int8_t windResistance    = rxValue[6];
        port = bytes_to_u16(buf[1], buf[0]);
        userConfig.setIncline(port);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Sim Mode Incline %2f", userConfig.getIncline() / 100);
        userConfig.setERGMode(false);
        returnValue[2]       = 0x01;
        uint8_t simStatus[7] = {0x12, (uint8_t)rxValue[1], (uint8_t)rxValue[2], (uint8_t)rxValue[3], (uint8_t)rxValue[4], (uint8_t)rxValue[5], (uint8_t)rxValue[6]};
        fitnessMachineStatusCharacteristic->setValue(simStatus, 7);
        pCharacteristic->setValue(returnValue, 3);
        ftmsTrainingStatus[1] = 0x00;
        fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
        fitnessMachineTrainingStatus->notify();
      } break;

      case 0x13: {  // Spin Down
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Spin Down Requested");
        uint8_t spinStatus[2] = {0x14, 0x01};  // send low and high speed targets
        fitnessMachineStatusCharacteristic->setValue(spinStatus, 2);
        uint8_t controlPoint[6] = {0x80, 0x01, 0x24, 0x03, 0x96, 0x0e};  // send low and high speed targets
        pCharacteristic->setValue(controlPoint, 6);
        ftmsTrainingStatus[1] = 0x00;
        fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
        fitnessMachineTrainingStatus->notify();
      } break;

      default:
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Unsupported FTMS Request");
        pCharacteristic->setValue(returnValue, 3);
    }
    SS2K_LOG(BLE_SERVER_LOG_TAG, "%s", logBuf);
    fitnessMachineStatusCharacteristic->notify();
  } else {
    SS2K_LOG(BLE_SERVER_LOG_TAG, "App wrote nothing ");
    SS2K_LOG(BLE_SERVER_LOG_TAG, "assuming it's a Control request");
    uint8_t controlPoint[3] = {0x80, 0x00, 0x01};
    userConfig.setERGMode(true);
    pCharacteristic->setValue(controlPoint, 3);
    ftmsTrainingStatus[1] = 0x01;
    fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus, 2);
    fitnessMachineTrainingStatus->notify();
  }
  rxValue = pCharacteristic->getValue();
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
  static int oldHR    = userConfig.getSimulatedHr();
  static int newHR    = userConfig.getSimulatedHr();
  static double delta = 0;
  oldHR               = newHR;  // Copying HR from Last loop
  newHR               = userConfig.getSimulatedHr();

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
  userConfig.setSimulatedWatts(avgP);
  userConfig.setSimulatedCad(90);
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

  size_t returnLength = rxValue.length();
  uint8_t returnValue[returnLength];
  returnValue[0] = error;
  for (size_t i = 1; i < returnLength; i++) {
    returnValue[i] = rxValue[i];
  }

  if (rxValue[0] == read) {  // read requests are shorter than writes but output is same length.
    returnLength += 2;
  }

  SS2K_LOG(BLE_SERVER_LOG_TAG, "Custom Request Received");
  switch (rxValue[1]) {
    case BLE_firmwareUpdateURL:  // 0x01
      returnValue[0] = error;
      break;

    case BLE_incline: {  // 0x02
      returnValue[0] = success;
      if (rxValue[0] == read) {
        int inc        = userConfig.getIncline() * 100;
        returnValue[2] = (uint8_t)(inc & 0xff);
        returnValue[3] = (uint8_t)(inc >> 8);
      }
      if (rxValue[0] == write) {
        userConfig.setIncline(bytes_to_u16(rxValue[3], rxValue[2]) / 100);
      }
    } break;

    case BLE_simulatedWatts:  // 0x03
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig.getSimulatedWatts() & 0xff);
        returnValue[3] = (uint8_t)(userConfig.getSimulatedWatts() >> 8);
      }
      if (rxValue[0] == write) {
        userConfig.setSimulatedWatts(bytes_to_u16(rxValue[3], rxValue[2]));
      }
      break;

    case BLE_simulatedHr:  // 0x04
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig.getSimulatedHr() & 0xff);
        returnValue[3] = (uint8_t)(userConfig.getSimulatedHr() >> 8);
      }
      if (rxValue[0] == write) {
        userConfig.setSimulatedHr(bytes_to_u16(rxValue[3], rxValue[2]));
      }
      break;

    case BLE_simulatedCad:  // 0x05
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig.getSimulatedCad() & 0xff);
        returnValue[3] = (uint8_t)(userConfig.getSimulatedCad() >> 8);
      }
      if (rxValue[0] == write) {
        userConfig.setSimulatedCad(bytes_to_u16(rxValue[3], rxValue[2]));
      }
      break;

    case BLE_simulatedSpeed: {  // 0x06
      returnValue[0] = success;
      int spd        = userConfig.getSimulatedSpeed() * 10;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(spd & 0xff);
        returnValue[3] = (uint8_t)(spd >> 8);
      }
      if (rxValue[0] == write) {
        userConfig.setSimulatedSpeed(bytes_to_u16(rxValue[3], rxValue[2]) / 10);
      }
    } break;

    case BLE_deviceName:  // 0x07
      returnValue[0] = error;
      break;

    case BLE_shiftStep:  // 0x08
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig.getShiftStep() & 0xff);
        returnValue[3] = (uint8_t)(userConfig.getShiftStep() >> 8);
      }
      if (rxValue[0] == write) {
        userConfig.setShiftStep(bytes_to_u16(rxValue[3], rxValue[2]));
      }
      break;

    case BLE_stepperPower:  // 0x09
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig.getStepperPower() & 0xff);
        returnValue[3] = (uint8_t)(userConfig.getStepperPower() >> 8);
      }
      if (rxValue[0] == write) {
        userConfig.setStepperPower(bytes_to_u16(rxValue[3], rxValue[2]));
        updateStepperPower();
      }
      break;

    case BLE_stealthchop:  // 0x0A
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig.getStealthchop());
      }
      if (rxValue[0] == write) {
        userConfig.setStealthChop(bytes_to_u16(rxValue[3], rxValue[2]));
        updateStealthchop();
      }
      break;

    case BLE_inclineMultiplier: {  // 0x0B
      returnValue[0] = success;
      int inc        = userConfig.getInclineMultiplier();
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(inc & 0xff);
        returnValue[3] = (uint8_t)(inc >> 8);
      }
      if (rxValue[0] == write) {
        userConfig.setInclineMultiplier(bytes_to_u16(rxValue[3], rxValue[2]));
      }
    } break;

    case BLE_powerCorrectionFactor: {  // 0x0C
      returnValue[0] = success;
      int pcf        = userConfig.getPowerCorrectionFactor() * 10;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(pcf & 0xff);
        returnValue[3] = (uint8_t)(pcf >> 8);
      }
      if (rxValue[0] == write) {
        userConfig.setPowerCorrectionFactor(bytes_to_u16(rxValue[3], rxValue[2]) / 10);
      }
    } break;

    case BLE_simulateHr:  // 0x0D
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig.getSimulateHr());
      }
      if (rxValue[0] == write) {
        userConfig.setSimulateHr(bytes_to_u16(rxValue[3], rxValue[2]));
      }
      break;

    case BLE_simulateWatts:  // 0x0E
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig.getSimulateWatts());
      }
      if (rxValue[0] == write) {
        userConfig.setSimulateWatts(bytes_to_u16(rxValue[3], rxValue[2]));
      }
      break;

    case BLE_simulateCad:  // 0x0F
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig.getSimulateCad());
      }
      if (rxValue[0] == write) {
        userConfig.setSimulateCad(bytes_to_u16(rxValue[3], rxValue[2]));
      }
      break;

    case BLE_ERGMode:  // 0x10
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig.getERGMode());
      }
      if (rxValue[0] == write) {
        userConfig.setERGMode(bytes_to_u16(rxValue[3], rxValue[2]));
      }
      break;

    case BLE_autoUpdate:  // 0x11
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig.getautoUpdate());
      }
      if (rxValue[0] == write) {
        userConfig.setAutoUpdate(bytes_to_u16(rxValue[3], rxValue[2]));
      }
      break;

    case BLE_ssid:  // 0x12
      returnValue[0] = error;
      break;

    case BLE_password:  // 0x13
      returnValue[0] = error;
      break;

    case BLE_foundDevices:  // 0x14
      returnValue[0] = error;
      break;

    case BLE_connectedPowerMeter:  // 0x15
      returnValue[0] = error;
      break;

    case BLE_connectedHeartMonitor:  // 0x16
      returnValue[0] = error;
      break;

    case BLE_shifterPosition:  // 0x17
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig.getShifterPosition() & 0xff);
        returnValue[3] = (uint8_t)(userConfig.getShifterPosition() >> 8);
      }
      if (rxValue[0] == write) {
        userConfig.setShifterPosition(bytes_to_u16(rxValue[3], rxValue[2]));
      }
      break;

    case BLE_saveToSpiffs:  // 0x18
      userConfig.saveToSPIFFS();
      break;

    case BLE_targetPosition:  // 0x19
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(targetPosition & 0xff);
        returnValue[3] = (uint8_t)(targetPosition >> 8);
        returnValue[4] = (uint8_t)(targetPosition >> 16);
        returnValue[5] = (uint8_t)(targetPosition >> 24);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        targetPosition = (long((uint8_t)(rxValue[2]) << 0 | (uint8_t)(rxValue[3]) << 8 | (uint8_t)(rxValue[4]) << 16 | (uint8_t)(rxValue[5]) << 24));
      }
      break;

    case BLE_externalControl:  // 0x1A
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(externalControl);
      }
      if (rxValue[0] == write) {
        externalControl = (bool)(bytes_to_u16(rxValue[3], rxValue[2]));
      }
      break;

    case BLE_syncMode:  // 0x1B
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(syncMode);
      }
      if (rxValue[0] == write) {
        syncMode = (bool)(bytes_to_u16(rxValue[3], rxValue[2]));
      }
      break;
  }

  pCharacteristic->setValue(returnValue, returnLength);
  pCharacteristic->indicate();
}

void SpinBLEServer::notifyShift(bool upDown) {
  uint8_t returnValue[4];
  returnValue[0] = 0x80;
  returnValue[1] = BLE_shifterPosition;
  returnValue[2] = (uint8_t)(userConfig.getShifterPosition() & 0xff);
  returnValue[3] = (uint8_t)(userConfig.getShifterPosition() >> 8);
  smartSpin2kCharacteristic->setValue(returnValue, 4);
  smartSpin2kCharacteristic->notify(true);
}