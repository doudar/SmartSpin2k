/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BLE_OpenBikeControl_Service.h"
#include "DirConManager.h"
#include "Main.h"
#include "SS2KLog.h"
#include <Constants.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <cctype>
#include <cstring>

#define OPENBIKECONTROL_MDNS_SERVICE_NAME     "_openbikecontrol"
#define OPENBIKECONTROL_MDNS_SERVICE_PROTOCOL "tcp"

namespace {
constexpr uint8_t kOpenBikeControlButtonStateMessageType = 0x01;
constexpr uint8_t kOpenBikeControlAppInfoMessageType     = 0x04;
constexpr uint8_t kOpenBikeControlShiftUpButtonId        = 0x01;
constexpr uint8_t kOpenBikeControlShiftDownButtonId      = 0x02;
constexpr uint8_t kOpenBikeControlGearSetButtonId        = 0x03;
constexpr uint8_t kOpenBikeControlButtonReleasedState    = 0x00;
constexpr uint8_t kOpenBikeControlButtonPressedState     = 0x01;
constexpr unsigned long kOpenBikeControlButtonPressMs     = 50;
constexpr char kOpenBikeControlLogTag[]                  = "BLE_OBC";
}  // namespace

static char fullUuidListBuffer[512] = "";
static size_t fullUuidListLength    = 0;
static bool openBikeControlServiceSetupCalled = false;
static bool openBikeControlMdnsStarted        = false;

static OpenBikeControlHapticCallbacks obcHapticCallbacks;
static OpenBikeControlAppInfoCallbacks obcAppInfoCallbacks;
static OpenBikeControlButtonStateCallbacks obcButtonStateCallbacks;

BLE_OpenBikeControl_Service::BLE_OpenBikeControl_Service()
    : pOpenBikeControlService(nullptr),
      buttonStateCharacteristic(nullptr),
      hapticFeedbackCharacteristic(nullptr),
      appInformationCharacteristic(nullptr),
      _shiftUpReleaseAtMs(0),
      _shiftDownReleaseAtMs(0),
      _shiftUpReleasePending(false),
      _shiftDownReleasePending(false),
      _gearSetPending(false) {}

void BLE_OpenBikeControl_Service::setupMDNS() {
  if (!openBikeControlServiceSetupCalled || openBikeControlMdnsStarted) {
    return;
  }

  static char macAddress[18];  // MAC format: 11:22:33:44:55:66\0
  static char obcId[13];       // aabbccddeeff\0

  strcpy(macAddress, WiFi.macAddress().c_str());
  size_t out = 0;
  for (size_t i = 0; macAddress[i] != '\0' && out < sizeof(obcId) - 1; i++) {
    if (macAddress[i] != ':') {
      obcId[out++] = static_cast<char>(tolower(static_cast<unsigned char>(macAddress[i])));
    }
  }
  obcId[out] = '\0';

  SS2K_LOG(kOpenBikeControlLogTag, "Adding OpenBikeControl MDNS service: %s.%s on port %d", OPENBIKECONTROL_MDNS_SERVICE_NAME,
           OPENBIKECONTROL_MDNS_SERVICE_PROTOCOL, DIRCON_TCP_PORT);
  if (MDNS.addService(OPENBIKECONTROL_MDNS_SERVICE_NAME, OPENBIKECONTROL_MDNS_SERVICE_PROTOCOL, DIRCON_TCP_PORT)) {
    openBikeControlMdnsStarted = true;
    MDNS.addServiceTxt(OPENBIKECONTROL_MDNS_SERVICE_NAME, OPENBIKECONTROL_MDNS_SERVICE_PROTOCOL, "version", "1");
    MDNS.addServiceTxt(OPENBIKECONTROL_MDNS_SERVICE_NAME, OPENBIKECONTROL_MDNS_SERVICE_PROTOCOL, "id", static_cast<const char *>(obcId));
    MDNS.addServiceTxt(OPENBIKECONTROL_MDNS_SERVICE_NAME, OPENBIKECONTROL_MDNS_SERVICE_PROTOCOL, "name", userConfig->getDeviceName());
    MDNS.addServiceTxt(OPENBIKECONTROL_MDNS_SERVICE_NAME, OPENBIKECONTROL_MDNS_SERVICE_PROTOCOL, "service-uuids", "");
    MDNS.addServiceTxt(OPENBIKECONTROL_MDNS_SERVICE_NAME, OPENBIKECONTROL_MDNS_SERVICE_PROTOCOL, "manufacturer", "SmartSpin2k");
    MDNS.addServiceTxt(OPENBIKECONTROL_MDNS_SERVICE_NAME, OPENBIKECONTROL_MDNS_SERVICE_PROTOCOL, "model", "SmartSpin2k");
  } else {
    SS2K_LOG(kOpenBikeControlLogTag, "Failed to add OpenBikeControl MDNS service");
  }
}

void BLE_OpenBikeControl_Service::addServiceUuidToMDNS(const NimBLEUUID& serviceUuid) {
  if (!openBikeControlServiceSetupCalled) {
    return;
  }

  setupMDNS();
  if (!openBikeControlMdnsStarted) {
    return;
  }

  std::string fullUuidStr = serviceUuid.toString();
  const char *fullUuid    = fullUuidStr.c_str();

  if (strstr(fullUuidListBuffer, fullUuid) != nullptr) {
    return;
  }

  size_t fullUuidLen      = strlen(fullUuid);
  bool needFullUuidComma  = (fullUuidListLength > 0);
  size_t fullRequiredSize = fullUuidLen + (needFullUuidComma ? 1 : 0);
  if (fullUuidListLength + fullRequiredSize < sizeof(fullUuidListBuffer) - 1) {
    if (needFullUuidComma) {
      fullUuidListBuffer[fullUuidListLength++] = ',';
    }
    strcpy(&fullUuidListBuffer[fullUuidListLength], fullUuid);
    fullUuidListLength += fullUuidLen;
    MDNS.addServiceTxt(OPENBIKECONTROL_MDNS_SERVICE_NAME, OPENBIKECONTROL_MDNS_SERVICE_PROTOCOL, "service-uuids", (const char *)fullUuidListBuffer);
  } else {
    SS2K_LOG(kOpenBikeControlLogTag, "Warning: Not enough space to add full UUID %s", fullUuid);
  }
}

void BLE_OpenBikeControl_Service::setupService(NimBLEServer *pServer) {
  openBikeControlServiceSetupCalled = true;

  pOpenBikeControlService = pServer->createService(OPENBIKECONTROL_SERVICE_UUID);

  buttonStateCharacteristic = pOpenBikeControlService->createCharacteristic(OPENBIKECONTROL_BUTTON_STATE_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  buttonStateCharacteristic->setCallbacks(&obcButtonStateCallbacks);

  const uint8_t initialButtonState[] = {kOpenBikeControlButtonStateMessageType, kOpenBikeControlShiftUpButtonId, kOpenBikeControlButtonReleasedState,
                                        kOpenBikeControlShiftDownButtonId, kOpenBikeControlButtonReleasedState};
  buttonStateCharacteristic->setValue(initialButtonState, sizeof(initialButtonState));

  hapticFeedbackCharacteristic =
      pOpenBikeControlService->createCharacteristic(OPENBIKECONTROL_HAPTIC_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  hapticFeedbackCharacteristic->setCallbacks(&obcHapticCallbacks);

  appInformationCharacteristic =
      pOpenBikeControlService->createCharacteristic(OPENBIKECONTROL_APP_INFO_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  appInformationCharacteristic->setCallbacks(&obcAppInfoCallbacks);

  pOpenBikeControlService->start();

  DirConManager::registerService(
      pOpenBikeControlService->getUUID(),
      [](NimBLECharacteristic *characteristic, const uint8_t *data, size_t length, DirConWriteResult *result) -> bool {
        if (characteristic->getUUID().equals(NimBLEUUID(OPENBIKECONTROL_HAPTIC_CHARACTERISTIC_UUID))) {
          openBikeControlService.handleHapticWrite(data, length, true);
          result->autoSubscribeUuids[0] = NimBLEUUID(OPENBIKECONTROL_BUTTON_STATE_CHARACTERISTIC_UUID);
          result->autoSubscribeCount    = 1;
          return true;
        }
        if (characteristic->getUUID().equals(NimBLEUUID(OPENBIKECONTROL_APP_INFO_CHARACTERISTIC_UUID))) {
          openBikeControlService.handleAppInfoWrite(data, length, true);
          result->autoSubscribeUuids[0] = NimBLEUUID(OPENBIKECONTROL_BUTTON_STATE_CHARACTERISTIC_UUID);
          result->autoSubscribeCount    = 1;
          return true;
        }
        return false;
      },
      BLE_OpenBikeControl_Service::addServiceUuidToMDNS);

  SS2K_LOG(kOpenBikeControlLogTag, "OpenBikeControl service started");
}

void BLE_OpenBikeControl_Service::update() {
  unsigned long now = millis();

  if (_shiftUpReleasePending && static_cast<long>(now - _shiftUpReleaseAtMs) >= 0) {
    _shiftUpReleasePending = false;
    sendButtonState(kOpenBikeControlShiftUpButtonId, kOpenBikeControlButtonReleasedState);
  }

  if (_shiftDownReleasePending && static_cast<long>(now - _shiftDownReleaseAtMs) >= 0) {
    _shiftDownReleasePending = false;
    sendButtonState(kOpenBikeControlShiftDownButtonId, kOpenBikeControlButtonReleasedState);
  }

  if (_gearSetPending) {
    _gearSetPending = false;
    sendGearSet(rtConfig->getShifterPosition());
  }
}

void BLE_OpenBikeControl_Service::sendButtonState(uint8_t buttonId, uint8_t state) {
  if (buttonStateCharacteristic == nullptr) {
    return;
  }

  const uint8_t payload[] = {kOpenBikeControlButtonStateMessageType, buttonId, state};
  buttonStateCharacteristic->setValue(payload, sizeof(payload));
  buttonStateCharacteristic->notify();
  DirConManager::notifyCharacteristic(NimBLEUUID(OPENBIKECONTROL_SERVICE_UUID), buttonStateCharacteristic->getUUID(), const_cast<uint8_t *>(payload),
                                      sizeof(payload));
}

void BLE_OpenBikeControl_Service::scheduleButtonRelease(uint8_t buttonId) {
  unsigned long releaseAtMs = millis() + kOpenBikeControlButtonPressMs;

  if (buttonId == kOpenBikeControlShiftUpButtonId) {
    _shiftUpReleaseAtMs      = releaseAtMs;
    _shiftUpReleasePending   = true;
  } else if (buttonId == kOpenBikeControlShiftDownButtonId) {
    _shiftDownReleaseAtMs    = releaseAtMs;
    _shiftDownReleasePending = true;
  }
}

void BLE_OpenBikeControl_Service::sendShiftUp() {
  sendButtonState(kOpenBikeControlShiftUpButtonId, kOpenBikeControlButtonPressedState);
  scheduleButtonRelease(kOpenBikeControlShiftUpButtonId);
}

void BLE_OpenBikeControl_Service::sendShiftDown() {
  sendButtonState(kOpenBikeControlShiftDownButtonId, kOpenBikeControlButtonPressedState);
  scheduleButtonRelease(kOpenBikeControlShiftDownButtonId);
}

void BLE_OpenBikeControl_Service::sendGearSet(int gear) {
  uint8_t gearValue = static_cast<uint8_t>(constrain(gear, 0, 255));
  sendButtonState(kOpenBikeControlGearSetButtonId, gearValue);
}

void BLE_OpenBikeControl_Service::handleHapticWrite(const uint8_t *data, size_t length, bool isDirCon) {
  if (data == nullptr) {
    SS2K_LOG(kOpenBikeControlLogTag, "Ignoring null haptic write");
    return;
  }

  std::string payload = toHexString(data, length);
  SS2K_LOG(kOpenBikeControlLogTag, "Haptic write from %s len=%zu data=%s", isDirCon ? "DirCon" : "BLE", length, payload.c_str());
}

void BLE_OpenBikeControl_Service::handleAppInfoWrite(const uint8_t *data, size_t length, bool isDirCon) {
  if (data == nullptr || length < 2) {
    SS2K_LOG(kOpenBikeControlLogTag, "Ignoring short app info write");
    return;
  }
  if (data[0] != kOpenBikeControlAppInfoMessageType) {
    SS2K_LOG(kOpenBikeControlLogTag, "Ignoring invalid app info message type 0x%02X", data[0]);
    return;
  }

  SS2K_LOG(kOpenBikeControlLogTag, "App info update from %s (len=%d, version=%u)", isDirCon ? "DirCon" : "BLE", length, data[1]);
  _gearSetPending = true;
}

void BLE_OpenBikeControl_Service::handleButtonStateSubscription(uint16_t subValue) {
  if (subValue > 0) {
    _gearSetPending = true;
  }
}

void OpenBikeControlHapticCallbacks::onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) {
  NimBLEAttValue value = pCharacteristic->getValue();
  openBikeControlService.handleHapticWrite(value.data(), value.length());
  SS2K_LOG(kOpenBikeControlLogTag, "Haptic write from %s", connInfo.getAddress().toString().c_str());
}

void OpenBikeControlAppInfoCallbacks::onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) {
  NimBLEAttValue value = pCharacteristic->getValue();
  openBikeControlService.handleAppInfoWrite(value.data(), value.length());
  SS2K_LOG(kOpenBikeControlLogTag, "App info write from %s", connInfo.getAddress().toString().c_str());
}

void OpenBikeControlButtonStateCallbacks::onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue) {
  openBikeControlService.handleButtonStateSubscription(subValue);
  SS2K_LOG(kOpenBikeControlLogTag, "Button state subscription change on %s from %s: %d", pCharacteristic->getUUID().toString().c_str(),
           connInfo.getAddress().toString().c_str(), subValue);
}
