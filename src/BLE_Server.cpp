/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "BLE_Common.h"

#include <ArduinoJson.h>
#include <NimBLEDevice.h>

// BLE Server Settings

NimBLEServer *pServer = nullptr;

BLECharacteristic *heartRateMeasurementCharacteristic;
BLECharacteristic *cyclingPowerMeasurementCharacteristic;
BLECharacteristic *fitnessMachineFeature;
BLECharacteristic *fitnessMachineIndoorBikeData;

/********************************Bit field Flag
 * Example***********************************/
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
// 00001110000000001100
// 00000101000010000110
// 00000000100001010100
//               100000
byte heartRateMeasurement[5]    = {0b00000, 60, 0, 0, 0};
byte cyclingPowerMeasurement[9] = {0b0000000100011, 0, 200, 0, 0, 0, 0, 0, 0};
byte cpsLocation[1]             = {0b000};       // sensor location 5 == left crank
byte cpFeature[1]               = {0b00100000};  // crank information present // 3rd & 2nd
                                                 // byte is reported power

byte ftmsService[6]       = {0x00, 0x00, 0x00, 0b01, 0b0100000, 0x00};
byte ftmsControlPoint[8]  = {0, 0, 0, 0, 0, 0, 0, 0};  // 0x08 we need to return a value of 1 for any sucessful change
byte ftmsMachineStatus[8] = {0, 0, 0, 0, 0, 0, 0, 0};

struct FitnessMachineFeature ftmsFeature = {
    FitnessMachineFeatureFlags::Types::CadenceSupported | FitnessMachineFeatureFlags::Types::HeartRateMeasurementSupported |
        FitnessMachineFeatureFlags::Types::PowerMeasurementSupported,
    FitnessMachineTargetFlags::Types::InclinationTargetSettingSupported | FitnessMachineTargetFlags::Types::IndoorBikeSimulationParametersSupported};

uint8_t ftmsIndoorBikeData[14] = {0x44, 0x02, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};  // 00000000100001010100 ISpeed, ICAD,
                                                                                                            // TDistance, IPower, ETime
uint8_t ftmsResistanceLevelRange[6] = {0x00, 0x00, 0x3A, 0x98, 0xC5, 0x68};                                 // +-15000 not sure what units
uint8_t ftmsPowerRange[6]           = {0x00, 0x00, 0xA0, 0x0F, 0x01, 0x00};                                 // 1-4000 watts

void startBLEServer() {
  // Server Setup
  debugDirector("Starting BLE Server");
  pServer = BLEDevice::createServer();

  // HEART RATE MONITOR SERVICE SETUP
  BLEService *pHeartService          = pServer->createService(HEARTSERVICE_UUID);
  heartRateMeasurementCharacteristic = pHeartService->createCharacteristic(HEARTCHARACTERISTIC_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  // Power Meter MONITOR SERVICE SETUP
  BLEService *pPowerMonitor = pServer->createService(CYCLINGPOWERSERVICE_UUID);

  cyclingPowerMeasurementCharacteristic = pPowerMonitor->createCharacteristic(CYCLINGPOWERMEASUREMENT_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  BLECharacteristic *cyclingPowerFeatureCharacteristic = pPowerMonitor->createCharacteristic(CYCLINGPOWERFEATURE_UUID, NIMBLE_PROPERTY::READ);

  BLECharacteristic *sensorLocationCharacteristic = pPowerMonitor->createCharacteristic(SENSORLOCATION_UUID, NIMBLE_PROPERTY::READ);

  // Fitness Machine service setup
  BLEService *pFitnessMachineService = pServer->createService(FITNESSMACHINESERVICE_UUID);

  fitnessMachineFeature = pFitnessMachineService->createCharacteristic(FITNESSMACHINEFEATURE_UUID,
                                                                       NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);

  BLECharacteristic *fitnessMachineControlPoint =
      pFitnessMachineService->createCharacteristic(FITNESSMACHINECONTROLPOINT_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);

  BLECharacteristic *fitnessMachineStatus =
      pFitnessMachineService->createCharacteristic(FITNESSMACHINESTATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);

  fitnessMachineIndoorBikeData = pFitnessMachineService->createCharacteristic(FITNESSMACHINEINDOORBIKEDATA_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  BLECharacteristic *fitnessMachineResistanceLevelRange = pFitnessMachineService->createCharacteristic(FITNESSMACHINERESISTANCELEVELRANGE_UUID, NIMBLE_PROPERTY::READ);

  BLECharacteristic *fitnessMachinePowerRange = pFitnessMachineService->createCharacteristic(FITNESSMACHINEPOWERRANGE_UUID, NIMBLE_PROPERTY::READ);

  pServer->setCallbacks(new MyServerCallbacks());

  // Creating Characteristics
  heartRateMeasurementCharacteristic->setValue(heartRateMeasurement, 5);

  cyclingPowerMeasurementCharacteristic->setValue(cyclingPowerMeasurement, 9);
  cyclingPowerFeatureCharacteristic->setValue(cpFeature, 1);
  sensorLocationCharacteristic->setValue(cpsLocation, 1);

  fitnessMachineFeature->setValue(ftmsFeature.bytes, sizeof(ftmsFeature));
  fitnessMachineControlPoint->setValue(ftmsControlPoint, 8);

  fitnessMachineIndoorBikeData->setValue(ftmsIndoorBikeData, 14);

  fitnessMachineStatus->setValue(ftmsMachineStatus, 8);
  fitnessMachineResistanceLevelRange->setValue(ftmsResistanceLevelRange, 6);
  fitnessMachinePowerRange->setValue(ftmsPowerRange, 6);

  fitnessMachineControlPoint->setCallbacks(new MyCallbacks());

  pHeartService->start();           // heart rate service
  pPowerMonitor->start();           // Power Meter Service
  pFitnessMachineService->start();  // Fitness Machine Service

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(CYCLINGPOWERSERVICE_UUID);
  pAdvertising->addServiceUUID(FITNESSMACHINESERVICE_UUID);
  pAdvertising->addServiceUUID(HEARTSERVICE_UUID);
  pAdvertising->setMaxInterval(250);
  pAdvertising->setMinInterval(160);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  debugDirector("Bluetooth Characteristic defined!");
}

void computeERG(int currentWatts, int setPoint) {
  // cooldownTimer--;

  float incline             = userConfig.getIncline();
  int cad                   = userConfig.getSimulatedCad();
  int newIncline            = incline;
  int amountToChangeIncline = 0;

  if (cad > 20) {
    if (abs(currentWatts - setPoint) < 50) {
      amountToChangeIncline = (currentWatts - setPoint) * .5;
    }
    if (abs(currentWatts - setPoint) > 50) {
      amountToChangeIncline = amountToChangeIncline + ((currentWatts - setPoint)) * 1;
    }
    amountToChangeIncline = amountToChangeIncline / ((currentWatts / 100) + 1);
  }

  if (abs(amountToChangeIncline) > userConfig.getShiftStep() * 3) {
    if (amountToChangeIncline > 0) {
      amountToChangeIncline = userConfig.getShiftStep() * 3;
    }
    if (amountToChangeIncline < 0) {
      amountToChangeIncline = -(userConfig.getShiftStep() * 3);
    }
  }

  newIncline = incline - amountToChangeIncline;  //  }
  userConfig.setIncline(newIncline);
}

void computeCSC() {  // What was SIG smoking when they came up with the Cycling
                     // Speed and Cadence Characteristic?
  if (userConfig.getSimulatedCad() > 0) {
    float crankRevPeriod = (60 * 1024) / userConfig.getSimulatedCad();
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
  }  // ^^Using the old way of setting bytes because I like it and it makes
     // more sense to me looking at it.
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
  fitnessMachineFeature->notify();
  fitnessMachineIndoorBikeData->notify();
}  // ^^Using the New Way of setting Bytes.

void updateCyclingPowerMesurementChar() {
  int remainder, quotient;
  quotient                   = userConfig.getSimulatedWatts() / 256;
  remainder                  = userConfig.getSimulatedWatts() % 256;
  cyclingPowerMeasurement[2] = remainder;
  cyclingPowerMeasurement[3] = quotient;
  cyclingPowerMeasurementCharacteristic->setValue(cyclingPowerMeasurement, 9);

  // Data(18), Sep(data/2), Static(13), Nul(1) == 41, rounded up
  char logBuf[50];
  char *logBufP = logBuf;
  for (const auto &it : cyclingPowerMeasurement) {
    logBufP += sprintf(logBufP, "%02x ", it);
  }
  strcat(logBufP, "<-- CPMC sent");

  cyclingPowerMeasurementCharacteristic->notify();
  debugDirector(String(logBuf), true);
}

void updateHeartRateMeasurementChar() {
  heartRateMeasurement[1] = userConfig.getSimulatedHr();
  heartRateMeasurementCharacteristic->setValue(heartRateMeasurement, 5);

  // Data(10), Sep(data/2), Static(11), Nul(1) == 26, rounded up
  char logBuf[35];
  char *logBufP = logBuf;
  for (const auto &it : heartRateMeasurement) {
    logBufP += sprintf(logBufP, "%02x ", it);
  }
  strcat(logBufP, "<-- HR sent");

  heartRateMeasurementCharacteristic->notify();
  debugDirector(String(logBuf), true);
}

// Creating Server Connection Callbacks

void MyServerCallbacks::onConnect(BLEServer *pServer, ble_gap_conn_desc *desc) {
  debugDirector("Bluetooth Remote Client Connected: " + String(NimBLEAddress(desc->peer_ota_addr).toString().c_str()) +
                " Connected Clients: " + String(pServer->getConnectedCount()));
  updateConnParametersFlag = true;
  bleConnDesc              = desc->conn_handle;

  if (pServer->getConnectedCount() < CONFIG_BT_NIMBLE_MAX_CONNECTIONS - NUM_BLE_DEVICES) {
    BLEDevice::startAdvertising();
  } else {
    debugDirector("Max Remote Client Connections Reached");
    BLEDevice::stopAdvertising();
  }
}

void MyServerCallbacks::onDisconnect(BLEServer *pServer) {
  debugDirector("Bluetooth Remote Client Disconnected. Remaining Clients: " + String(pServer->getConnectedCount()));
  BLEDevice::startAdvertising();
}

void MyCallbacks::onWrite(BLECharacteristic *pCharacteristic) {
  std::string rxValue = pCharacteristic->getValue();

  if (rxValue.length() > 1) {
    for (const auto &text : rxValue) {  // Range-for!
      debugDirector(String(text, HEX) + " ", false);
    }
    debugDirector("<-- From APP ", false);
    /* 17 means FTMS Incline Control Mode  (aka SIM mode)*/

    if (static_cast<int>(rxValue[0]) == 17) {
      signed char buf[2];
      buf[0] = rxValue[3];  // (Least significant byte)
      buf[1] = rxValue[4];  // (Most significant byte)

      int port = bytes_to_u16(buf[1], buf[0]);
      userConfig.setIncline(port);
      if (userConfig.getERGMode()) {
        userConfig.setERGMode(false);
      }
      debugDirector(" Target Incline: " + String((userConfig.getIncline() / 100)), false);
    }
    debugDirector("");

    /* 5 means FTMS Watts Control Mode (aka ERG mode) */
    if ((static_cast<int>(rxValue[0]) == 5) && (spinBLEClient.connectedPM)) {
      int targetWatts = bytes_to_int(rxValue[2], rxValue[1]);
      if (!userConfig.getERGMode()) {
        userConfig.setERGMode(true);
      }
      computeERG(userConfig.getSimulatedWatts(), targetWatts);
      debugDirector("ERG MODE", false);
      debugDirector(" Target: " + String(targetWatts), false);
      debugDirector(" Current: " + String(userConfig.getSimulatedWatts()),
                    false);  // not displaying numbers less than 256 correctly
                             // but they do get sent to Zwift correctly.
      debugDirector(" Incline: " + String(userConfig.getIncline() / 100), false);
      debugDirector("");
    }
  }
}

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

  oldHR = newHR;  // Copying HR from Last loop
  newHR = userConfig.getSimulatedHr();

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
#endif

  debugDirector("Power From HR: " + String(avgP));
}
