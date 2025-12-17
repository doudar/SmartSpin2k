/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "BLE_Fitness_Machine_Service.h"
#include "DirConManager.h"
#include "Main.h"
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
  uint8_t ftmsResistanceLevelRange[6] = {0x01, 0x00, 0x64, 0x00, 0x01, 0x00};  // .1:10 increment .1
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
  pFitnessMachineService = spinBLEServer.pServer->createService(FITNESSMACHINESERVICE_UUID);
  fitnessMachineFeature  = pFitnessMachineService->createCharacteristic(FITNESSMACHINEFEATURE_UUID, NIMBLE_PROPERTY::READ);
  fitnessMachineControlPoint =
      pFitnessMachineService->createCharacteristic(FITNESSMACHINECONTROLPOINT_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
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

  if (strcmp(userConfig->getConnectedHeartMonitor(), NONE) != 0) {
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
  int resistanceValue;
  // Check if bike has resistance reporting capability or resistance simulation enabled
  bool hasResistanceReporting = (!rtConfig->resistance.getSimulate() && 
                                (rtConfig->resistance.getTimestamp() > 0 && 
                                 (millis() - rtConfig->resistance.getTimestamp()) < 5000));
  
  if (hasResistanceReporting) {
    // Use reported resistance value
    resistanceValue = rtConfig->resistance.getValue();
  } else {
    // Calculate resistance from stepper position for bikes that don't report resistance
    resistanceValue = this->calculateResistanceFromPosition();
    rtConfig->resistance.setValue(resistanceValue);
    rtConfig->resistance.setSimulate(true); // Mark as simulated
  }
  ftmsIndoorBikeData.push_back(static_cast<uint8_t>(resistanceValue & 0xff));
  ftmsIndoorBikeData.push_back(static_cast<uint8_t>(resistanceValue >> 8));

  // Add power
  ftmsIndoorBikeData.push_back(static_cast<uint8_t>(rtConfig->watts.getValue() & 0xff));
  ftmsIndoorBikeData.push_back(static_cast<uint8_t>(rtConfig->watts.getValue() >> 8));

  // Add heart rate if HRM is connected
  if (strcmp(userConfig->getConnectedHeartMonitor(), NONE) != 0) {
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
          break;

        case FitnessMachineControlPointProcedure::Reset: {
          returnValue[2] = FitnessMachineControlPointResultCode::Success;
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Reset");
          ftmsStatus            = {FitnessMachineStatus::Reset};
          ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Idle;
        } break;

        case FitnessMachineControlPointProcedure::SetTargetInclination: {
          rtConfig->setFTMSMode((uint8_t)rxValue[0]);
          returnValue[2] = FitnessMachineControlPointResultCode::Success;
          int16_t rawInclineTenthsPercent = (int16_t)((rxValue[2] << 8) | rxValue[1]); // signed 0.1% units
          port                            = static_cast<int>(rawInclineTenthsPercent) * 10; // convert to 0.01% units
          rtConfig->setTargetIncline(port);
          logBufLength += snprintf(logBuf + logBufLength,
                                   kLogBufCapacity - logBufLength,
                                   "-> Incline Mode: %2f",
                                   rtConfig->getTargetIncline() / 100);
          ftmsStatus            = {FitnessMachineStatus::TargetInclineChanged, (uint8_t)rxValue[1], (uint8_t)rxValue[2]};
          ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::ManualMode;
        } break;

        case FitnessMachineControlPointProcedure::SetTargetResistanceLevel: {
          rtConfig->setFTMSMode((uint8_t)rxValue[0]);
          int16_t requestedResistance = (int16_t)((rxValue[2] << 8) | rxValue[1]);

          if (requestedResistance >= rtConfig->getMinResistance() && requestedResistance <= rtConfig->getMaxResistance()) {
            rtConfig->resistance.setTarget(requestedResistance);
            
            // For bikes that don't report resistance, calculate stepper position from resistance level (0-100)
            bool hasResistanceReporting = (!rtConfig->resistance.getSimulate() && 
                                          (rtConfig->resistance.getTimestamp() > 0 && 
                                           (millis() - rtConfig->resistance.getTimestamp()) < 5000));
            
            if (!hasResistanceReporting) {
              int32_t minPos, maxPos;
              
              // Use homing values if available, otherwise use stepper min/max
              if (userConfig->getHMin() != INT32_MIN && userConfig->getHMax() != INT32_MIN) {
                minPos = userConfig->getHMin();
                maxPos = userConfig->getHMax();
              } else {
                minPos = rtConfig->getMinStep();
                maxPos = rtConfig->getMaxStep();
              }
              
              // TODO: Implement calculation of target position from resistance percentage if resistance reporting is unavailable.
            }
            
            returnValue[2] = FitnessMachineControlPointResultCode::Success;
            logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Resistance Mode: %d", rtConfig->resistance.getTarget());
          } else {
            // Clamp the value if it's out of bounds
            if (requestedResistance > rtConfig->getMaxResistance()) {
              rtConfig->resistance.setTarget(rtConfig->getMaxResistance());
            } else {  // requestedResistance < rtConfig->getMinResistance()
              rtConfig->resistance.setTarget(rtConfig->getMinResistance());
            }
            returnValue[2] = FitnessMachineControlPointResultCode::InvalidParameter;
            logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Resistance Request %d beyond limits", requestedResistance);
          }

          int16_t targetRes     = rtConfig->resistance.getTarget();
          ftmsStatus            = {FitnessMachineStatus::TargetResistanceLevelChanged, (uint8_t)(targetRes & 0xff), (uint8_t)(targetRes >> 8)};
          ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::ManualMode;
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
          ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::WarmingUp;
          ftmsStatus            = {FitnessMachineStatus::StartedOrResumedByUser};
        } break;

        case FitnessMachineControlPointProcedure::StopOrPause: {
          returnValue[2] = FitnessMachineControlPointResultCode::Success;

          uint8_t controlParam = (rxValue.length() > 1) ? rxValue[1] : 0x01; 
          ftmsStatus = {FitnessMachineStatus::StoppedOrPausedByUser, controlParam};
          if (controlParam == 0x01) {  // Stop
            logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Stop Training");
            ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Idle;
          } else if (controlParam == 0x02) {  // Pause
            logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Pause Training");
            ftmsTrainingStatus = fitnessMachineTrainingStatus->getValue();
          }

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

          ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::ManualMode;
          spinBLEClient.FTMSControlPointWrite(pData, length);
        } break;

        case FitnessMachineControlPointProcedure::SpinDownControl: {
          rtConfig->setFTMSMode((uint8_t)rxValue[0]);

          // The response parameter for a successful spin down command.
          // Values are Target Speed Low and Target Speed High in km/h with a resolution of 0.01.
          // Example: 8.00 km/h (0x0320) and 24.00 km/h (0x0960)
          uint8_t responseParams[] = {0x20, 0x03, 0x60, 0x09};

          // Build the complete, correct response in a single vector
          returnValue = {FitnessMachineControlPointProcedure::ResponseCode, (uint8_t)rxValue[0], FitnessMachineControlPointResultCode::Success};

          // Append the mandatory parameters for a successful spindown
          returnValue.insert(returnValue.end(), std::begin(responseParams), std::end(responseParams));

          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Spin Down Requested");
          ftmsStatus                 = {FitnessMachineStatus::SpinDownStatus, FitnessMachineStatus::SpinDown_SpinDownRequested};
          ftmsTrainingStatus[1]      = FitnessMachineTrainingStatus::Other;
          spinBLEServer.spinDownFlag = 2;
        } break;

        case FitnessMachineControlPointProcedure::SetTargetedCadence: {
          rtConfig->setFTMSMode((uint8_t)rxValue[0]);
          returnValue[2]    = FitnessMachineControlPointResultCode::Success;  // 0x01;
          int targetCadence = bytes_to_u16(rxValue[2], rxValue[1]);
          // rtConfig->setTargetCadence(targetCadence);
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Target Cadence: %d ", targetCadence);
          ftmsStatus            = {FitnessMachineStatus::TargetedCadenceChanged, (uint8_t)rxValue[1], (uint8_t)rxValue[2]};
          ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::ManualMode;  // 0x00;
        } break;

        default: {
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Unsupported FTMS Request");
        }
      }
      SS2K_LOG(FMTS_SERVER_LOG_TAG, "%s. Responding: %02x %02x %02x", logBuf, returnValue[0], returnValue[1], returnValue[2]);
    } else {
      SS2K_LOG(FMTS_SERVER_LOG_TAG, "App wrote nothing ");
      SS2K_LOG(FMTS_SERVER_LOG_TAG, "assuming it's a Control request");
      returnValue[2]        = FitnessMachineControlPointResultCode::Success;
      ftmsStatus            = {FitnessMachineStatus::StartedOrResumedByUser};
      ftmsTrainingStatus[1] = FitnessMachineTrainingStatus::Other;  // 0x00;
    }
    // not checking for subscription because a write request would have triggered this
    fitnessMachineControlPoint->setValue(returnValue.data(), returnValue.size());
    fitnessMachineControlPoint->notify();
    if (fitnessMachineTrainingStatus->getValue() != ftmsTrainingStatus) {
      fitnessMachineTrainingStatus->setValue(ftmsTrainingStatus);
      fitnessMachineTrainingStatus->notify();
      // Also notify DirCon TCP clients
      DirConManager::notifyCharacteristic(NimBLEUUID(FITNESSMACHINESERVICE_UUID), fitnessMachineTrainingStatus->getUUID(), ftmsTrainingStatus.data(), ftmsTrainingStatus.size());
    }
    if (fitnessMachineStatusCharacteristic->getValue() != ftmsStatus) {
      fitnessMachineStatusCharacteristic->setValue(ftmsStatus);
      fitnessMachineStatusCharacteristic->notify();
      // Also notify DirCon TCP clients
      DirConManager::notifyCharacteristic(NimBLEUUID(FITNESSMACHINESERVICE_UUID), fitnessMachineStatusCharacteristic->getUUID(), ftmsStatus.data(), ftmsStatus.size());
    }

    // Also notify DirCon TCP clients
    DirConManager::notifyCharacteristic(NimBLEUUID(FITNESSMACHINESERVICE_UUID), fitnessMachineControlPoint->getUUID(), returnValue.data(), returnValue.size());
  }
}

bool BLE_Fitness_Machine_Service::spinDown(uint8_t response) {
  uint8_t spinStatus[2] = {FitnessMachineStatus::SpinDownStatus, response};
  // Set the value of the characteristic
  fitnessMachineStatusCharacteristic->setValue(spinStatus, sizeof(spinStatus));
  // Notify the connected client
  fitnessMachineStatusCharacteristic->notify();
  SS2K_LOG(FMTS_SERVER_LOG_TAG, "Sent SpinDown Status: 0x%02X", response);
  // Also notify DirCon TCP clients about the status change
  DirConManager::notifyCharacteristic(NimBLEUUID(FITNESSMACHINESERVICE_UUID), fitnessMachineStatusCharacteristic->getUUID(), spinStatus, sizeof(spinStatus));

  return true;
}

// Calculate resistance from stepper position for bikes that don't natively report resistance
int BLE_Fitness_Machine_Service::calculateResistanceFromPosition() {
  int32_t currentPosition = ss2k->getCurrentPosition();
  int32_t minPos, maxPos;
  
  // Use homing values if available, otherwise use stepper min/max
  if (userConfig->getHMin() != INT32_MIN && userConfig->getHMax() != INT32_MIN) {
    minPos = userConfig->getHMin();
    maxPos = userConfig->getHMax();
  } else {
    minPos = rtConfig->getMinStep();
    maxPos = rtConfig->getMaxStep();
  }
  
  // Ensure we have valid range
  if (maxPos <= minPos) {
    return 50; // Default to mid-point resistance if range is invalid
  }
  
  // Calculate resistance as percentage (0-100) based on position
  int resistance = ((currentPosition - minPos) * 100) / (maxPos - minPos);
  
  // Clamp to valid range
  if (resistance < 0) resistance = 0;
  if (resistance > 100) resistance = 100;
  
  return resistance;
}
