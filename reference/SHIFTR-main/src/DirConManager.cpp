#include <Arduino.h>
#include <BTDeviceManager.h>
#include <Config.h>
#include <DirConManager.h>
#include <ESPmDNS.h>
#include <Service.h>
#include <ServiceManagerCallbacks.h>
#include <Utils.h>
#include <uleb128.h>
#include <SettingsManager.h>
#include <Calculations.h>
#include <FTMS.h>

ServiceManager *DirConManager::serviceManager{};
Timer<> DirConManager::notificationTimer = timer_create_default();
bool DirConManager::started = false;
AsyncServer *DirConManager::dirConServer = new AsyncServer(DIRCON_TCP_PORT);
AsyncClient *DirConManager::dirConClients[];
String DirConManager::statusMessage = "";

uint8_t DirConManager::zwiftAsyncRideOnAnswer[18] = {0x2a, 0x08, 0x03, 0x12, 0x0d, 0x22, 0x0b, 0x52, 0x49, 0x44, 0x45, 0x5f, 0x4f, 0x4e, 0x28, 0x30, 0x29, 0x00};
uint8_t DirConManager::zwiftSyncRideOnAnswer[8] = {0x52, 0x69, 0x64, 0x65, 0x4f, 0x6e, 0x02, 0x00};

double DirConManager::defaultGearRatio;
VirtualShiftingMode DirConManager::virtualShiftingMode;

TrainerMode DirConManager::zwiftTrainerMode;
uint64_t DirConManager::zwiftPower;
int64_t DirConManager::zwiftGrade;
uint64_t DirConManager::zwiftGearRatio;
uint16_t DirConManager::zwiftBicycleWeight;
uint16_t DirConManager::zwiftUserWeight;

int64_t DirConManager::smoothedZwiftGrade;

uint16_t DirConManager::trainerInstantaneousPower;
uint16_t DirConManager::trainerInstantaneousSpeed;
uint8_t DirConManager::trainerCadence;
uint16_t DirConManager::trainerMaximumResistance;

uint16_t DirConManager::difficulty;

int16_t DirConManager::ftmsPower;
int16_t DirConManager::ftmsWindSpeed;
int16_t DirConManager::ftmsGrade;
uint8_t DirConManager::ftmsCrr;
uint8_t DirConManager::ftmsCw;

class DirConServiceManagerCallbacks : public ServiceManagerCallbacks {
  void onServiceAdded(Service *service) {
    if (service->isAdvertised()) {
      String serviceUUIDs = "";
      for (Service *currentService : service->getServiceManager()->getServices()) {
        if (currentService->isAdvertised()) {
          if (serviceUUIDs != "") {
            serviceUUIDs += ",";
          }
          serviceUUIDs += currentService->UUID.to16().toString().c_str();
        }
      }
      MDNS.addServiceTxt(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, "ble-service-uuids", serviceUUIDs.c_str());
    };
  };

  void onCharacteristicSubscriptionChanged(Characteristic *characteristic, bool removed) {
    if (characteristic->UUID.equals(NimBLEUUID(CYCLING_POWER_MEASUREMENT_CHARACTERISTIC_UUID))) {
      if (removed && (characteristic->getSubscriptions().size() == 0)) {
        DirConManager::resetValues();
      } 
    }
  };
};

void DirConManager::resetValues() {
  defaultGearRatio = (double)SettingsManager::getChainringTeeth() / (double)SettingsManager::getSprocketTeeth();
  virtualShiftingMode = SettingsManager::getVirtualShiftingMode();
  zwiftTrainerMode = TrainerMode::SIM_MODE;
  zwiftPower = 0;
  zwiftGrade = 0;
  zwiftGearRatio = 0;
  zwiftBicycleWeight = 1000;
  zwiftUserWeight = 7500;

  smoothedZwiftGrade = 0;
  
  trainerInstantaneousPower = 0;
  trainerInstantaneousSpeed = 0;
  trainerCadence = 0;
  trainerMaximumResistance = 0;

  difficulty = SettingsManager::getDifficulty();

  ftmsPower = 0;
  ftmsWindSpeed = 0;
  ftmsGrade = 0;
  ftmsCrr = 0;
  ftmsCw = 0;
}

void DirConManager::setServiceManager(ServiceManager *serviceManager) {
  DirConManager::serviceManager = serviceManager;
}

bool DirConManager::start() {
  if (!started) {
    if (serviceManager == nullptr) {
      return false;
    }
    resetValues();
    notificationTimer.every(DIRCON_NOTIFICATION_INTERVAL, DirConManager::doNotifications);
    serviceManager->subscribeCallbacks(new DirConServiceManagerCallbacks);
    MDNS.addService(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, DIRCON_TCP_PORT);
    MDNS.addServiceTxt(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, "mac-address", Utils::getMacAddressString().c_str());
    MDNS.addServiceTxt(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, "serial-number", Utils::getSerialNumberString().c_str());
    MDNS.addServiceTxt(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, "ble-service-uuids", "");
    dirConServer->begin();
    dirConServer->onClient(&handleNewClient, dirConServer);
    started = true;
    updateStatusMessage();
    return true;
  }
  return false;
}

void DirConManager::stop() {
  resetValues();
  dirConServer->end();
  notificationTimer.cancel();
  MDNS.end();
  MDNS.begin(Utils::getHostName().c_str());
  MDNS.setInstanceName(Utils::getDeviceName().c_str());
}

void DirConManager::update() {
  notificationTimer.tick();
}

bool DirConManager::doNotifications(void *arg) {
  if (started) {
    notifyInternalCharacteristics();
  }
  return true;
}

void DirConManager::handleNewClient(void *arg, AsyncClient *client) {
  bool clientAccepted = false;
  size_t clientAcceptedIndex = 0;
  log_i("New DirCon connection from %s", client->remoteIP().toString().c_str());
  for (size_t clientIndex = 0; clientIndex < DIRCON_MAX_CLIENTS; clientIndex++) {
    if ((dirConClients[clientIndex] == nullptr) || ((dirConClients[clientIndex] != nullptr) && (!dirConClients[clientIndex]->connected()))) {
      dirConClients[clientIndex] = client;
      clientAccepted = true;
      clientAcceptedIndex = clientIndex;
      // enable notifications for FE-C and internal characteristics if virtual shifting or FTMS emulation is enabled
      if (SettingsManager::isVirtualShiftingEnabled() || SettingsManager::isFTMSEnabled()) {
        Characteristic* tacxFECReadCharacteristic = serviceManager->getCharacteristic(NimBLEUUID(TACX_FEC_READ_CHARACTERISTIC_UUID));
        if (tacxFECReadCharacteristic != nullptr) {
          tacxFECReadCharacteristic->addSubscription(clientIndex);
        } else {
          log_e("Error enabling notifications for FE-C, characteristic not found, disconnecting client");
          removeSubscriptions(client);
          client->stop();
          client->abort();
          return;
        }
        Characteristic* zwiftAsyncCharacteristic = serviceManager->getCharacteristic(NimBLEUUID(ZWIFT_ASYNC_CHARACTERISTIC_UUID));
        if (zwiftAsyncCharacteristic != nullptr) {
          zwiftAsyncCharacteristic->addSubscription(clientIndex);
        }
        Characteristic* indoorBikeDataCharacteristic = serviceManager->getCharacteristic(NimBLEUUID(INDOOR_BIKE_DATA_CHARACTERISTIC_UUID));
        if (indoorBikeDataCharacteristic != nullptr) {
          indoorBikeDataCharacteristic->addSubscription(clientIndex);
        }
      }
      // send the FE-C request to receive the capabilities
      if (!BTDeviceManager::writeFECCapabilitiesRequest()) {
        log_e("Error writing FEC capabilities request");
      }
      break;
    }
  }
  updateStatusMessage();
  if (clientAccepted) {
    log_i("Free connection slot found (%d), DirCon connection from %s accepted", clientAcceptedIndex, client->remoteIP().toString().c_str());
    client->onData(&DirConManager::handleDirConData, NULL);
    client->onError(&DirConManager::handleDirConError, NULL);
    client->onDisconnect(&DirConManager::handleDirConDisconnect, NULL);
    client->onTimeout(&DirConManager::handleDirConTimeOut, NULL);
  } else {
    log_e("Maximum number of %d connections reached, DirCon connection from %s rejected", DIRCON_MAX_CLIENTS, client->remoteIP().toString().c_str());
    client->abort();
  }
}

void DirConManager::handleDirConData(void *arg, AsyncClient *client, void *data, size_t len) {
  size_t clientIndex = DirConManager::getDirConClientIndex(client);
  if (clientIndex != (DIRCON_MAX_CLIENTS + 1)) {
    DirConMessage currentMessage;
    size_t parsedBytes = 0;
    while (parsedBytes < len) {
      parsedBytes += currentMessage.parse((uint8_t *)data + parsedBytes, (len - parsedBytes), 0);
      if (currentMessage.Identifier == DIRCON_MSGID_ERROR) {
        log_e("Error handling data from DirCon client: Unable to parse DirCon message");
        parsedBytes += 1;
        if ((len - parsedBytes) <= 0) {
          return;
        }
      }
      if (!DirConManager::processDirConMessage(&currentMessage, client, clientIndex)) {
        log_e("Error handling data from DirCon client: Unable to process DirCon message");
      }
    }
  } else {
    log_e("Error handling data from DirCon client: Client slot not found");
  }
}

void DirConManager::handleDirConError(void *arg, AsyncClient *client, int8_t error) {
  log_e("DirCon client connection error %s from %s, stopping client...", client->errorToString(error), client->remoteIP().toString().c_str());
  removeSubscriptions(client);
  client->stop();
}

void DirConManager::handleDirConDisconnect(void *arg, AsyncClient *client) {
  log_i("DirCon client disconnected");
  removeSubscriptions(client);
}

void DirConManager::handleDirConTimeOut(void *arg, AsyncClient *client, uint32_t time) {
  log_e("DirCon client ACK timeout from %s, stopping client...", client->remoteIP().toString().c_str());
  removeSubscriptions(client);
  client->stop();
}

void DirConManager::removeSubscriptions(AsyncClient *client) {
  size_t clientIndex = DirConManager::getDirConClientIndex(client);
  if (clientIndex != (DIRCON_MAX_CLIENTS + 1)) {
    for (Service *service : serviceManager->getServices()) {
      for (Characteristic *characteristic : service->getCharacteristics()) {
        characteristic->removeSubscription(clientIndex);
        if (characteristic->UUID.equals(NimBLEUUID(TACX_FEC_READ_CHARACTERISTIC_UUID))) {
          if (characteristic->getSubscriptions().size() == 0) {
            // if no client is connected anymore set default SIM mode on trainer
            BTDeviceManager::writeFECTrackResistance(0x4E20);
          }
        }
      }
    }
  }
  updateStatusMessage();
}

size_t DirConManager::getDirConClientIndex(AsyncClient *client) {
  for (size_t clientIndex = 0; clientIndex < DIRCON_MAX_CLIENTS; clientIndex++) {
    if (dirConClients[clientIndex] != nullptr) {
      if (dirConClients[clientIndex] == client) {
        return clientIndex;
        break;
      }
    }
  }
  return DIRCON_MAX_CLIENTS + 1;
}

bool DirConManager::processDirConMessage(DirConMessage *dirConMessage, AsyncClient *client, size_t clientIndex) {
  Service *service = nullptr;
  Characteristic *characteristic = nullptr;
  std::vector<uint8_t> *returnData;
  DirConMessage returnMessage;
  returnMessage.Identifier = dirConMessage->Identifier;
  returnMessage.UUID = dirConMessage->UUID;
  switch (dirConMessage->Identifier) {
    case DIRCON_MSGID_DISCOVER_SERVICES:
      returnMessage.ResponseCode = DIRCON_RESPCODE_SUCCESS_REQUEST;
      for (Service *currentService : serviceManager->getServices()) {
        returnMessage.AdditionalUUIDs.push_back(currentService->UUID);
      }
      break;
    case DIRCON_MSGID_DISCOVER_CHARACTERISTICS:
      returnMessage.ResponseCode = DIRCON_RESPCODE_SUCCESS_REQUEST;
      service = serviceManager->getService(dirConMessage->UUID);
      if (service == nullptr) {
        returnMessage.ResponseCode = DIRCON_RESPCODE_SERVICE_NOT_FOUND;
        break;
      }
      for (Characteristic *characteristic : service->getCharacteristics()) {
        returnMessage.AdditionalUUIDs.push_back(characteristic->UUID);
        returnMessage.AdditionalData.push_back(getDirConProperties(characteristic->getProperties()));
      }
      break;
    case DIRCON_MSGID_ENABLE_CHARACTERISTIC_NOTIFICATIONS:
      returnMessage.ResponseCode = DIRCON_RESPCODE_SUCCESS_REQUEST;
      characteristic = serviceManager->getCharacteristic(dirConMessage->UUID);
      if (characteristic == nullptr) {
        returnMessage.ResponseCode = DIRCON_RESPCODE_CHARACTERISTIC_NOT_FOUND;
        break;
      }
      if (dirConMessage->AdditionalData.size() != 1) {
        returnMessage.Identifier = DIRCON_RESPCODE_CHARACTERISTIC_OPERATION_NOT_SUPPORTED;
        break;
      }
      if (dirConMessage->AdditionalData.at(0) == 0) {
        if (characteristic->isSubscribed(clientIndex)) {
          characteristic->removeSubscription(clientIndex);
        }
      } else {
        if (!characteristic->isSubscribed(clientIndex)) {
          characteristic->addSubscription(clientIndex);
        }
      }
      break;
    case DIRCON_MSGID_WRITE_CHARACTERISTIC:
      returnMessage.ResponseCode = DIRCON_RESPCODE_SUCCESS_REQUEST;
      characteristic = serviceManager->getCharacteristic(dirConMessage->UUID);
      if (characteristic == nullptr) {
        returnMessage.ResponseCode = DIRCON_RESPCODE_CHARACTERISTIC_NOT_FOUND;
        break;
      }
      if (dirConMessage->AdditionalData.size() == 0) {
        returnMessage.Identifier = DIRCON_RESPCODE_CHARACTERISTIC_OPERATION_NOT_SUPPORTED;
        break;
      }
      if (characteristic->getService() != nullptr) {
        if (!characteristic->getService()->isInternal()) {
          if (!BTDeviceManager::writeBLECharacteristic(characteristic->getService()->UUID, characteristic->UUID, &(dirConMessage->AdditionalData))) {
            returnMessage.Identifier = DIRCON_RESPCODE_CHARACTERISTIC_OPERATION_NOT_SUPPORTED;
            break;
          }
        } else {
          if (characteristic->UUID.equals(NimBLEUUID(ZWIFT_SYNCRX_CHARACTERISTIC_UUID))) {
            if (!processZwiftSyncRequest(characteristic->getService(), characteristic, &dirConMessage->AdditionalData)) {
              returnMessage.Identifier = DIRCON_RESPCODE_CHARACTERISTIC_OPERATION_NOT_SUPPORTED;
              break;
            }
          }
          if (characteristic->UUID.equals(NimBLEUUID(FITNESS_MACHINE_CONTROL_POINT_CHARACTERISTIC_UUID))) {
            returnMessage.AdditionalData = processFTMSWriteRequest(characteristic->getService(), characteristic, &dirConMessage->AdditionalData);
            if (returnMessage.AdditionalData.size() == 0) {
              returnMessage.Identifier = DIRCON_RESPCODE_CHARACTERISTIC_OPERATION_NOT_SUPPORTED;
              break;
            }
          }
          break;
        }
      } else {
        returnMessage.ResponseCode = DIRCON_RESPCODE_CHARACTERISTIC_NOT_FOUND;
        break;
      }
      break;
    case DIRCON_MSGID_READ_CHARACTERISTIC:
      returnMessage.ResponseCode = DIRCON_RESPCODE_SUCCESS_REQUEST;
      characteristic = serviceManager->getCharacteristic(dirConMessage->UUID);
      if (characteristic == nullptr) {
        returnMessage.ResponseCode = DIRCON_RESPCODE_CHARACTERISTIC_NOT_FOUND;
        break;
      }
      if (characteristic->getService() != nullptr) {
        if (!characteristic->getService()->isInternal()) {
          returnMessage.AdditionalData = BTDeviceManager::readBLECharacteristic(characteristic->getService()->UUID, characteristic->UUID);
          if (returnMessage.AdditionalData.size() == 0) {
            returnMessage.Identifier = DIRCON_RESPCODE_CHARACTERISTIC_OPERATION_NOT_SUPPORTED;
            break;
          }
        } else {
          if (characteristic->getService()->UUID.equals(NimBLEUUID(FITNESS_MACHINE_SERVICE_UUID))) {
            returnMessage.AdditionalData = processFTMSReadRequest(characteristic->getService(), characteristic, &dirConMessage->AdditionalData);
            if (returnMessage.AdditionalData.size() == 0) {
              returnMessage.Identifier = DIRCON_RESPCODE_CHARACTERISTIC_OPERATION_NOT_SUPPORTED;
              break;
            }
          } else {
            log_e("DirCon read to internal characteristic %s not implemented!", characteristic->UUID.toString().c_str());
          }
        }
      } else {
        returnMessage.ResponseCode = DIRCON_RESPCODE_CHARACTERISTIC_NOT_FOUND;
        break;
      }

      break;
    default:
      log_e("Unknown identifier %d in DirCon message, skipping further processing", dirConMessage->Identifier);
      return false;
      break;
  }
  returnData = returnMessage.encode(dirConMessage->SequenceNumber);
  if (returnData->size() > 0) {
    client->write((char *)returnData->data(), returnData->size());
  } else {
    log_e("Error encoding DirCon message, aborting");
    return false;
  }

  return true;
}

uint8_t DirConManager::getDirConProperties(uint32_t characteristicProperties) {
  uint8_t returnValue = 0x00;
  if ((characteristicProperties & READ) == READ) {
    returnValue = returnValue | DIRCON_CHAR_PROP_FLAG_READ;
  }
  if ((characteristicProperties & WRITE) == WRITE) {
    returnValue = returnValue | DIRCON_CHAR_PROP_FLAG_WRITE;
  }
  if ((characteristicProperties & NOTIFY) == NOTIFY) {
    returnValue = returnValue | DIRCON_CHAR_PROP_FLAG_NOTIFY;
  }
  return returnValue;
}

std::map<uint8_t, uint64_t> DirConManager::getZwiftDataValues(std::vector<uint8_t> *requestData) {
  std::map<uint8_t, uint64_t> returnMap;
  if (requestData->size() > 2) {
    if (requestData->at(0) == 0x04) {
      size_t processedBytes = 0;
      size_t dataIndex = 0;
      uint8_t currentKey = 0;
      uint64_t currentValue = 0;
      if (((requestData->at(1) == 0x22) || (requestData->at(1) == 0x2A)) && requestData->size() > 4) {
        dataIndex = 3;
        if (requestData->size() == (requestData->at(2) + dataIndex)) {
          while (dataIndex < requestData->size()) {
            currentKey = requestData->at(dataIndex);
            dataIndex++;
            processedBytes = bfs::DecodeUleb128(requestData->data() + dataIndex, requestData->size() - dataIndex, &currentValue);
            dataIndex = dataIndex + processedBytes;
            if (processedBytes == 0) {
              log_e("Error parsing unsigned Zwift data values, hex: ", Utils::getHexString(requestData).c_str());
              dataIndex++;
            }
            returnMap.emplace(currentKey, currentValue);
          }
        } else {
          log_e("Error parsing unsigned Zwift data values, length mismatch");
        }
      } else if (requestData->at(1) == 0x18) {
        dataIndex = 2;
        processedBytes = bfs::DecodeUleb128(requestData->data() + dataIndex, requestData->size() - dataIndex, &currentValue);
        if (processedBytes == 0) {
          log_e("Error parsing unsigned Zwift data value, hex: ", Utils::getHexString(requestData).c_str());
        } else {
          returnMap.emplace(currentKey, currentValue);
        }
      }
    }
  }
  return returnMap;
}

// @Roberto Viola: This is the part you're probably looking for to implement the virtual shifting correctly in qdomyos-zwift :-)
// The values that are being sent do not specify a specific gear but the ratio from 0.75 to 5.49 in LEB128 encoding.
// 0.75 0.87 0.99 1.11 1.23 1.38 1.53 1.68 1.86 2.04 2.22 2.40 2.61 2.82 3.03 3.24 3.49 3.74 3.99 4.24 4.54 4.84 5.14 5.49
// LEB128 is used nearly everywhere here, feel free to contact me to get more insights.
bool DirConManager::processZwiftSyncRequest(Service *service, Characteristic *characteristic, std::vector<uint8_t> *requestData) {
  std::vector<uint8_t> returnData;
  if (requestData->size() >= 3) {
    uint8_t zwiftCommand = requestData->at(0);
    uint8_t zwiftCommandSubtype = requestData->at(1);
    uint8_t zwiftCommandLength = requestData->at(2);
    std::map<uint8_t, uint64_t> requestValues = getZwiftDataValues(requestData);
    TrainerMode newZwiftTrainerMode = zwiftTrainerMode;
    // for development purposes
    /*
    if ((zwiftCommandSubtype == 0x22) && (zwiftCommandSubtype != 0x18)) {
      log_i("Zwift command: %d, commandsubtype: %d", zwiftCommand, zwiftCommandSubtype);
      for (auto requestValue = requestValues.begin(); requestValue != requestValues.end(); requestValue++) {
        log_i("Zwift sync data key: %d, value %d", requestValue->first, requestValue->second);
      }
    }
    log_i("Zwift request: %s", Utils::getHexString(requestData).c_str());
    */
    switch (zwiftCommand) {
      // Status request
      case 0x00:
        log_i("Zwift 0x00 request, hex value: %s", Utils::getHexString(requestData).c_str());
        // do nothing for the moment but TODO in future
        return true;
        break;

      // Change request (ERG / SIM mode / Gear ratio / ...)
      case 0x04:
        switch (zwiftCommandSubtype) {
          // ERG Mode
          case 0x18:
            if ((zwiftTrainerMode == TrainerMode::SIM_MODE) || (zwiftTrainerMode == TrainerMode::SIM_MODE_VIRTUAL_SHIFTING)) {
              log_i("ERG mode requested");
              zwiftTrainerMode = TrainerMode::ERG_MODE;
              updateStatusMessage();            
            }
            if (requestValues.find(0x00) != requestValues.end()) {
              zwiftPower = requestValues.at(0x00);
            }
            log_i("FEC target power: %d", (zwiftPower * 4));
            // FE-C target power is in 0.25W unit
            if (!BTDeviceManager::writeFECTargetPower((zwiftPower * 4))) {
              log_e("Error writing FEC target power");
            }
            break;

          // SIM/ERG Mode Inclination
          // TODO: [04 22 09 10 8D 01 18 EC 27 20 90 03] 5100 / 400 what are these values?!
          case 0x22:
            if (requestValues.find(0x10) != requestValues.end()) {
              zwiftGrade = requestValues.at(0x10); 
              // don't know why but they're using bit 0 for signing...
              if ((zwiftGrade & 0x01) == 0x01) {
                zwiftGrade ^= 0x01;
                zwiftGrade *= -1;
              }
              smoothedZwiftGrade += zwiftGrade;
              smoothedZwiftGrade = smoothedZwiftGrade / 2;
            }
            updateSIMModeResistance();
            break;

          // SIM mode parameter update
          case 0x2A:
            if (requestValues.find(0x10) != requestValues.end()) {
              zwiftGearRatio = requestValues.at(0x10);
            }
            if (zwiftGearRatio == 0) {
              newZwiftTrainerMode = TrainerMode::SIM_MODE;
            } else {
              newZwiftTrainerMode = TrainerMode::SIM_MODE_VIRTUAL_SHIFTING;
            }
            if (zwiftTrainerMode != newZwiftTrainerMode) {
              if (newZwiftTrainerMode == TrainerMode::SIM_MODE) {
                log_i("SIM mode requested");
              } else {
                log_i("SIM + VS mode requested");
              }
              zwiftTrainerMode = newZwiftTrainerMode;
              updateStatusMessage();            
            }
            if (requestValues.find(0x20) != requestValues.end()) {
              zwiftBicycleWeight = requestValues.at(0x20);
              if (requestValues.find(0x28) != requestValues.end()) {
                zwiftUserWeight = requestValues.at(0x28);
                if (!BTDeviceManager::writeFECUserConfiguration((uint16_t)(zwiftBicycleWeight / 5), zwiftUserWeight, (uint8_t)(Calculations::wheelDiameter / 0.01), (uint8_t)round(defaultGearRatio / 0.03))) {
                  log_e("Error writing FEC user configuration");
                }
              }
            }
            updateSIMModeResistance();
            break;

          // Unknown
          default:
            log_e("Unknown Zwift Sync change request with hex value: %s", Utils::getHexString(requestData).c_str());
            for (auto requestValue = requestValues.begin(); requestValue != requestValues.end(); requestValue++) {
              log_i("Zwift sync data key: %d, value %d", requestValue->first, requestValue->second);
            }
            break;
        }

        return true;
        break;

      // Unknown request, similar to 0x00
      case 0x41:
        log_e("Zwift 0x41 request, hex value: %s", Utils::getHexString(requestData).c_str());
        // do nothing for the moment but TODO in future
        return true;
        break;

      // RideOn request
      case 0x52:
        if (requestData->size() == 8) {
          sendDirConCharacteristicNotification(NimBLEUUID(ZWIFT_SYNCTX_CHARACTERISTIC_UUID), zwiftSyncRideOnAnswer, sizeof(zwiftSyncRideOnAnswer), false);
          sendDirConCharacteristicNotification(NimBLEUUID(ZWIFT_ASYNC_CHARACTERISTIC_UUID), zwiftAsyncRideOnAnswer, sizeof(zwiftAsyncRideOnAnswer), false);
          return true;
        }
        break;

      default:
        log_e("Unknown Zwift Sync request with hex value: %s", Utils::getHexString(requestData).c_str());
        break;
    }
  }
  return false;
}

void DirConManager::updateSIMModeResistance() {
  // normal SIM mode w/o virtual shifting, use normal track resistance mode
  if (zwiftTrainerMode == TrainerMode::SIM_MODE) {
    if (!BTDeviceManager::writeFECTrackResistance((uint16_t)(0x4E20 + zwiftGrade), 0x53)) {
      log_e("Error writing FEC track resistance");
    }
  } else if (zwiftTrainerMode == TrainerMode::SIM_MODE_VIRTUAL_SHIFTING) {
    // Target Power Mode
    if (virtualShiftingMode == VirtualShiftingMode::TARGET_POWER) {
      uint16_t trainerTargetPower = Calculations::calculateFECTargetPowerValue(
        (zwiftBicycleWeight + zwiftUserWeight) / 100.0,
        (SettingsManager::isGradeSmoothingEnabled() ? smoothedZwiftGrade : zwiftGrade) / 100.0,
        trainerInstantaneousSpeed / 1000.0,
        trainerCadence,
        zwiftGearRatio / 10000.0,
        defaultGearRatio,
        difficulty);
      if (!BTDeviceManager::writeFECTargetPower(trainerTargetPower)) {
        log_e("Error writing SIM+VS FEC target power");
      }
    
    // Track Resistance Mode
    } else if (virtualShiftingMode == VirtualShiftingMode::TRACK_RESISTANCE) {
      uint16_t trainerTrackResistanceGrade = Calculations::calculateFECTrackResistanceGrade(
        (zwiftBicycleWeight + zwiftUserWeight) / 100.0,
        (SettingsManager::isGradeSmoothingEnabled() ? smoothedZwiftGrade : zwiftGrade) / 100.0,
        trainerInstantaneousSpeed / 1000.0,
        trainerCadence,
        zwiftGearRatio / 10000.0,
        defaultGearRatio,
        difficulty);
      if (!BTDeviceManager::writeFECTrackResistance(trainerTrackResistanceGrade, 0x53)) {
        log_e("Error writing SIM+VS FEC track resistance");
      }

    // Basic Resistance Mode
    } else {
      uint8_t trainerBasicResistance = Calculations::calculateFECBasicResistancePercentageValue(
        (zwiftBicycleWeight + zwiftUserWeight) / 100.0,
        (SettingsManager::isGradeSmoothingEnabled() ? smoothedZwiftGrade : zwiftGrade) / 100.0,
        trainerInstantaneousSpeed / 1000.0,
        trainerCadence,
        zwiftGearRatio / 10000.0,
        defaultGearRatio,
        trainerMaximumResistance,
        difficulty);
      if (!BTDeviceManager::writeFECBasicResistance(trainerBasicResistance)) {
        log_e("Error writing SIM+VS FEC basic resistance");
      }
    }
  }
}

void DirConManager::notifyDirConCharacteristic(const NimBLEUUID &characteristicUUID, uint8_t *pData, size_t length) {
  notifyDirConCharacteristic(serviceManager->getCharacteristic(characteristicUUID), pData, length);
}

void DirConManager::notifyDirConCharacteristic(Characteristic *characteristic, uint8_t *pData, size_t length) {
  if (characteristic != nullptr) {
    // Fetch FE-C information
    if (characteristic->UUID.equals(NimBLEUUID(TACX_FEC_READ_CHARACTERISTIC_UUID))) {
      if (length == 13) {
        // switch for the FE-C data page received
        switch (pData[4]) {
          //page 16 - 0x10 - General FE Data
          case 0x10:
            trainerInstantaneousSpeed = (pData[9] << 8) | pData[8];
            break;
          //page 25 - 0x19 - Stationary Bike Data
          case 0x19:
            trainerCadence = pData[6];
            trainerInstantaneousPower = ((pData[10] & 0xF) << 8) | pData[9];
            break;
          //page 54 - 0x36 - FE Capabilities
          case 0x36:
            trainerMaximumResistance = (pData[10] << 8) | pData[9];
            break;
          default:
            //log_i("FEC DATA: %s", Utils::getHexString(pData, length).c_str());
            break;
        }
      }
    }
    sendDirConCharacteristicNotification(characteristic, pData, length, true);
  }
}

void DirConManager::sendDirConCharacteristicNotification(const NimBLEUUID &characteristicUUID, uint8_t *pData, size_t length, bool onlySubscribers) {
  sendDirConCharacteristicNotification(serviceManager->getCharacteristic(characteristicUUID), pData, length, onlySubscribers);
}

void DirConManager::sendDirConCharacteristicNotification(Characteristic *characteristic, uint8_t *pData, size_t length, bool onlySubscribers) {
  if (onlySubscribers && (characteristic->getSubscriptions().size() == 0)) {
    return;
  }
  DirConMessage dirConMessage;
  dirConMessage.Identifier = DIRCON_MSGID_UNSOLICITED_CHARACTERISTIC_NOTIFICATION;
  dirConMessage.UUID = characteristic->UUID;
  for (size_t dataIndex = 0; dataIndex < length; dataIndex++) {
    dirConMessage.AdditionalData.push_back(pData[dataIndex]);
  }
  std::vector<uint8_t> *messageData = dirConMessage.encode(0);
  for (size_t clientIndex = 0; clientIndex < DIRCON_MAX_CLIENTS; clientIndex++) {
    if ((dirConClients[clientIndex] != nullptr) && dirConClients[clientIndex]->connected()) {
      if ((onlySubscribers && characteristic->isSubscribed(clientIndex)) || !onlySubscribers) {
        dirConClients[clientIndex]->write((char *)messageData->data(), messageData->size());
      }
    }
  }
}

void DirConManager::notifyInternalCharacteristics() {
  for (Service *service : serviceManager->getServices()) {
    if (service->isInternal()) {
      for (Characteristic *characteristic : service->getCharacteristics()) {
        if (characteristic->getSubscriptions().size() > 0) {
          if (characteristic->UUID.equals(NimBLEUUID(ZWIFT_ASYNC_CHARACTERISTIC_UUID))) {
            std::vector<uint8_t> notificationData = generateZwiftAsyncNotificationData(trainerInstantaneousPower, trainerCadence, 0, 0, 0);
            notifyDirConCharacteristic(characteristic, notificationData.data(), notificationData.size());
          }
          if (characteristic->UUID.equals(NimBLEUUID(INDOOR_BIKE_DATA_CHARACTERISTIC_UUID))) {
            std::vector<uint8_t> notificationData = generateIndoorBikeDataNotificationData((uint16_t)(trainerInstantaneousSpeed * 36 / 100), (uint16_t)(trainerCadence * 2), (int16_t)trainerInstantaneousPower);
            notifyDirConCharacteristic(characteristic, notificationData.data(), notificationData.size());
          }
        }
      }
    }
  }
}

std::vector<uint8_t> DirConManager::generateZwiftAsyncNotificationData(int64_t power, int64_t cadence, int64_t unknown1, int64_t unknown2, int64_t unknown3, int64_t unknown4) {
  std::vector<uint8_t> notificationData;
  int64_t currentData = 0;
  uint8_t leb128Buffer[16];
  size_t leb128Size = 0;

  notificationData.push_back(0x03);

  for (uint8_t dataBlock = 0x08; dataBlock <= 0x30; dataBlock += 0x08) {
    notificationData.push_back(dataBlock);
    if (dataBlock == 0x08) {
      currentData = power;
    }
    if (dataBlock == 0x10) {
      currentData = cadence;
    }
    if (dataBlock == 0x18) {
      currentData = unknown1;
    }
    if (dataBlock == 0x20) {
      currentData = unknown2;
    }
    if (dataBlock == 0x28) {
      currentData = unknown3;
    }
    if (dataBlock == 0x30) {
      currentData = unknown4;
    }
    leb128Size = bfs::EncodeUleb128(currentData, leb128Buffer, sizeof(leb128Buffer));
    for (uint8_t leb128Byte = 0; leb128Byte < leb128Size; leb128Byte++) {
      notificationData.push_back(leb128Buffer[leb128Byte]);
    }
  }
  return notificationData;
}

String DirConManager::getStatusMessage() {
  return statusMessage;
}

void DirConManager::updateStatusMessage() {

  if (zwiftTrainerMode == TrainerMode::SIM_MODE) {
    statusMessage = "SIM";
  } else if (zwiftTrainerMode == TrainerMode::SIM_MODE_VIRTUAL_SHIFTING) {
    statusMessage = "SIM + VS";
  } else {
    statusMessage = "ERG";
  }
  statusMessage += " mode, ";

  size_t connectedClients = 0;
  for (size_t clientIndex = 0; clientIndex < DIRCON_MAX_CLIENTS; clientIndex++) {
    if ((dirConClients[clientIndex] != nullptr) && dirConClients[clientIndex]->connected()) {
      connectedClients++;
    }
  }
  statusMessage += connectedClients;
  statusMessage += "/";
  statusMessage += DIRCON_MAX_CLIENTS;
  statusMessage += " client(s)";
}

TrainerMode DirConManager::getZwiftTrainerMode() {
  return zwiftTrainerMode;
}

std::vector<uint8_t> DirConManager::processFTMSReadRequest(Service* service, Characteristic* characteristic, std::vector<uint8_t>* requestData) {
  std::vector<uint8_t> returnData;
  if (characteristic->UUID.equals(NimBLEUUID(FITNESS_MACHINE_FEATURE_CHARACTERISTIC_UUID))) {
    FITNESS_MACHINE_FEATURES_TYPE fitnessMachineFeaturesType;

    fitnessMachineFeaturesType.fitnessMachineFeatures = 
    FITNESS_MACHINE_FEATURES::cadence_supported |
    FITNESS_MACHINE_FEATURES::power_measurement_supported;

    fitnessMachineFeaturesType.targetSettingFeatures = 
    TARGET_SETTING_FEATURES::inclination_target_setting_supported |
    TARGET_SETTING_FEATURES::resistance_target_setting_supported |
    TARGET_SETTING_FEATURES::power_target_setting_supported |
    TARGET_SETTING_FEATURES::indoor_bike_simulation_parameters_supported;

    returnData = Utils::getVectorFromStruct(&fitnessMachineFeaturesType, sizeof(fitnessMachineFeaturesType));
  }
  return returnData;
}

std::vector<uint8_t> DirConManager::processFTMSWriteRequest(Service* service, Characteristic* characteristic, std::vector<uint8_t>* requestData) {
  std::vector<uint8_t> returnData;
  //log_i("FTMS write request to characteristic %s with data %s", characteristic->UUID.toString().c_str(), Utils::getHexString(requestData).c_str());
  if (requestData->size() > 0) {
    bool success = false;
    uint8_t opCode = requestData->at(0);
    std::vector<uint8_t> parameter;
    if (requestData->size() > 1) {
      parameter = std::vector<uint8_t>(requestData->begin() + 1, requestData->end());
    }
    returnData.push_back(0x80);
    returnData.push_back(opCode);
    switch (opCode)
    {
      // request control
      case 0x00:
        zwiftTrainerMode = TrainerMode::SIM_MODE;
        log_i("SIM mode requested");
        success = true;
        break;

      // reset
      case 0x01:
        zwiftTrainerMode = TrainerMode::SIM_MODE;
        log_i("SIM mode requested");
        ftmsPower = 0;
        ftmsWindSpeed = 0;
        ftmsGrade = 0;
        ftmsCrr = 0;
        ftmsCw = 0;
        success = true;
        break;

      // target power
      case 0x05:
        if (parameter.size() == 2) {
          if (zwiftTrainerMode != TrainerMode::ERG_MODE) {
            zwiftTrainerMode = TrainerMode::ERG_MODE;
            log_i("ERG mode requested");
          }
          // target power in 1W 
          ftmsPower = (parameter.at(1) << 8) | parameter.at(0);
          if (ftmsPower < 0) {
            log_e("FTMS target power is negative, setting to 0");
            ftmsPower = 0;
          }
          if (!BTDeviceManager::writeFECTargetPower(ftmsPower * 4)) {
            log_e("Error writing ERG FEC target power");
          }
          success = true;
        } else {
          success = false;
        }
        break;

      // start or resume
      case 0x07:
        // todo: start or resume
        success = true;
        break;

      // stop or pause
      case 0x08:
        // todo: stop or pause  
        success = true;
        break;
      // set indoor bike simulation parameters
      case 0x11:
        // [11 00 00 14 00 32 33]
        if (parameter.size() == 6) {
          if (zwiftTrainerMode != TrainerMode::SIM_MODE) {
            zwiftTrainerMode = TrainerMode::SIM_MODE;
            log_i("SIM mode requested");
          }
          zwiftTrainerMode = TrainerMode::SIM_MODE;
          // wind speed in 0.001 m/s
          ftmsWindSpeed = (parameter.at(1) << 8) | parameter.at(0);
          // grade in 0.01%
          ftmsGrade = (parameter.at(3) << 8) | parameter.at(2);
          // crr in 0.0001
          ftmsCrr = parameter.at(4);
          // cw in 0.01
          ftmsCw = parameter.at(5);
          if (!BTDeviceManager::writeFECTrackResistance((uint16_t)(0x4E20 + ftmsGrade), ftmsCrr)) {
            log_e("Error writing SIM FEC track resistance");
          }
          success = true;
        } else {
          success = false;
        }
        break;

      default:
        log_e("Unknown FTMS opcode %d with parameter data %s received, discarding", opCode, Utils::getHexString(parameter).c_str());
        break;
    }
    if (success) {
      returnData.push_back(FITNESS_MACHINE_CONTROL_POINT_RESULT_CODE::success);
    } else {
      returnData.push_back(FITNESS_MACHINE_CONTROL_POINT_RESULT_CODE::op_code_not_supported);
    }
  }

  //log_i("FTMS write request to characteristic %s returning data %s", characteristic->UUID.toString().c_str(), Utils::getHexString(returnData).c_str());

  return returnData;
}

/**
 * @brief Generate the IndoorBikeData notification data
 * 
 * @param instantaneousSpeed Instantaneous speed in kph with a resolution of 0.01
 * @param instantaneousCadence Instantaneous cadence in rpm with a resolution of 0.5
 * @param instantaneousPower Instantaneous power in watts with a resolution of 1
 * @return std::vector<uint8_t> The notification data
 */
std::vector<uint8_t> DirConManager::generateIndoorBikeDataNotificationData(uint16_t instantaneousSpeed, uint16_t instantaneousCadence, int16_t instantaneousPower) {
  std::vector<uint8_t> notificationData;
  
  // enable instantaneous speed, instantaneous cadence and instantaneous power
  uint16_t flags =  INDOOR_BIKE_DATA_CHARACTERISTICS_FLAGS::instantaneous_cadence_present |
                    INDOOR_BIKE_DATA_CHARACTERISTICS_FLAGS::instantaneous_power_present;

  notificationData.push_back((uint8_t)(flags & 0xFF));
  notificationData.push_back((uint8_t)(flags >> 8));

  // add instantaneous speed (kph with a resolution of 0.01)
  notificationData.push_back((uint8_t)(instantaneousSpeed & 0xFF));
  notificationData.push_back((uint8_t)(instantaneousSpeed >> 8));

  // add instantaneous cadence (rpm with a resolution of 0.5)
  notificationData.push_back((uint8_t)(instantaneousCadence & 0xFF));
  notificationData.push_back((uint8_t)(instantaneousCadence >> 8));

  // add instantaneous power (watts with a resolution of 1)
  notificationData.push_back((uint8_t)(instantaneousPower & 0xFF));
  notificationData.push_back((uint8_t)(instantaneousPower >> 8));
  return notificationData;
}