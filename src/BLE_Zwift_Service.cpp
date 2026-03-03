/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BLE_Zwift_Service.h"
#include "Main.h"
#include "SS2KLog.h"
#include "DirConManager.h"
#include <Constants.h>

// "RideOn" handshake bytes
static const char RideOn[7] = "RideOn";
// static const uint8_t RIDE_ON[] = {0x52, 0x69, 0x64, 0x65, 0x4F, 0x6E};
// static const size_t RIDE_ON_LEN = sizeof(RIDE_ON);

// Response type bytes appended after RideOn for trainer emulation
static const uint8_t RESPONSE_START[] = {0x02, 0x00};
static const size_t RESPONSE_START_LEN = sizeof(RESPONSE_START);

// // Async RideOn answer (protobuf-encoded "RIDE_ON(0)") - sent on async after handshake
// static const uint8_t ASYNC_RIDEON_ANSWER[] = {
//     0x2a, 0x08, 0x03, 0x12, 0x0d, 0x22, 0x0b,
//     0x52, 0x49, 0x44, 0x45, 0x5f, 0x4f, 0x4e, 0x28, 0x30, 0x29,
//     0x00};
// static const size_t ASYNC_RIDEON_ANSWER_LEN = sizeof(ASYNC_RIDEON_ANSWER);

// Pre-computed "no buttons pressed" Ride notification (opcode 0x23 + bitmap + analog data)
static const uint8_t ALL_RELEASED[] = {
    ZWIFT_CONTROLLER_NOTIFICATION_OPCODE,       // 0x23
    0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F,         // field 1: bitmap = 0xFFFFFFFF (all released)
    0x12, 0x18,                                  // field 2: length-delimited, 24 bytes
    0x0A, 0x04, 0x08, 0x00, 0x10, 0x00,          // analog 0 (LEFT): value 0
    0x0A, 0x04, 0x08, 0x01, 0x10, 0x00,          // analog 1 (RIGHT): value 0
    0x0A, 0x04, 0x08, 0x02, 0x10, 0x00,          // analog 2: value 0
    0x0A, 0x04, 0x08, 0x03, 0x10, 0x00           // analog 3: value 0
};
static const size_t ALL_RELEASED_LEN = sizeof(ALL_RELEASED);

static ZwiftSyncRxCallbacks zwiftSyncRxCallbacks;

BLE_Zwift_Service::BLE_Zwift_Service()
    : pZwiftService(nullptr),
      asyncCharacteristic(nullptr),
      syncRxCharacteristic(nullptr),
      syncTxCharacteristic(nullptr),
      unknownCharacteristic5(nullptr),
      unknownCharacteristic6(nullptr),
      _handshakeComplete(false),
      _lastKeepaliveTime(0),
      _lastRidingDataTime(0),
      _gearRatioX10000(0) {}

void BLE_Zwift_Service::setupService(NimBLEServer *pServer) {
  // Battery Level Service (0x180F) - Zwift expects this on Click controllers
  NimBLEService *pBatteryService = pServer->createService(NimBLEUUID((uint16_t)0x180F));
  NimBLECharacteristic *batteryLevelChar = pBatteryService->createCharacteristic(
      NimBLEUUID((uint16_t)0x2A19),
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  uint8_t batteryLevel = 100;
  batteryLevelChar->setValue(&batteryLevel, 1);
  pBatteryService->start();

  // Zwift Custom Service
  pZwiftService = pServer->createService(ZWIFT_CUSTOM_SERVICE_UUID);

  // Async characteristic: NOTIFY - sends button presses to Zwift
  asyncCharacteristic = pZwiftService->createCharacteristic(
      ZWIFT_ASYNC_CHARACTERISTIC_UUID,
      NIMBLE_PROPERTY::NOTIFY);

  // Sync RX: WRITE_WITHOUT_RESPONSE - receives handshake from Zwift
  syncRxCharacteristic = pZwiftService->createCharacteristic(
      ZWIFT_SYNC_RX_CHARACTERISTIC_UUID,
      NIMBLE_PROPERTY::WRITE_NR);
  syncRxCharacteristic->setCallbacks(&zwiftSyncRxCallbacks);

  // Sync TX: READ | INDICATE - sends handshake response and keepalive
  syncTxCharacteristic = pZwiftService->createCharacteristic(
      ZWIFT_SYNC_TX_CHARACTERISTIC_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::INDICATE);

  // Unknown characteristic 5: NOTIFY
  unknownCharacteristic5 = pZwiftService->createCharacteristic(
      ZWIFT_UNKNOWN_CHARACTERISTIC5_UUID,
      NIMBLE_PROPERTY::NOTIFY);

  // Unknown characteristic 6: READ | WRITE | WRITE_NR | INDICATE
  unknownCharacteristic6 = pZwiftService->createCharacteristic(
      ZWIFT_UNKNOWN_CHARACTERISTIC6_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::INDICATE);

  pZwiftService->start();

  // Add service UUID to DirCon MDNS
  DirConManager::addBleServiceUuid(pZwiftService->getUUID());

  SS2K_LOG(ZWIFT_LOG_TAG, "Zwift Custom Service started");
}

void BLE_Zwift_Service::update() {
  if (!_handshakeComplete) {
    return;
  }

  unsigned long now = millis();

  // Send riding data periodically (trainer protocol only, not Click controller)
  if ((now - _lastRidingDataTime >= ZWIFT_RIDING_DATA_INTERVAL_MS)) {
    sendRidingData();
    _lastRidingDataTime = now;
  }

  // Send keepalive on sync_tx every ZWIFT_KEEPALIVE_INTERVAL_MS
  if (now - _lastKeepaliveTime >= ZWIFT_KEEPALIVE_INTERVAL_MS) {
    sendKeepalive();
    _lastKeepaliveTime = now;
  }
}

bool BLE_Zwift_Service::isConnected() {
  return _handshakeComplete;
}

uint32_t BLE_Zwift_Service::getGearRatioX10000() {
  return _gearRatioX10000;
}

void BLE_Zwift_Service::onClientDisconnect() {
  if (_handshakeComplete) {
    SS2K_LOG(ZWIFT_LOG_TAG, "Zwift client disconnected");
    _handshakeComplete = false;
    _lastKeepaliveTime = 0;
    _lastRidingDataTime = 0;
    _gearRatioX10000 = 0;
  }
}

void BLE_Zwift_Service::sendShiftUp() {
  if (!_handshakeComplete) {
    return;
  }
  SS2K_LOG(ZWIFT_LOG_TAG, "Sending shift up to Zwift");
  sendButtonNotification(ZWIFT_BTN_SHFT_UP_R);
  // Small delay then release - Zwift needs to see the transition
  vTaskDelay(50 / portTICK_PERIOD_MS);
  sendAllButtonsReleased();
}

void BLE_Zwift_Service::sendShiftDown() {
  if (!_handshakeComplete) {
    return;
  }
  SS2K_LOG(ZWIFT_LOG_TAG, "Sending shift down to Zwift");
  sendButtonNotification(ZWIFT_BTN_SHFT_UP_L);
  // Small delay then release
  vTaskDelay(50 / portTICK_PERIOD_MS);
  sendAllButtonsReleased();
}

void BLE_Zwift_Service::sendButtonNotification(uint32_t buttonMask) {
  // Build Ride format: opcode(1) + tag(1) + varint(max5) + analog(26) = max 33 bytes
  uint32_t buttonMap = ~buttonMask & 0xFFFFFFFF;

  static const uint8_t analogData[] = {
      0x12, 0x18,                               // field 2: length-delimited, 24 bytes
      0x0A, 0x04, 0x08, 0x00, 0x10, 0x00,       // analog 0 (LEFT): value 0
      0x0A, 0x04, 0x08, 0x01, 0x10, 0x00,       // analog 1 (RIGHT): value 0
      0x0A, 0x04, 0x08, 0x02, 0x10, 0x00,       // analog 2: value 0
      0x0A, 0x04, 0x08, 0x03, 0x10, 0x00        // analog 3: value 0
  };

  uint8_t buf[33];
  buf[0] = ZWIFT_CONTROLLER_NOTIFICATION_OPCODE;  // 0x23
  buf[1] = 0x08;  // protobuf tag: field 1, varint
  size_t varintLen = encodeVarint32(buttonMap, &buf[2]);
  size_t pos = 2 + varintLen;
  memcpy(&buf[pos], analogData, sizeof(analogData));
  pos += sizeof(analogData);

  asyncCharacteristic->setValue(buf, pos);
  asyncCharacteristic->notify();
  DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_CUSTOM_SERVICE_UUID), asyncCharacteristic->getUUID(), buf, pos);
}

size_t BLE_Zwift_Service::encodeVarint32(uint32_t value, uint8_t *buffer) {
  size_t i = 0;
  while (value > 0x7F) {
    buffer[i++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
    value >>= 7;
  }
  buffer[i++] = static_cast<uint8_t>(value);
  return i;
}

void BLE_Zwift_Service::sendAllButtonsReleased() {
  asyncCharacteristic->setValue(ALL_RELEASED, ALL_RELEASED_LEN);
  asyncCharacteristic->notify();
  DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_CUSTOM_SERVICE_UUID), asyncCharacteristic->getUUID(),
                                      const_cast<uint8_t*>(ALL_RELEASED), ALL_RELEASED_LEN);
}

void BLE_Zwift_Service::sendKeepalive() {
  // Keepalive sent on sync_tx as "no buttons pressed" notification
  syncTxCharacteristic->setValue(ALL_RELEASED, ALL_RELEASED_LEN);
  syncTxCharacteristic->indicate();
  DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_CUSTOM_SERVICE_UUID), syncTxCharacteristic->getUUID(),
                                      const_cast<uint8_t*>(ALL_RELEASED), ALL_RELEASED_LEN);
}

void BLE_Zwift_Service::handleSyncRxWrite(const std::string &value, bool isDirCon) {
  if (value.length() < 2) {
    SS2K_LOG(ZWIFT_LOG_TAG, "Received short write on sync_rx, ignoring");
    return;
  }

  const uint8_t *data = reinterpret_cast<const uint8_t *>(value.data());

  // Check if it starts with "RideOn" (handshake)
  if (value.length() >= 6 && memcmp(data, RideOn, 6) == 0) {
    SS2K_LOG(ZWIFT_LOG_TAG, "Received RideOn handshake from Zwift (%s)", isDirCon ? "DirCon/Trainer" : "BLE/Click");

    // Send sync_tx response (indicate)
    syncTxCharacteristic->setValue({0x52, 0x69, 0x64, 0x65, 0x4F, 0x6E, 0x01, uint8_t(value.length())});
    syncTxCharacteristic->indicate();
    
    if (isDirCon) {
      DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_CUSTOM_SERVICE_UUID), syncTxCharacteristic->getUUID(), (uint8_t*)RideOn, 6, false);
    }

    _handshakeComplete = true;
    _lastKeepaliveTime = millis();
    _lastRidingDataTime = millis();
    _gearRatioX10000 = 0;

    SS2K_LOG(ZWIFT_LOG_TAG, "Handshake complete - %s mode active", isDirCon ? "Trainer" : "Click controller");
    return;
  }

  // Handle Zwift protocol commands (if handshake is complete)
  if (_handshakeComplete) {
    handleZwiftCommand(data, value.length());
  } else {
    SS2K_LOG(ZWIFT_LOG_TAG, "Received command before handshake (opcode=0x%02X), ignoring", data[0]);
  }
}

// ---- Trainer Protocol Methods ----

void BLE_Zwift_Service::sendRidingData() {
  uint8_t buf[48];
  size_t pos = 0;

  buf[pos++] = ZWIFT_TRAINER_RIDING_DATA;  // 0x03

  // Field 1: Power (tag 0x08)
  buf[pos++] = 0x08;
  pos += encodeUleb128(static_cast<uint64_t>(rtConfig->watts.getValue()), &buf[pos]);

  // Field 2: Cadence (tag 0x10)
  buf[pos++] = 0x10;
  pos += encodeUleb128(static_cast<uint64_t>(rtConfig->cad.getValue()), &buf[pos]);

  // Field 3: SpeedX100 (tag 0x18)
  int speedX100 = 0;
  if (rtConfig->getSimulatedSpeed() > 5) {
    speedX100 = static_cast<int>(rtConfig->getSimulatedSpeed() * 100);
  } else {
    speedX100 = static_cast<int>(spinBLEServer.calculateSpeed() * 100);
  }
  buf[pos++] = 0x18;
  pos += encodeUleb128(static_cast<uint64_t>(speedX100), &buf[pos]);

  // Field 4: HR (tag 0x20)
  buf[pos++] = 0x20;
  pos += encodeUleb128(static_cast<uint64_t>(rtConfig->hr.getValue()), &buf[pos]);

  // Field 5: Unknown1 (tag 0x28)
  buf[pos++] = 0x28;
  pos += encodeUleb128(0ULL, &buf[pos]);

  // Field 6: Unknown2 (tag 0x30) - constant 25714 from SHIFTR reference
  buf[pos++] = 0x30;
  pos += encodeUleb128(25714ULL, &buf[pos]);

  asyncCharacteristic->setValue(buf, pos);
  asyncCharacteristic->notify();
  DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_CUSTOM_SERVICE_UUID), asyncCharacteristic->getUUID(), buf, pos);
}

void BLE_Zwift_Service::handleZwiftCommand(const uint8_t *data, size_t length) {
  if (length < 1) return;

  uint8_t opcode = data[0];

  switch (opcode) {
    case 0x00: {  // Info/status request
      // Parse optional parameter from the request
      uint64_t param = 0;
      if (length >= 2) {
        decodeUleb128(&data[1], length - 1, &param);
      }
      SS2K_LOG(ZWIFT_LOG_TAG, "Info request (0x00) param=%llu", param);

      // Respond to gear ratio query (parameter 520 per Makinolo blog)
      if (param == 520 && _gearRatioX10000 > 0) {
        uint8_t resp[16];
        size_t pos = 0;
        resp[pos++] = 0x00;  // Info response opcode
        resp[pos++] = 0x10;  // protobuf field 2 tag (GearRatioX10000)
        pos += encodeUleb128(static_cast<uint64_t>(_gearRatioX10000), &resp[pos]);
        syncTxCharacteristic->setValue(resp, pos);
        syncTxCharacteristic->indicate();
        DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_CUSTOM_SERVICE_UUID), syncTxCharacteristic->getUUID(), resp, pos);
        SS2K_LOG(ZWIFT_LOG_TAG, "Responded with gear ratio: %.4f", _gearRatioX10000 / 10000.0);
      }
      break;
    }

    case ZWIFT_TRAINER_CONTROL: {  // 0x04 - Control command
      uint8_t subtype = data[1];

      switch (subtype) {
        case 0x18: {  // ERG mode target power
          if (length >= 3) {
            uint64_t power = 0;
            decodeUleb128(&data[2], length - 2, &power);
            SS2K_LOG(ZWIFT_LOG_TAG, "ERG power: %dW", static_cast<int>(power));
            rtConfig->setFTMSMode(FitnessMachineControlPointProcedure::SetTargetPower);
            rtConfig->watts.setTarget(static_cast<int>(power));
          }
          break;
        }

        case 0x22: {  // SIM mode - grade/inclination
          if (length >= 4) {
            uint8_t subLen = data[2];
            size_t pos = 3;
            while (pos < static_cast<size_t>(3 + subLen) && pos < length) {
              uint8_t fieldTag = data[pos++];
              uint64_t fieldValue = 0;
              size_t decoded = decodeUleb128(&data[pos], length - pos, &fieldValue);
              if (decoded == 0) break;
              pos += decoded;

              if (fieldTag == 0x10) {  // Field 2: Grade
                int64_t grade = static_cast<int64_t>(fieldValue);
                // Bit 0 is sign flag (Zwift's encoding)
                if (grade & 0x01) {
                  grade ^= 0x01;
                  grade *= -1;
                }
                SS2K_LOG(ZWIFT_LOG_TAG, "SIM grade: %.2f%%", grade / 100.0);
                rtConfig->setFTMSMode(FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters);
                rtConfig->setTargetIncline(static_cast<int>(grade));
              }
            }
          }
          break;
        }

        case 0x2A: {  // Physical params (gear ratio, weights)
          if (length >= 4) {
            uint8_t subLen = data[2];
            size_t pos = 3;
            while (pos < static_cast<size_t>(3 + subLen) && pos < length) {
              uint8_t fieldTag = data[pos++];
              uint64_t fieldValue = 0;
              size_t decoded = decodeUleb128(&data[pos], length - pos, &fieldValue);
              if (decoded == 0) break;
              pos += decoded;

              if (fieldTag == 0x10) {  // Field 2: GearRatioX10000
                _gearRatioX10000 = static_cast<uint32_t>(fieldValue);
                SS2K_LOG(ZWIFT_LOG_TAG, "Gear ratio: %.4f", _gearRatioX10000 / 10000.0);
                applyGearRatio();
              }
              // Fields 0x20 (bike weight) and 0x28 (rider weight) - logged for debugging
              else if (fieldTag == 0x20) {
                SS2K_LOG(ZWIFT_LOG_TAG, "Bike weight: %.2fkg", fieldValue / 100.0);
              } else if (fieldTag == 0x28) {
                SS2K_LOG(ZWIFT_LOG_TAG, "Rider weight: %.2fkg", fieldValue / 100.0);
              }
            }
          }
          break;
        }

        default:
          SS2K_LOG(ZWIFT_LOG_TAG, "Unknown control subtype: 0x%02X", subtype);
          break;
      }
      break;
    }

    case 0x41:  // Unknown request type (similar to info request)
      SS2K_LOG(ZWIFT_LOG_TAG, "Request 0x41");
      break;

    default:
      SS2K_LOG(ZWIFT_LOG_TAG, "Unknown command opcode: 0x%02X", opcode);
      break;
  }
}

void BLE_Zwift_Service::applyGearRatio() {
  if (_gearRatioX10000 == 0) return;

  // Zwift virtual gear ratios (24 gears, from 0.75 to 5.49)
  static const uint32_t gearRatios[] = {
      7500,  8700,  9900,  11100, 12300, 13800, 15300, 16800,
      18600, 20400, 22200, 24000, 26100, 28200, 30300, 32400,
      34900, 37400, 39900, 42400, 45400, 48400, 51400, 54900};
  //static const int kDefaultGearIndex = 11;  // ratio 2.40 (≈ 34T/14T)
  static const int kNumGears = sizeof(gearRatios) / sizeof(gearRatios[0]);

  // Find closest gear index
  int closestIndex = 0;
  uint32_t closestDist = 0xFFFFFFFFU;
  for (int i = 0; i < kNumGears; i++) {
    uint32_t dist = static_cast<uint32_t>(
        abs(static_cast<int>(_gearRatioX10000) - static_cast<int>(gearRatios[i])));
    if (dist < closestDist) {
      closestDist = dist;
      closestIndex = i + 1;
    }
  }

  int newShifterPos = closestIndex;
  rtConfig->setShifterPosition(newShifterPos);
  // Also update lastShifterPosition so FTMSModeShiftModifier doesn't
  // see this Zwift-driven change as a user shift and echo it back.
  ss2k->setLastShifterPosition(newShifterPos);
  SS2K_LOG(ZWIFT_LOG_TAG, "Gear %d -> shifter position %d", closestIndex + 1, newShifterPos);
}

size_t BLE_Zwift_Service::decodeUleb128(const uint8_t *buf, size_t bufLen, uint64_t *result) {
  *result = 0;
  size_t i = 0;
  unsigned shift = 0;
  while (i < bufLen) {
    uint8_t byte = buf[i];
    *result |= static_cast<uint64_t>(byte & 0x7F) << shift;
    i++;
    if ((byte & 0x80) == 0) break;
    shift += 7;
    if (shift >= 64) break;  // overflow protection
  }
  return i;
}

size_t BLE_Zwift_Service::encodeUleb128(uint64_t value, uint8_t *buffer) {
  size_t i = 0;
  do {
    uint8_t byte = static_cast<uint8_t>(value & 0x7F);
    value >>= 7;
    if (value) byte |= 0x80;
    buffer[i++] = byte;
  } while (value);
  return i;
}

// ---- Callbacks ----

void ZwiftSyncRxCallbacks::onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) {
  NimBLEAttValue value = pCharacteristic->getValue();
//log the entire data recieved in 0x
  std::string hexValue;
  for (size_t i = 0; i < value.length(); i++) {
      char buf[3];
      snprintf(buf, sizeof(buf), "%02X", value[i]);
      hexValue.append(buf);
  }
  SS2K_LOG(ZWIFT_LOG_TAG, "Sync RX write from %s (len=%d) %s", connInfo.getAddress().toString().c_str(), value.length(), hexValue.c_str());
  zwiftService.handleSyncRxWrite(value);
}

void ZwiftSyncRxCallbacks::onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue) {
  SS2K_LOG(ZWIFT_LOG_TAG, "Subscription change on %s: %d", pCharacteristic->getUUID().toString().c_str(), subValue);
}
