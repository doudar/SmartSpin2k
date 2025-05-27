/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "BLE_Fitness_Machine_Service.h"
#include "DirConManager.h"
#include <Constants.h>
#include <vector>

BLE_Fitness_Machine_Service::BLE_Fitness_Machine_Service()
    : pFitnessMachineService(nullptr),
      fitnessMachineFeature(nullptr),
      fitnessMachineControlPoint(nullptr),
      fitnessMachineStatusCharacteristic(nullptr),
      fitnessMachineIndoorBikeData(nullptr),
      fitnessMachineResistanceLevelRange(nullptr),
      fitnessMachinePowerRange(nullptr),
      fitnessMachineInclinationRange(nullptr),
      fitnessMachineTrainingStatus(nullptr) {}

void BLE_Fitness_Machine_Service::setupService(NimBLEServer *pServer, MyCharacteristicCallbacks *chrCallbacks) {
  // Resistance, IPower, HeartRate
  uint8_t ftmsResistanceLevelRange[6] = {0x01, 0x00, 0x64, 0x00, 0x01, 0x00};  // 1:100 increment 1
  uint8_t ftmsPowerRange[6]           = {0x01, 0x00, 0xA0, 0x0F, 0x01, 0x00};  // 1:4000 watts increment 1
  uint8_t ftmsInclinationRange[6]     = {0x38, 0xff, 0xc8, 0x00, 0x01, 0x00};  // -20.0:20.0 increment .1
  // Fitness Machine Feature Flags Setup
  struct FitnessMachineFeature ftmsFeature = {FitnessMachineFeatureFlags::Types::CadenceSupported | FitnessMachineFeatureFlags::Types::HeartRateMeasurementSupported |
                                                  FitnessMachineFeatureFlags::Types::PowerMeasurementSupported | FitnessMachineFeatureFlags::Types::InclinationSupported |
                                                  FitnessMachineFeatureFlags::Types::ResistanceLevelSupported,
                                              FitnessMachineTargetFlags::PowerTargetSettingSupported | FitnessMachineTargetFlags::Types::InclinationTargetSettingSupported |
                                                  FitnessMachineTargetFlags::Types::ResistanceTargetSettingSupported |
                                                  FitnessMachineTargetFlags::Types::IndoorBikeSimulationParametersSupported |
                                                  FitnessMachineTargetFlags::Types::SpinDownControlSupported};

  // Fitness Machine service setup
  pFitnessMachineService             = spinBLEServer.pServer->createService(FITNESSMACHINESERVICE_UUID);
  fitnessMachineFeature              = pFitnessMachineService->createCharacteristic(FITNESSMACHINEFEATURE_UUID, NIMBLE_PROPERTY::READ);
  fitnessMachineControlPoint         = pFitnessMachineService->createCharacteristic(FITNESSMACHINECONTROLPOINT_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE);
  fitnessMachineStatusCharacteristic = pFitnessMachineService->createCharacteristic(FITNESSMACHINESTATUS_UUID, NIMBLE_PROPERTY::NOTIFY);
  fitnessMachineIndoorBikeData       = pFitnessMachineService->createCharacteristic(FITNESSMACHINEINDOORBIKEDATA_UUID, NIMBLE_PROPERTY::NOTIFY);
  fitnessMachineResistanceLevelRange = pFitnessMachineService->createCharacteristic(FITNESSMACHINERESISTANCELEVELRANGE_UUID, NIMBLE_PROPERTY::READ);
  fitnessMachinePowerRange           = pFitnessMachineService->createCharacteristic(FITNESSMACHINEPOWERRANGE_UUID, NIMBLE_PROPERTY::READ);
  fitnessMachineInclinationRange     = pFitnessMachineService->createCharacteristic(FITNESSMACHINEINCLINATIONRANGE_UUID, NIMBLE_PROPERTY::READ);
  fitnessMachineTrainingStatus       = pFitnessMachineService->createCharacteristic(FITNESSMACHINETRAININGSTATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  fitnessMachineFeature->setValue(ftmsFeature.bytes, sizeof(ftmsFeature));

  fitnessMachineResistanceLevelRange->setValue(ftmsResistanceLevelRange, sizeof(ftmsResistanceLevelRange));
  fitnessMachinePowerRange->setValue(ftmsPowerRange, sizeof(ftmsPowerRange));
  fitnessMachineInclinationRange->setValue(ftmsInclinationRange, sizeof(ftmsInclinationRange));
  fitnessMachineIndoorBikeData->setCallbacks(chrCallbacks);
  fitnessMachineControlPoint->setCallbacks(chrCallbacks);
  pFitnessMachineService->start();
  spinBLEServer.pServer->getAdvertising()->addServiceUUID(pFitnessMachineService->getUUID());

  // Add service UUID to DirCon MDNS
  DirConManager::addBleServiceUuid(pFitnessMachineService->getUUID());
}

void BLE_Fitness_Machine_Service::update() {
  std::vector<uint8_t> ftmsIndoorBikeData;

  this->processFTMSWrite();

  // Calculate Speed for FTMS
  int speedFtmsUnit = 0;
  if (rtConfig->getSimulatedSpeed() > 5) {
    speedFtmsUnit = rtConfig->getSimulatedSpeed() * 100;
  } else {
    speedFtmsUnit = spinBLEServer.calculateSpeed() * 100;
  }

  // Rebuild ftmsIndoorBikeData vector with current values
  ftmsIndoorBikeData.clear();

  // Fitness Machine Indoor Bike Data Flags Setup
  FitnessMachineIndoorBikeDataFlags::Types ftmsIBDFlags = FitnessMachineIndoorBikeDataFlags::InstantaneousCadencePresent |
                                                          FitnessMachineIndoorBikeDataFlags::ResistanceLevelPresent | FitnessMachineIndoorBikeDataFlags::InstantaneousPowerPresent;

  if (String(userConfig->getConnectedHeartMonitor()) != "none") {
    ftmsIBDFlags = ftmsIBDFlags | FitnessMachineIndoorBikeDataFlags::HeartRatePresent;
  }

  // Add flags
  ftmsIndoorBikeData.push_back(static_cast<uint8_t>(ftmsIBDFlags & 0xFF));
  ftmsIndoorBikeData.push_back(static_cast<uint8_t>((ftmsIBDFlags >> 8) & 0xFF));

  // Add speed
  ftmsIndoorBikeData.push_back(static_cast<uint8_t>(speedFtmsUnit & 0xff));
  ftmsIndoorBikeData.push_back(static_cast<uint8_t>(speedFtmsUnit >> 8));

  // Add cadence. FTMS expects cadence in 0.5 RPM units
  ftmsIndoorBikeData.push_back(static_cast<uint8_t>(static_cast<int>(rtConfig->cad.getValue() * 2) & 0xff));
  ftmsIndoorBikeData.push_back(static_cast<uint8_t>(static_cast<int>(rtConfig->cad.getValue() * 2) >> 8));

  // Add resistance
  ftmsIndoorBikeData.push_back(static_cast<uint8_t>(rtConfig->resistance.getValue() & 0xff));
  ftmsIndoorBikeData.push_back(static_cast<uint8_t>(rtConfig->resistance.getValue() >> 8));

  // Add power
  ftmsIndoorBikeData.push_back(static_cast<uint8_t>(rtConfig->watts.getValue() & 0xff));
  ftmsIndoorBikeData.push_back(static_cast<uint8_t>(rtConfig->watts.getValue() >> 8));

  // Add heart rate if HRM is connected
  if (String(userConfig->getConnectedHeartMonitor()) != "none") {
    ftmsIndoorBikeData.push_back(static_cast<uint8_t>(rtConfig->hr.getValue()));
  }

  // Notify the cycling power measurement characteristic
  // Need to set the value before notifying so that read works correctly.
  fitnessMachineIndoorBikeData->setValue(ftmsIndoorBikeData.data(), ftmsIndoorBikeData.size());
  fitnessMachineIndoorBikeData->notify();

  // Also notify DirCon TCP clients about Indoor Bike Data
  DirConManager::notifyCharacteristic(NimBLEUUID(FITNESSMACHINESERVICE_UUID), fitnessMachineIndoorBikeData->getUUID(), ftmsIndoorBikeData.data(), ftmsIndoorBikeData.size());

  const int kLogBufCapacity = 200;  // Data(30), Sep(data/2), Arrow(3), CharId(37), Sep(3), CharId(37), Sep(3), Name(10), Prefix(2), HR(7), SEP(1), CD(10), SEP(1), PW(8),
                                    // SEP(1), SD(7), Suffix(2), Nul(1), rounded up
  char logBuf[kLogBufCapacity];
  logCharacteristic(logBuf, kLogBufCapacity, ftmsIndoorBikeData.data(), ftmsIndoorBikeData.size(), FITNESSMACHINESERVICE_UUID, fitnessMachineIndoorBikeData->getUUID(),
                    "FTMS(IBD)[ HR(%d) CD(%.2f) PW(%d) SD(%.2f) ]", rtConfig->hr.getValue() % 1000, fmodf(rtConfig->cad.getValue(), 1000.0), rtConfig->watts.getValue() % 10000,
                    fmodf((float)speedFtmsUnit / 100.0, 1000.0));
}

// The things that happen when we receive a FitnessMachineControlPointProcedure from a Client.
void BLE_Fitness_Machine_Service::processFTMSWrite() {
  while (!spinBLEServer.writeCache.empty()) {
    std::string rxValue = spinBLEServer.writeCache.front();
    spinBLEServer.writeCache.pop();
    if (rxValue == "") {
      return;
    }
    std::vector<uint8_t> returnValue        = {FitnessMachineControlPointProcedure::ResponseCode, (uint8_t)rxValue[0], FitnessMachineControlPointResultCode::OpCodeNotSupported};
    BLECharacteristic *pCharacteristic      = NimBLEDevice::getServer()->getServiceByUUID(FITNESSMACHINESERVICE_UUID)->getCharacteristic(FITNESSMACHINECONTROLPOINT_UUID);
    std::vector<uint8_t> ftmsStatus         = {FitnessMachineStatus::ReservedForFutureUse};
    std::vector<uint8_t> ftmsTrainingStatus = {0x00, FitnessMachineTrainingStatus::Other};

    if (rxValue.length() >= 1) {
      uint8_t *pData            = reinterpret_cast<uint8_t *>(&rxValue[0]);
      int length                = rxValue.length();
      const int kLogBufCapacity = (rxValue.length() * 2) + 60;  // largest comment is 48 VV
      char logBuf[kLogBufCapacity];
      int logBufLength = ss2k_log_hex_to_buffer(pData, length, logBuf, 0, kLogBufCapacity);
      int port         = 0;

      switch ((uint8_t)rxValue[0]) {
        case FitnessMachineControlPointProcedure::RequestControl:
          returnValue[2] = FitnessMachineControlPointResultCode::Success;
          rtConfig->watts.setTarget(0);
          rtConfig->setSimTargetWatts(false);
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Control Request");
          ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x01;
          ftmsStatus            = {FitnessMachineStatus::StartedOrResumedByUser};
          break;

        case FitnessMachineControlPointProcedure::Reset: {
          returnValue[2] = FitnessMachineControlPointResultCode::Success;  // 0x01;
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Reset");
          ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x01;
          ftmsStatus            = {FitnessMachineStatus::Reset};

        } break;

        case FitnessMachineControlPointProcedure::SetTargetInclination: {
          rtConfig->setFTMSMode((uint8_t)rxValue[0]);
          returnValue[2] = FitnessMachineControlPointResultCode::Success;  // 0x01;
          port           = (rxValue[2] << 8) + rxValue[1];
          port *= 10;
          rtConfig->setTargetIncline(port);
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Incline Mode: %2f", rtConfig->getTargetIncline() / 100);
          ftmsStatus            = {FitnessMachineStatus::TargetInclineChanged, (uint8_t)rxValue[1], (uint8_t)rxValue[2]};
          ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x00;
        } break;

        case FitnessMachineControlPointProcedure::SetTargetResistanceLevel: {
          rtConfig->setFTMSMode((uint8_t)rxValue[0]);
          returnValue[2]        = FitnessMachineControlPointResultCode::Success;  // 0x01;
          ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;            // 0x00;
          if ((int)rxValue[1] >= rtConfig->getMinResistance() && (int)rxValue[1] <= rtConfig->getMaxResistance()) {
            rtConfig->resistance.setTarget((int)rxValue[1]);
            returnValue[2] = FitnessMachineControlPointResultCode::Success;
            logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Resistance Mode: %d", rtConfig->resistance.getTarget());
          } else if ((int)rxValue[1] > rtConfig->getMinResistance()) {
            rtConfig->resistance.setTarget(rtConfig->getMaxResistance());
            returnValue[2] = FitnessMachineControlPointResultCode::InvalidParameter;
            logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Resistance Request %d beyond limits", (int)rxValue[1]);
          } else {
            rtConfig->resistance.setTarget(rtConfig->getMinResistance());
            returnValue[2] = FitnessMachineControlPointResultCode::InvalidParameter;
            logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Resistance Request %d beyond limits", (int)rxValue[1]);
          }
          ftmsStatus = {FitnessMachineStatus::TargetResistanceLevelChanged, (uint8_t)(rtConfig->resistance.getTarget() % 256)};
          rtConfig->resistance.setTarget(rtConfig->resistance.getTarget());
        } break;

        case FitnessMachineControlPointProcedure::SetTargetPower: {
          rtConfig->setFTMSMode((uint8_t)rxValue[0]);
          if (spinBLEClient.connectedPM || rtConfig->watts.getSimulate() || spinBLEClient.connectedCD) {
            returnValue[2] = FitnessMachineControlPointResultCode::Success;  // 0x01;
            rtConfig->watts.setTarget(bytes_to_u16(rxValue[2], rxValue[1]));
            logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> ERG Mode Target: %d Current: %d Incline: %2f", rtConfig->watts.getTarget(),
                                     rtConfig->watts.getValue(), rtConfig->getTargetIncline() / 100);
            ftmsStatus            = {FitnessMachineStatus::TargetPowerChanged, (uint8_t)rxValue[1], (uint8_t)rxValue[2]};
            ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::WattControl;  // 0x0C;
            // Adjust set point for powerCorrectionFactor and send to FTMS server (if connected)
            int adjustedTarget         = rtConfig->watts.getTarget() / userConfig->getPowerCorrectionFactor();
            const uint8_t translated[] = {FitnessMachineControlPointProcedure::SetTargetPower, (uint8_t)(adjustedTarget % 256), (uint8_t)(adjustedTarget / 256)};
            spinBLEClient.FTMSControlPointWrite(translated, 3);
          } else {
            returnValue[2] = FitnessMachineControlPointResultCode::OpCodeNotSupported;  // 0x02; no power meter connected, so no ERG
            logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> ERG Mode: No Power Meter Connected");
          }
        } break;

        case FitnessMachineControlPointProcedure::StartOrResume: {
          returnValue[2] = FitnessMachineControlPointResultCode::Success;  // 0x01;
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Start Training");
          ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x00;
          ftmsStatus            = {FitnessMachineStatus::StartedOrResumedByUser};
        } break;

        case FitnessMachineControlPointProcedure::StopOrPause: {
          returnValue[2] = FitnessMachineControlPointResultCode::Success;  // 0x01;
          // rxValue[1] == 1 -> Stop, 2 -> Pause
          // TODO: Move stepper to Min Position
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Stop Training");
          ftmsStatus            = {FitnessMachineStatus::StoppedOrPausedByUser};
          ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x00;

        } break;

        case FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters: {  // sim mode
          rtConfig->setFTMSMode((uint8_t)rxValue[0]);
          returnValue[2] = FitnessMachineControlPointResultCode::Success;  // 0x01;
          signed char buf[2];
          // int16_t windSpeed        = (rxValue[2] << 8) + rxValue[1];
          buf[0] = rxValue[3];  // (Least significant byte)
          buf[1] = rxValue[4];  // (Most significant byte)
          // int8_t rollingResistance = rxValue[5];
          // int8_t windResistance    = rxValue[6];
          port = bytes_to_u16(buf[1], buf[0]);
          rtConfig->setTargetIncline(port);
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Sim Mode Incline %2f", rtConfig->getTargetIncline() / 100);
          ftmsStatus = {FitnessMachineStatus::IndoorBikeSimulationParametersChanged,
                        (uint8_t)rxValue[1],
                        (uint8_t)rxValue[2],
                        (uint8_t)rxValue[3],
                        (uint8_t)rxValue[4],
                        (uint8_t)rxValue[5],
                        (uint8_t)rxValue[6]};

          ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x00;
          spinBLEClient.FTMSControlPointWrite(pData, length);
        } break;

        case FitnessMachineControlPointProcedure::SpinDownControl: {
          rtConfig->setFTMSMode((uint8_t)rxValue[0]);
          uint8_t controlPoint[6] = {FitnessMachineControlPointProcedure::ResponseCode, 0x01, 0x24, 0x03, 0x96, 0x0e};  // send low and high speed targets
          returnValue[2]          = FitnessMachineControlPointResultCode::Success;
          pCharacteristic->setValue(controlPoint, 6);
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Spin Down Requested");
          ftmsStatus                 = {FitnessMachineStatus::SpinDownStatus, 0x01};  // send low and high speed targets
          ftmsTrainingStatus[1]      = FitnessMachineTrainingStatus::Other;           // 0x00;
          spinBLEServer.spinDownFlag = 2;
        } break;

        case FitnessMachineControlPointProcedure::SetTargetedCadence: {
          rtConfig->setFTMSMode((uint8_t)rxValue[0]);
          returnValue[2]    = FitnessMachineControlPointResultCode::Success;  // 0x01;
          int targetCadence = bytes_to_u16(rxValue[2], rxValue[1]);
          // rtConfig->setTargetCadence(targetCadence);
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Target Cadence: %d ", targetCadence);
          ftmsStatus            = {FitnessMachineStatus::TargetedCadenceChanged, (uint8_t)rxValue[1], (uint8_t)rxValue[2]};
          ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x00;
        } break;

        default: {
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Unsupported FTMS Request");
        }
      }
      SS2K_LOG(FMTS_SERVER_LOG_TAG, "%s", logBuf);
    } else {
      SS2K_LOG(FMTS_SERVER_LOG_TAG, "App wrote nothing ");
      SS2K_LOG(FMTS_SERVER_LOG_TAG, "assuming it's a Control request");
      returnValue[2]        = FitnessMachineControlPointResultCode::Success;
      ftmsStatus            = {FitnessMachineStatus::StartedOrResumedByUser};
      ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x00;
    }
    // not checking for subscription because a write request would have triggererd this
    fitnessMachineControlPoint->indicate(returnValue.data(), returnValue.size());
    fitnessMachineTrainingStatus->notify(ftmsTrainingStatus.data(), ftmsTrainingStatus.size());
    fitnessMachineStatusCharacteristic->notify(ftmsStatus.data(), ftmsStatus.size());

    // Also notify DirCon TCP clients
    DirConManager::notifyCharacteristic(NimBLEUUID(FITNESSMACHINESERVICE_UUID), fitnessMachineControlPoint->getUUID(), returnValue.data(), returnValue.size());
  }
}

bool BLE_Fitness_Machine_Service::spinDown(uint8_t response) {
  uint8_t spinStatus[2] = {FitnessMachineStatus::SpinDownStatus, response};
  fitnessMachineStatusCharacteristic->notify(spinStatus, 2);
  /*std::string rxValue = fitnessMachineStatusCharacteristic->getValue();
  if (rxValue[0] != 0x14) {
   return false;
  }
  uint8_t spinStatus[2] = {0x14, 0x01};
  SS2K_LOG(FMTS_SERVER_LOG_TAG, "Spin Status: %d", rxValue[1]);
  Serial.printf("Spin Status: %d", rxValue[1]);
  if (rxValue[1] == 0x01) {
    SS2K_LOG(FMTS_SERVER_LOG_TAG, "Spin Down Initiated");
    Serial.printf("Spin Down Initiated");
    vTaskDelay(500 / portTICK_RATE_MS);
    spinStatus[1] = 0x01;  // Initiated
    fitnessMachineStatusCharacteristic->setValue(spinStatus, 2);
    fitnessMachineStatusCharacteristic->notify();
    vTaskDelay(500 / portTICK_RATE_MS);
    spinStatus[1] = 0x04;  // send Stop Pedaling
    fitnessMachineStatusCharacteristic->setValue(spinStatus, 2);
    fitnessMachineStatusCharacteristic->notify();
    SS2K_LOG(FMTS_SERVER_LOG_TAG, "Stop Pedaling");
    Serial.printf("Stop Pedaling");
    vTaskDelay(500 / portTICK_RATE_MS);
    spinStatus[1] = 0x02;  // Success
    fitnessMachineStatusCharacteristic->setValue(spinStatus, 2);
    fitnessMachineStatusCharacteristic->notify();
  }
  if (rxValue[1] == 0x02) {
    SS2K_LOG(FMTS_SERVER_LOG_TAG, "Success");
    Serial.printf("Success");
    spinStatus[0] = 0x00;
    spinStatus[1] = 0x00;  // Success
    fitnessMachineStatusCharacteristic->setValue(spinStatus, 2);
    uint8_t returnValue[3] = {0x00, 0x00, 0x00};
    fitnessMachineControlPoint->setValue(returnValue, 3);
    fitnessMachineControlPoint->indicate();
    fitnessMachineStatusCharacteristic->notify();
  }*/

  return true;
}
