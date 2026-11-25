/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <BLE_Common.h>
#include <Power_Table.h>
#include <BLE_Custom_Characteristic.h>
#include <Constants.h>
#include "BleAppender.h"
#include "DirConManager.h"
#include "ByteUtils.h"
#include "ScanResultProtocol.h"
#include "CustomCharacteristicEvents.h"
#include "PairedSettings.h"
#include <algorithm>
#include <vector>

extern BleAppender bleAppender;

namespace {
CustomCharacteristicEvents<CONFIG_BT_NIMBLE_MAX_CONNECTIONS> bleEvents;
uint16_t scanResultId = 0, scanResultSequence = 0;

void sendScanResultPayload(ScanResultProtocol::Event event, uint16_t sequence, const std::vector<uint8_t>& payload = {}) {
  if (!PairedSettings::hasRecipients()) return;
  auto packet = ScanResultProtocol::makePacket(event, scanResultId, sequence, 0, 1, payload.data(), payload.size());
  PairedSettings::publish(std::move(packet));
}
}  // namespace

void BLE_ss2kCustomCharacteristic::setupService(NimBLEServer* pServer) {
  pSmartSpin2kService = pServer->createService(SMARTSPIN2K_SERVICE_UUID);
  smartSpin2kCharacteristic = pSmartSpin2kService->createCharacteristic(SMARTSPIN2K_CHARACTERISTIC_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
  smartSpin2kCharacteristic->setCallbacks(new ss2kCustomCharacteristicCallbacks());
  PairedSettings::initialize([](uint16_t peer, uint64_t generation, const uint8_t* data, size_t n) {
    if (peer >= PairedSettings::DIRCON_BASE) return DirConManager::notifyCustomPeer(peer - PairedSettings::DIRCON_BASE, data, n);
    if (!bleEvents.connected(peer, generation)) return false;
    return ss2kCustomCharacteristic.smartSpin2kCharacteristic->notify(data, n, peer);
  }, [](uint16_t peer, uint64_t session) {
    return peer >= PairedSettings::DIRCON_BASE || bleEvents.connected(peer, session);
  }, process);
  DirConManager::registerService(pSmartSpin2kService->getUUID(), [](NimBLECharacteristic* characteristic, const uint8_t* data, size_t length, DirConWriteResult* result) {
    if (!characteristic->getUUID().equals(SMARTSPIN2K_CHARACTERISTIC_UUID)) return false;
    PairedSettings::receive(PairedSettings::DIRCON_BASE + result->clientIndex, 1, DIRCON_SEND_BUFFER_SIZE - 22, data, length, &result->privateResponse);
    return true;
  });
}

void BLE_ss2kCustomCharacteristic::processPendingEvents() {
  PairedSettings::tick();
  decltype(bleEvents)::Event event;
  for (size_t i = 0; i < decltype(bleEvents)::CAPACITY && bleEvents.pop(event); ++i) {
    if (!event.isStatus) {
      const bool wasUpdating = ss2k->isUpdating;
      PairedSettings::receive(event.peer, event.session, std::min<size_t>(512, event.mtu - 3), reinterpret_cast<const uint8_t*>(event.value.data()), event.value.size(), nullptr);
      if (!wasUpdating && ss2k->isUpdating) {
        auto server = NimBLEDevice::getServer();
        server->setDataLen(event.peer, 251);
        // Ask for 15–30 ms intervals on the updater link only. The central decides.
        auto info = server->getPeerInfoByHandle(event.peer);
        if (info.getConnInterval() > 24 || info.getConnLatency() > 0) server->updateConnParams(event.peer, 12, 24, 0, info.getConnTimeout());
      }
    }
  }
  const unsigned dropped = bleEvents.takeDropped();
  if (dropped) SS2K_LOGW(CUSTOM_CHAR_LOG_TAG, "Settings queue full; dropped %u event(s)", dropped);
}

void ss2kCustomCharacteristicCallbacks::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
  decltype(bleEvents)::Event event;
  event.peer = connInfo.getConnHandle();
  event.mtu = connInfo.getMTU();
  event.value = pCharacteristic->getValue();
  bleEvents.push(std::move(event));
}

void BLE_ss2kCustomCharacteristic::onConnect(uint16_t connHandle) { bleEvents.connect(connHandle); }
void BLE_ss2kCustomCharacteristic::onDisconnect(uint16_t connHandle) { bleEvents.disconnect(connHandle); }

void ss2kCustomCharacteristicCallbacks::onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) {
  SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "Subscribe from %s", connInfo.getAddress().toString().c_str());
  NimBLEDevice::setMTU(515);
}
void ss2kCustomCharacteristicCallbacks::onStatus(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, int code) {
  // Notifications also produce status 0; only indication completion/failure can
  // advance a snapshot. The connection identifies whose acknowledgment it is.
  if (code == 0) return;
  decltype(bleEvents)::Event event;
  event.peer = connInfo.getConnHandle();
  event.isStatus = true;
  event.status = code;
  bleEvents.push(std::move(event));
}

void BLE_ss2kCustomCharacteristic::notify(char _item, int tableRow) {
  if (!PairedSettings::hasRecipients()) return;
  std::string read = {cc_read, _item};
  if (tableRow >= 0) read += static_cast<char>(tableRow);
  PairedSettings::publish(process(read));
}

void BLE_ss2kCustomCharacteristic::beginScanResults() {
  ++scanResultId;
  if (scanResultId == 0) ++scanResultId;
  scanResultSequence = 0;
  sendScanResultPayload(ScanResultProtocol::Event::Begin, 0);
}

void BLE_ss2kCustomCharacteristic::notifyScanResult(const String& name, const NimBLEUUID& serviceUuid) {
  if (!PairedSettings::hasRecipients()) return;
  const std::string uuid          = serviceUuid.toString();
  const std::vector<uint8_t> body = ScanResultProtocol::makeDeviceBody(uuid, std::string(name.c_str(), name.length()));
  sendScanResultPayload(ScanResultProtocol::Event::Device, scanResultSequence, body);
  ++scanResultSequence;
}

void BLE_ss2kCustomCharacteristic::endScanResults() { sendScanResultPayload(ScanResultProtocol::Event::End, scanResultSequence); }

std::vector<uint8_t> BLE_ss2kCustomCharacteristic::process(const std::string& rxValue) {
  if (rxValue.size() < 2) return {};
  const uint8_t* pData = reinterpret_cast<const uint8_t*>(rxValue.data());

#ifdef CUSTOM_CHAR_DEBUG
#define LOG_BUF_APPEND(...) do { \
  const int remaining = kLogBufCapacity - logBufLength; \
  const int written = snprintf(logBuf + logBufLength, remaining, __VA_ARGS__); \
  if (written > 0) logBufLength += std::min(written, remaining - 1); \
} while (0)
  int length                = rxValue.length();
  // A password write is never dumped beyond its two header bytes.
  if (rxValue[0] == cc_write && static_cast<uint8_t>(rxValue[1]) == BLE_password) length = 2;
  const int kLogBufCapacity = (rxValue.length() * 3) + 60;  // Hex bytes include a separating space.
  std::vector<char> logStorage(kLogBufCapacity);
  char* logBuf = logStorage.data();
  int logBufLength = ss2k_log_hex_to_buffer(pData, length, logBuf, 0, kLogBufCapacity);
#else
#define LOG_BUF_APPEND(...)
#endif

  size_t returnLength = rxValue.length();
  std::vector<uint8_t> returnValue(std::max<size_t>(returnLength + 4, 6), 0);
  std::string returnString = "";
  returnValue[0]           = cc_error;
  for (size_t i = 1; i < returnLength; i++) {
    returnValue[i] = rxValue[i];
  }

  size_t requiredLength = 2;
  if (rxValue[0] == cc_write) {
    switch (customCharacteristicValueFormat(static_cast<uint8_t>(rxValue[1]))) {
      case CustomUnsigned16:
      case CustomSigned16:
        requiredLength = 4;
        break;
      case CustomSigned32:
        requiredLength = 6;
        break;
      case CustomBoolean:
      case CustomBooleanWriteStringRead:
        requiredLength = 3;
        break;
      case CustomGearTeeth:
        requiredLength = 3;
        break;
      case CustomPowerTableRow:
        requiredLength = 3 + (2 * POWERTABLE_WATT_SIZE);
        break;
      default:
        break;
    }
  } else if (rxValue[0] == cc_read && static_cast<uint8_t>(rxValue[1]) == BLE_powerTableData) {
    requiredLength = 3;
  }

  if (rxValue.length() < requiredLength) {
    return {cc_error, static_cast<uint8_t>(rxValue[1])};
  }

  switch (rxValue[1]) {
    case BLE_incline: {  // 0x02
      LOG_BUF_APPEND("<-incline");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        int inc        = rtConfig->getTargetIncline() * 10;
        put_le16s(&returnValue[2], static_cast<int16_t>(inc));
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->setTargetIncline(get_le16s(&pData[2]) / 10.0f);
        LOG_BUF_APPEND("(%f)", rtConfig->getTargetIncline());
      }
    } break;

    case BLE_simulatedWatts:  // 0x03
      LOG_BUF_APPEND("<-simulatedWatts");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le16(&returnValue[2], static_cast<uint16_t>(rtConfig->watts.getValue()));
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->watts.setValue(get_le16(&pData[2]));
        LOG_BUF_APPEND("(%d)", rtConfig->watts.getValue());
      }
      break;

    case BLE_simulatedHr:  // 0x04
      LOG_BUF_APPEND("<-simulatedHr");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le16(&returnValue[2], static_cast<uint16_t>(rtConfig->hr.getValue()));
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->hr.setValue(get_le16(&pData[2]));
        LOG_BUF_APPEND("(%d)", rtConfig->hr.getValue());
      }
      break;

    case BLE_simulatedCad:  // 0x05
      LOG_BUF_APPEND("<-simulatedCad");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le16(&returnValue[2], static_cast<uint16_t>(rtConfig->cad.getValue()));
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->cad.setValue(get_le16(&pData[2]));
        LOG_BUF_APPEND("(%d)", rtConfig->cad.getValue());
      }
      break;

    case BLE_simulatedSpeed: {  // 0x06
      LOG_BUF_APPEND("<-simulatedSpeed");
      int spd = rtConfig->getSimulatedSpeed() * 10;
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le16(&returnValue[2], static_cast<uint16_t>(spd));
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->setSimulatedSpeed(get_le16(&pData[2]) / 10.0f);
        LOG_BUF_APPEND("(%f)", rtConfig->getSimulatedSpeed());
      }
    } break;

    case BLE_deviceName:  // 0x07
      LOG_BUF_APPEND("<-deviceName");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnString   = userConfig->getDeviceName();
      } else if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        String str     = reinterpret_cast<const char*>(pData);
        str.remove(0, 2);
        userConfig->setDeviceName(str);
        LOG_BUF_APPEND("(%s)", userConfig->getDeviceName());
      }
      break;

    case BLE_shiftStep:  // 0x08
      LOG_BUF_APPEND("<-shiftStep");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le16(&returnValue[2], static_cast<uint16_t>(userConfig->getShiftStep()));
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->setShiftStep(get_le16(&pData[2]));
        LOG_BUF_APPEND("(%d)", userConfig->getShiftStep());
      }
      break;

    case BLE_stepperPower:  // 0x09
      LOG_BUF_APPEND("<-stepperPower");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le16(&returnValue[2], static_cast<uint16_t>(userConfig->getStepperPower()));
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->setStepperPower(get_le16(&pData[2]));
        ss2k->updateStepperPower();
        LOG_BUF_APPEND("(%d)", userConfig->getStepperPower());
      }
      break;

    case BLE_stealthChop:  // 0x0A
      LOG_BUF_APPEND("<-stealthChop");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(userConfig->getStealthChop());
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->setStealthChop(rxValue[2]);
        ss2k->updateStealthChop();
        LOG_BUF_APPEND("(%s)", userConfig->getStealthChop() ? "true" : "false");
      }
      break;

    case BLE_inclineMultiplier: {  // 0x0B
      LOG_BUF_APPEND("<-inclineMultiplier");
      int inc = userConfig->getInclineMultiplier() * 10;
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le16s(&returnValue[2], static_cast<int16_t>(inc));
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->setInclineMultiplier(get_le16s(&pData[2]) / 10.0f);
        LOG_BUF_APPEND("(%f)", userConfig->getInclineMultiplier());
      }
    } break;

    case BLE_powerCorrectionFactor: {  // 0x0C
      LOG_BUF_APPEND("<-powerCorrectionFactor");
      int pcf = userConfig->getPowerCorrectionFactor() * 10;
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le16(&returnValue[2], static_cast<uint16_t>(pcf));
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->setPowerCorrectionFactor(get_le16(&pData[2]) / 10.0f);
        LOG_BUF_APPEND("(%f)", userConfig->getPowerCorrectionFactor());
      }
    } break;

    case BLE_simulateHr:  // 0x0D
      LOG_BUF_APPEND("<-simulateHr");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(rtConfig->hr.getSimulate());
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->hr.setSimulate(rxValue[2]);
        LOG_BUF_APPEND("(%s)", rtConfig->hr.getSimulate() ? "true" : "false");
      }
      break;

    case BLE_simulateWatts:  // 0x0E
      LOG_BUF_APPEND("<-simulateWatts");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(rtConfig->watts.getSimulate());
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->watts.setSimulate(rxValue[2]);
        LOG_BUF_APPEND("(%s)", rtConfig->watts.getSimulate() ? "true" : "false");
      }
      break;

    case BLE_simulateCad:  // 0x0F
      LOG_BUF_APPEND("<-simulateCad");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(rtConfig->cad.getSimulate());
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->cad.setSimulate(rxValue[2]);
        LOG_BUF_APPEND("(%s)", rtConfig->cad.getSimulate() ? "true" : "false");
      }
      break;

    case BLE_FTMSMode:  // 0x10
      LOG_BUF_APPEND("<-FTMSMode");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le16(&returnValue[2], rtConfig->getFTMSMode());
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->setFTMSMode(get_le16(&pData[2]));
        LOG_BUF_APPEND("(%hhu)", rtConfig->getFTMSMode());
      }
      break;

    case BLE_ssid:  // 0x12
      LOG_BUF_APPEND("<-ssid");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnString   = userConfig->getSsid();
      } else if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        String str     = reinterpret_cast<const char*>(pData);
        str.remove(0, 2);
        userConfig->setSsid(str);
        LOG_BUF_APPEND("(%s)", userConfig->getSsid());
      }
      break;

    case BLE_password:  // 0x13
      LOG_BUF_APPEND("<-password");
      if (rxValue[0] == cc_read) {
        // The stored password is never returned: success with no value bytes.
        returnValue[0] = cc_success;
        returnLength   = 2;
      } else if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        returnLength   = 2;  // Unlike other writes, the response never echoes the value.
        String str     = reinterpret_cast<const char*>(pData);
        str.remove(0, 2);
        // An empty write keeps the stored password.
        if (str.length() > 0) userConfig->setPassword(str);
        LOG_BUF_APPEND("(%s)", "******");
      }
      break;

    case BLE_foundDevices:  // 0x14
      LOG_BUF_APPEND("<-foundDevices");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnString   = userConfig->getFoundDevices();
      } else if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        String str     = reinterpret_cast<const char*>(pData);
        str.remove(0, 2);
        userConfig->setFoundDevices(str);
        LOG_BUF_APPEND("(%s)", userConfig->getFoundDevices());
      }
      break;

    case BLE_connectedPowerMeter:  // 0x15
      LOG_BUF_APPEND("<-connectedPowerMete");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnString   = userConfig->getConnectedPowerMeter();
      } else if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        String str     = reinterpret_cast<const char*>(pData);
        str.remove(0, 2);
        userConfig->setConnectedPowerMeter(str);
        LOG_BUF_APPEND("(%s)", userConfig->getConnectedPowerMeter());
      }
      break;

    case BLE_connectedHeartMonitor:  // 0x16
      LOG_BUF_APPEND("<-connectedHeartMonitor");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnString   = userConfig->getConnectedHeartMonitor();
      } else if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        String str     = reinterpret_cast<const char*>(pData);
        str.remove(0, 2);
        userConfig->setConnectedHeartMonitor(str);
        LOG_BUF_APPEND("(%s)", userConfig->getConnectedHeartMonitor());
      }
      break;

    case BLE_shifterPosition:  // 0x17
      LOG_BUF_APPEND("<-shifterPosition");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le16s(&returnValue[2], static_cast<int16_t>(rtConfig->getShifterPosition()));
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        rtConfig->setShifterPosition(get_le16s(&pData[2]));
        LOG_BUF_APPEND("(%d)", rtConfig->getShifterPosition());
#ifdef CUSTOM_CHAR_DEBUG
        SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "%s", logBuf);
#endif
        returnValue[0] = cc_success;
        returnLength = 4;
        put_le16s(&returnValue[2], rtConfig->getShifterPosition());
      }
      break;

    case BLE_saveToLittleFS:  // 0x18
      LOG_BUF_APPEND("<-saveToLittleFS");
      if (rxValue[0] == cc_write) {
        ss2k->saveFlag = true;
        returnValue[0] = cc_success;
      }

      break;

    case BLE_targetPosition:  // 0x19
      LOG_BUF_APPEND("<-targetPosition");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le32s(&returnValue[2], ss2k->getTargetPosition());
        returnLength += 4;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        ss2k->setTargetPosition(get_le32s(&pData[2]));
        LOG_BUF_APPEND(" (%d)", ss2k->getTargetPosition());
      }
      break;

    case BLE_externalControl:  // 0x1A
      LOG_BUF_APPEND("<-externalControl");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(ss2k->externalControl);
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0]        = cc_success;
        ss2k->externalControl = static_cast<bool>(rxValue[2]);
        LOG_BUF_APPEND("(%s)", ss2k->externalControl ? "On" : "Off");
      }
      break;

    case BLE_syncMode:  // 0x1B
      LOG_BUF_APPEND("<-syncMode");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(ss2k->syncMode);
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        ss2k->syncMode = static_cast<bool>(rxValue[2]);
        LOG_BUF_APPEND("(%s)", ss2k->syncMode ? "true" : "false");
      }
      break;

    case BLE_reboot:  // 0x1C
      LOG_BUF_APPEND("<-reboot");
      if (rxValue[0] == cc_write) {
        ss2k->rebootFlag = true;
        returnValue[0]   = cc_success;
      }
      break;

    case BLE_resetToDefaults:  // 0x1D
      LOG_BUF_APPEND("<-reset to defaults");
      if (rxValue[0] == cc_write) {
        ss2k->resetDefaultsFlag = true;
        returnValue[0]          = cc_success;
      }

      break;
    case BLE_stepperSpeed:  // 0x1E
      LOG_BUF_APPEND("<-stepperSpeed");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le16(&returnValue[2], static_cast<uint16_t>(userConfig->getStepperSpeed()));
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->setStepperSpeed(get_le16(&pData[2]));
        LOG_BUF_APPEND("(%d)", userConfig->getStepperSpeed());
        ss2k->updateStepperSpeed();
#ifdef CUSTOM_CHAR_DEBUG
        SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "%s", logBuf);
#endif
      }
      break;

    case BLE_ERGSensitivity: {  // 0x1F
      LOG_BUF_APPEND("<-ERGSensitivity");
      int pcf = userConfig->getERGSensitivity() * 10;
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le16(&returnValue[2], static_cast<uint16_t>(pcf));
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->setERGSensitivity(get_le16(&pData[2]) / 10.0f);
        LOG_BUF_APPEND("(%f)", userConfig->getERGSensitivity());
      }
    } break;

    case BLE_shiftDir:  // 0x20
      LOG_BUF_APPEND("<-ShiftDir");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(userConfig->getShifterDir());
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->setShifterDir(static_cast<bool>(rxValue[2]));
        LOG_BUF_APPEND("(%s)", userConfig->getShifterDir() ? "Normal" : "Reverse");
      }
      break;
      ///////////////
    case BLE_minBrakeWatts:  // 0x21
      LOG_BUF_APPEND("<-MinWatts");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le16(&returnValue[2], static_cast<uint16_t>(userConfig->getMinWatts()));
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->setMinWatts(get_le16(&pData[2]));
        LOG_BUF_APPEND("(%d)", userConfig->getMinWatts());
#ifdef CUSTOM_CHAR_DEBUG
        SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "%s", logBuf);
#endif
      }
      break;
    case BLE_maxBrakeWatts:  // 0x22
      LOG_BUF_APPEND("<-MaxWatts");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le16(&returnValue[2], static_cast<uint16_t>(userConfig->getMaxWatts()));
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->setMaxWatts(get_le16(&pData[2]));
        LOG_BUF_APPEND("(%d)", userConfig->getMaxWatts());
#ifdef CUSTOM_CHAR_DEBUG
        SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "%s", logBuf);
#endif
      }
      break;
    case BLE_restartBLE:  // 0x23
      LOG_BUF_APPEND("<-restart BLE");
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        spinBLEClient.reconnectAllDevices();
      }
      break;
    case BLE_scanBLE:  // 0x24
      LOG_BUF_APPEND("<-scan BLE");
      if (rxValue[0] == cc_write) {
        returnValue[0]       = cc_success;
        spinBLEClient.doScan = true;
      }
      break;
    case BLE_firmwareVer:  // 0x25
      LOG_BUF_APPEND("<-Firmware Version");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnString   = FIRMWARE_VERSION;
      }
      break;
    case BLE_resetPowerTable:  // 0x26
      LOG_BUF_APPEND("<-Reset PTab");
      if (rxValue[0] == cc_write) {
        returnValue[0]            = cc_success;
        ss2k->resetPowerTableFlag = true;
      }
      break;
    case BLE_powerTableData:  // 0x27
      LOG_BUF_APPEND("<-Power Tab Data");
      if (rxValue[0] == cc_read) {
        int row = 6;  // 90rpm
        if (rxValue[2] >= 0 && rxValue[2] < POWERTABLE_CAD_SIZE) {
          row = rxValue[2];
        }
        returnString += (uint8_t)row;
        for (int i = 0; i < POWERTABLE_WATT_SIZE; i++) {
          uint8_t encodedPosition[2];
          put_le16s(encodedPosition, powerTable->ptData.tableRow[row].tableEntry[i].targetPosition);
          returnString.append(reinterpret_cast<const char*>(encodedPosition), sizeof(encodedPosition));
        }
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        if (rxValue[2] >= 0 && rxValue[2] < POWERTABLE_CAD_SIZE) {
          const bool firstRow = !powerTable->saveFlag;
          for (int i = 0; i < POWERTABLE_WATT_SIZE; i++) {
            auto& entry          = powerTable->ptData.tableRow[rxValue[2]].tableEntry[i];
            entry                = TableEntry{};
            entry.targetPosition = get_le16s(&pData[i * 2 + 3]);
            if (entry.targetPosition != INT16_MIN) entry.readings = MINIMUM_RELIABLE_POSITIONS + 1;
          }
          ++powerTable->positionEpoch;
          powerTable->_hasBeenLoadedThisSession = true;
          powerTable->saveFlag                  = true;
          // Saved tables all use hMin of Zero and this is not set by the app.
          userConfig->setHMin(0);
          // Startup homing preserves the table while BLE receives more rows.
          if (firstRow && !spinBLEServer.spinDownFlag) spinBLEServer.spinDownFlag = 1;
        } else {
          // SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "Table row invalid");
          //  Logging causes crashes in ISR
        }
      }
      break;
    case BLE_simulatedTargetWatts:  // 0x28
      LOG_BUF_APPEND("<-targetWatts");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le16(&returnValue[2], static_cast<uint16_t>(rtConfig->watts.getTarget()));
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->watts.setTarget(get_le16(&pData[2]));
        LOG_BUF_APPEND("(%d)", rtConfig->watts.getTarget());
      }
      break;
    case BLE_simulateTargetWatts:  // 0x29
      LOG_BUF_APPEND("<-simulatetargetwatts");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(rtConfig->getSimTargetWatts());
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->setSimTargetWatts(rxValue[2]);
        LOG_BUF_APPEND("(%s)", rtConfig->getSimTargetWatts() ? "true" : "false");
      }
      break;
    case BLE_hMin:  // 0x2A
      LOG_BUF_APPEND("<-hMin");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le32s(&returnValue[2], userConfig->getHMin());
        returnLength += 4;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        int32_t hMin   = get_le32s(&pData[2]);
        userConfig->setHMin(hMin);
        rtConfig->setMinStep(hMin);
        LOG_BUF_APPEND(" (%d)", hMin);
      }
      break;

    case BLE_hMax:  // 0x2B
      LOG_BUF_APPEND("<-hMax");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le32s(&returnValue[2], userConfig->getHMax());
        returnLength += 4;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        int32_t hMax   = get_le32s(&pData[2]);
        Serial.printf("hMax: %d\n <--------------------------------------------", hMax);
        userConfig->setHMax(hMax);
        rtConfig->setMaxStep(hMax);
        LOG_BUF_APPEND(" (%d)", userConfig->getHMax());
      }
      break;

    case BLE_homingSensitivity:  // 0x2C
      LOG_BUF_APPEND("<-homingSensitivity");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        put_le16(&returnValue[2], static_cast<uint16_t>(userConfig->getHomingSensitivity()));
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->setHomingSensitivity(get_le16(&pData[2]));
        LOG_BUF_APPEND("(%d)", userConfig->getHomingSensitivity());
      }
      break;

    case BLE_pTab4Pwr:  // 0x2D
      LOG_BUF_APPEND("<-pTab4Pwr");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(userConfig->getPTab4Pwr());
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->setPTab4Pwr(rxValue[2]);
        LOG_BUF_APPEND("(%s)", userConfig->getPTab4Pwr() ? "true" : "false");
      }
      break;

    case BLE_UDPLogging:  // 0x2E
      LOG_BUF_APPEND("<-UDPLogging");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(userConfig->getUdpLogEnabled());
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->setUdpLogEnabled(rxValue[2]);
        LOG_BUF_APPEND("(%s)", userConfig->getUdpLogEnabled() ? "true" : "false");
      }
      break;

    case BLE_hardwareVersion:  // 0x2F
      LOG_BUF_APPEND("<-Hardware Version");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnString   = currentBoard.name.c_str();
      }
      break;

    case BLE_BLELogging:  // 0x30
      LOG_BUF_APPEND("<-BLELogging");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnString   = bleAppender.getLastMessage();
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->setBleLogEnabled(rxValue[2]);
        LOG_BUF_APPEND("(%s)", rtConfig->getBleLogEnabled() ? "true" : "false");
      }
      break;

    case BLE_gearTeeth: {
      returnLength = 2;
      if (rxValue[0] == cc_write) {
        VirtualGearing::Gears next;
        if (next.decode(pData + 2, rxValue.size() - 2)) {
          userConfig->setGearProfile(next);
          returnValue[0] = cc_success;
          returnValue[2] = next.count;
          returnLength   = 3;
        }
      } else if (rxValue[0] == cc_read && (rxValue.size() == 2 || rxValue.size() == 3)) {
        const VirtualGearing::Gears gears = userConfig->getGearProfile();
        // A metadata read/changed notification fits every ATT MTU. Indexed
        // reads return one tooth pair; BLE_allSettings returns the complete array.
        if (gears.preset == VirtualGearing::CUSTOM_TEETH && (rxValue.size() == 2 || pData[2] < gears.count)) {
          returnValue[0] = cc_success;
          returnValue[2] = gears.count;
          returnLength   = 3;
          if (rxValue.size() == 3) {
            returnValue[3] = pData[2];
            put_le16(&returnValue[4], gears.teeth[pData[2]]);
            returnLength = 6;
          }
        }
      }
      break;
    }

    case BLE_gearPreset: {
      returnLength = 2;
      if ((rxValue[0] == cc_read && rxValue.size() == 2) ||
          (rxValue[0] == cc_write && rxValue.size() == 4 && userConfig->setGearPreset(get_le16(pData + 2)))) {
        returnValue[0] = cc_success;
        put_le16(&returnValue[2], userConfig->getGearProfile().preset);
        returnLength = 4;
      }
      break;
    }

    case BLE_capabilities:  // 0x36
      returnLength = 2;
      if (rxValue[0] == cc_read && rxValue.size() == 2) {
        returnValue.resize(7);  // The default 2-byte-read buffer is 6 bytes.
        returnValue[0] = cc_success;
        returnValue[2] = CUSTOM_CAPABILITIES_VERSION;
        put_le32(&returnValue[3], customCharacteristicCapabilities() | SS2K_CAP_DIRCON_OTA_2K | SS2K_CAP_BLE_OTA_WINDOW4);
        returnLength = 7;
      }
      break;

    default:
      LOG_BUF_APPEND("<-Unknown Characteristic");
      returnValue[0] = cc_error;
      break;
  }

#ifdef CUSTOM_CHAR_DEBUG
  SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "%s", logBuf);
#endif
  if (returnString == "") {
    returnValue.resize(returnLength);
  } else {  // Need to send a string instead
    returnValue.resize(returnString.length() + 2);
    returnValue[0] = cc_success;
    returnValue[1] = rxValue[1];
    std::copy(returnString.begin(), returnString.end(), returnValue.begin() + 2);

  }

  return returnValue;
}

// iterate through all smartspin user parameters and notify the specific one if changed
void BLE_ss2kCustomCharacteristic::parseNemit() {
  static userParameters _oldParams;
  static RuntimeParameters _oldRTParams;

  const VirtualGearing::Gears gears = userConfig->getGearProfile();
  if (!(gears == _oldParams.getGearProfile())) {
    _oldParams.setGearProfile(gears);
    BLE_ss2kCustomCharacteristic::notify(BLE_gearPreset);
    if (gears.preset == VirtualGearing::CUSTOM_TEETH) BLE_ss2kCustomCharacteristic::notify(BLE_gearTeeth);
    return;
  }

  if (strcmp(userConfig->getDeviceName(), _oldParams.getDeviceName()) != 0) {
    _oldParams.setDeviceName(userConfig->getDeviceName());
    BLE_ss2kCustomCharacteristic::notify(BLE_deviceName);
    return;
  }

  if (userConfig->getShiftStep() != _oldParams.getShiftStep()) {
    _oldParams.setShiftStep(userConfig->getShiftStep());
    BLE_ss2kCustomCharacteristic::notify(BLE_shiftStep);
    return;
  }

  if (userConfig->getStealthChop() != _oldParams.getStealthChop()) {
    _oldParams.setStealthChop(userConfig->getStealthChop());
    BLE_ss2kCustomCharacteristic::notify(BLE_stealthChop);
    return;
  }

  if (userConfig->getInclineMultiplier() != _oldParams.getInclineMultiplier()) {
    _oldParams.setInclineMultiplier(userConfig->getInclineMultiplier());
    BLE_ss2kCustomCharacteristic::notify(BLE_inclineMultiplier);
    return;
  }

  if (userConfig->getPowerCorrectionFactor() != _oldParams.getPowerCorrectionFactor()) {
    _oldParams.setPowerCorrectionFactor(userConfig->getPowerCorrectionFactor());
    BLE_ss2kCustomCharacteristic::notify(BLE_powerCorrectionFactor);
    return;
  }

  if (strcmp(userConfig->getSsid(), _oldParams.getSsid()) != 0) {
    _oldParams.setSsid(userConfig->getSsid());
    BLE_ss2kCustomCharacteristic::notify(BLE_ssid);
    return;
  }

  if (strcmp(userConfig->getConnectedPowerMeter(), _oldParams.getConnectedPowerMeter()) != 0) {
    _oldParams.setConnectedPowerMeter(userConfig->getConnectedPowerMeter());
    BLE_ss2kCustomCharacteristic::notify(BLE_connectedPowerMeter);
    return;
  }

  if (strcmp(userConfig->getConnectedHeartMonitor(), _oldParams.getConnectedHeartMonitor()) != 0) {
    _oldParams.setConnectedHeartMonitor(userConfig->getConnectedHeartMonitor());
    BLE_ss2kCustomCharacteristic::notify(BLE_connectedHeartMonitor);
    return;
  }

  if (userConfig->getStepperPower() != _oldParams.getStepperPower()) {
    _oldParams.setStepperPower(userConfig->getStepperPower());
    BLE_ss2kCustomCharacteristic::notify(BLE_stepperPower);
    return;
  }

  if (userConfig->getStepperSpeed() != _oldParams.getStepperSpeed()) {
    _oldParams.setStepperSpeed(userConfig->getStepperSpeed());
    BLE_ss2kCustomCharacteristic::notify(BLE_stepperSpeed);
    return;
  }

  if (userConfig->getERGSensitivity() != _oldParams.getERGSensitivity()) {
    _oldParams.setERGSensitivity(userConfig->getERGSensitivity());
    BLE_ss2kCustomCharacteristic::notify(BLE_ERGSensitivity);
    return;
  }

  if (strcmp(userConfig->getFoundDevices(), _oldParams.getFoundDevices()) != 0) {
    _oldParams.setFoundDevices(userConfig->getFoundDevices());
    BLE_ss2kCustomCharacteristic::notify(BLE_foundDevices);
    return;
  }

  if (userConfig->getMinWatts() != _oldParams.getMinWatts()) {
    _oldParams.setMinWatts(userConfig->getMinWatts());
    BLE_ss2kCustomCharacteristic::notify(BLE_minBrakeWatts);
    return;
  }

  if (userConfig->getMaxWatts() != _oldParams.getMaxWatts()) {
    _oldParams.setMaxWatts(userConfig->getMaxWatts());
    BLE_ss2kCustomCharacteristic::notify(BLE_maxBrakeWatts);
    return;
  }
  if (userConfig->getShifterDir() != _oldParams.getShifterDir()) {
    _oldParams.setShifterDir(userConfig->getShifterDir());
    BLE_ss2kCustomCharacteristic::notify(BLE_shiftDir);
    return;
  }
  if (rtConfig->getFTMSMode() != _oldRTParams.getFTMSMode()) {
    _oldRTParams.setFTMSMode(rtConfig->getFTMSMode());
    BLE_ss2kCustomCharacteristic::notify(BLE_FTMSMode);
    return;
  }
  if (rtConfig->watts.getTarget() != _oldRTParams.watts.getTarget()) {
    _oldRTParams.watts.setTarget(rtConfig->watts.getTarget());
    BLE_ss2kCustomCharacteristic::notify(BLE_simulatedTargetWatts);
    return;
  }
  if (rtConfig->getSimTargetWatts() != _oldRTParams.getSimTargetWatts()) {
    _oldRTParams.setSimTargetWatts(rtConfig->getSimTargetWatts());
    BLE_ss2kCustomCharacteristic::notify(BLE_simulateTargetWatts);
    return;
  }
  if (userConfig->getHMin() != _oldParams.getHMin()) {
    _oldParams.setHMin(userConfig->getHMin());
    BLE_ss2kCustomCharacteristic::notify(BLE_hMin);
    userConfig->saveToLittleFS();
    return;
  }
  if (userConfig->getHMax() != _oldParams.getHMax()) {
    _oldParams.setHMax(userConfig->getHMax());
    BLE_ss2kCustomCharacteristic::notify(BLE_hMax);
    userConfig->saveToLittleFS();
    return;
  }
  if (userConfig->getHomingSensitivity() != _oldParams.getHomingSensitivity()) {
    _oldParams.setHomingSensitivity(userConfig->getHomingSensitivity());
    BLE_ss2kCustomCharacteristic::notify(BLE_homingSensitivity);
    return;
  }
  if (userConfig->getPTab4Pwr() != _oldParams.getPTab4Pwr()) {
    _oldParams.setPTab4Pwr(userConfig->getPTab4Pwr());
    BLE_ss2kCustomCharacteristic::notify(BLE_pTab4Pwr);
    // Home whenever this value is flipped true
    if (userConfig->getPTab4Pwr()) {
      spinBLEServer.spinDownFlag = 1;
    }
    return;
  }
  if (userConfig->getUdpLogEnabled() != _oldParams.getUdpLogEnabled()) {
    _oldParams.setUdpLogEnabled(userConfig->getUdpLogEnabled());
    BLE_ss2kCustomCharacteristic::notify(BLE_UDPLogging);
    return;
  }
}
