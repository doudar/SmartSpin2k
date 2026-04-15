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

namespace {
constexpr uint8_t kOpenBikeControlButtonStateMessageType = 0x01;
constexpr uint8_t kOpenBikeControlHapticMessageType      = 0x03;
constexpr uint8_t kOpenBikeControlAppInfoMessageType     = 0x04;
constexpr uint8_t kOpenBikeControlShiftUpButtonId        = 0x01;
constexpr uint8_t kOpenBikeControlShiftDownButtonId      = 0x02;
constexpr uint8_t kOpenBikeControlButtonReleasedState    = 0x00;
constexpr uint8_t kOpenBikeControlButtonPressedState     = 0x01;
constexpr unsigned long kOpenBikeControlSessionTimeoutMs = 15000;
constexpr char kOpenBikeControlLogTag[]                  = "BLE_OBC";
}  // namespace

static OpenBikeControlHapticCallbacks obcHapticCallbacks;
static OpenBikeControlAppInfoCallbacks obcAppInfoCallbacks;
static OpenBikeControlButtonStateCallbacks obcButtonStateCallbacks;

BLE_OpenBikeControl_Service::BLE_OpenBikeControl_Service()
    : pOpenBikeControlService(nullptr),
      buttonStateCharacteristic(nullptr),
      hapticFeedbackCharacteristic(nullptr),
      appInformationCharacteristic(nullptr),
      _lastClientActivityMs(0) {}

void BLE_OpenBikeControl_Service::setupService(NimBLEServer *pServer) {
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

  DirConManager::registerService(pOpenBikeControlService->getUUID(),
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
                                 });

  SS2K_LOG(kOpenBikeControlLogTag, "OpenBikeControl service started");
}

bool BLE_OpenBikeControl_Service::isConnected() {
  if (_lastClientActivityMs == 0) {
    return false;
  }
  return (millis() - _lastClientActivityMs) < kOpenBikeControlSessionTimeoutMs;
}

void BLE_OpenBikeControl_Service::markClientActivity() { _lastClientActivityMs = millis(); }

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

void BLE_OpenBikeControl_Service::sendShiftUp() {
  sendButtonState(kOpenBikeControlShiftUpButtonId, kOpenBikeControlButtonPressedState);
  vTaskDelay(50 / portTICK_PERIOD_MS);
  sendButtonState(kOpenBikeControlShiftUpButtonId, kOpenBikeControlButtonReleasedState);
}

void BLE_OpenBikeControl_Service::sendShiftDown() {
  sendButtonState(kOpenBikeControlShiftDownButtonId, kOpenBikeControlButtonPressedState);
  vTaskDelay(50 / portTICK_PERIOD_MS);
  sendButtonState(kOpenBikeControlShiftDownButtonId, kOpenBikeControlButtonReleasedState);
}

void BLE_OpenBikeControl_Service::handleHapticWrite(const uint8_t *data, size_t length, bool isDirCon) {
  if (data == nullptr || length < 4) {
    SS2K_LOG(kOpenBikeControlLogTag, "Ignoring short haptic write");
    return;
  }
  if (data[0] != kOpenBikeControlHapticMessageType) {
    SS2K_LOG(kOpenBikeControlLogTag, "Ignoring invalid haptic message type 0x%02X", data[0]);
    return;
  }

  markClientActivity();
  SS2K_LOG(kOpenBikeControlLogTag, "Haptic command from %s pattern=%u duration=%u intensity=%u", isDirCon ? "DirCon" : "BLE", data[1], data[2], data[3]);
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

  markClientActivity();
  SS2K_LOG(kOpenBikeControlLogTag, "App info update from %s (len=%d, version=%u)", isDirCon ? "DirCon" : "BLE", length, data[1]);
}

void BLE_OpenBikeControl_Service::handleButtonStateSubscription(uint16_t subValue) {
  if (subValue > 0) {
    markClientActivity();
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
