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

using ZwiftProtocol::CommandCode;
using ZwiftProtocol::RideButtonMask;
using ZwiftProtocol::WireType;
using ZwiftProtocol::makeTag;
using ZwiftProtocol::toUnderlying;

namespace {
constexpr uint8_t kRideAnalogButtonsPayloadSize = 0x18;

constexpr uint8_t kRideKeypadButtonMapTag = makeTag(ZwiftProtocol::RideKeyPadStatus::Field::ButtonMap, WireType::Varint);
constexpr uint8_t kRideKeypadAnalogButtonsTag = makeTag(ZwiftProtocol::RideKeyPadStatus::Field::AnalogButtons, WireType::LengthDelimited);
constexpr uint8_t kRideAnalogGroupStatusTag = makeTag(ZwiftProtocol::RideAnalogKeyGroup::Field::GroupStatus, WireType::LengthDelimited);
constexpr uint8_t kRideAnalogLocationTag = makeTag(ZwiftProtocol::RideAnalogKeyPress::Field::Location, WireType::Varint);
constexpr uint8_t kRideAnalogValueTag = makeTag(ZwiftProtocol::RideAnalogKeyPress::Field::AnalogValue, WireType::Varint);

uint32_t getNextZwiftField5Counter(uint16_t power) {
  const int knownOutputs[] = {2864, 4060, 4636, 6803};
  static int counter = 0;
  if (power > 0) {
    counter ++;
  } else {
    counter = 0;
  }
  return knownOutputs[counter % (sizeof(knownOutputs) / sizeof(knownOutputs[0]))];
}
}  // namespace

// "RideOn" handshake bytes
static const char RideOn[7] = "RideOn";
// static const uint8_t RIDE_ON[] = {0x52, 0x69, 0x64, 0x65, 0x4F, 0x6E};
// static const size_t RIDE_ON_LEN = sizeof(RIDE_ON);

// Response type bytes appended after RideOn for trainer emulation
static const uint8_t RESPONSE_START[]  = {0x02, 0x00};
static const size_t RESPONSE_START_LEN = sizeof(RESPONSE_START);

// Async RideOn answer (protobuf-encoded "RIDE_ON(0)") - sent on async after handshake
static const uint8_t ASYNC_RIDEON_ANSWER[]  = {0x2a, 0x08, 0x03, 0x12, 0x0d, 0x22, 0x0b, 0x52, 0x49, 0x44, 0x45, 0x5f, 0x4f, 0x4e, 0x28, 0x30, 0x29, 0x00};
static const size_t ASYNC_RIDEON_ANSWER_LEN = sizeof(ASYNC_RIDEON_ANSWER);

// Pre-computed "no buttons pressed" Ride notification (opcode 0x23 + bitmap + analog data)
static const uint8_t ALL_RELEASED[] = {
  toUnderlying(CommandCode::RideKeyPadStatus),
  kRideKeypadButtonMapTag,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0x0F,  // field 1: bitmap = 0xFFFFFFFF (all released)
  kRideKeypadAnalogButtonsTag,
  kRideAnalogButtonsPayloadSize,
  kRideAnalogGroupStatusTag,
    0x04,
  kRideAnalogLocationTag,
  toUnderlying(ZwiftProtocol::RideAnalogLocation::Left),
  kRideAnalogValueTag,
    0x00,  // analog 0 (LEFT): value 0
  kRideAnalogGroupStatusTag,
    0x04,
  kRideAnalogLocationTag,
  toUnderlying(ZwiftProtocol::RideAnalogLocation::Right),
  kRideAnalogValueTag,
    0x00,  // analog 1 (RIGHT): value 0
  kRideAnalogGroupStatusTag,
    0x04,
  kRideAnalogLocationTag,
  toUnderlying(ZwiftProtocol::RideAnalogLocation::Up),
  kRideAnalogValueTag,
    0x00,  // analog 2: value 0
  kRideAnalogGroupStatusTag,
    0x04,
  kRideAnalogLocationTag,
  toUnderlying(ZwiftProtocol::RideAnalogLocation::Down),
  kRideAnalogValueTag,
    0x00  // analog 3: value 0
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
      _lastActivityTime(0),
      _lastKeepaliveTime(0),
      _lastRidingDataTime(0),
      _gearRatioX10000(0) {}

const char* BLE_Zwift_Service::getLogTag() const { return isDirCon ? kZwiftDirConLogTag : kZwiftBleLogTag; }

void BLE_Zwift_Service::resetSession() {
  _lastActivityTime    = 0;
  _lastKeepaliveTime   = 0;
  _lastRidingDataTime  = 0;
  _gearRatioX10000     = 0;
}

bool BLE_Zwift_Service::keepAlive(unsigned long now) {
  if ((now - _lastActivityTime >= kZwiftSessionTimeoutMs) && isConnected()) {
    SS2K_LOG(getLogTag(), "Zwift session timed out waiting for keepalive/activity");
    resetSession();
    return false;
  }

  if (now - _lastKeepaliveTime >= kZwiftKeepaliveIntervalMs && isConnected()) {
    // Keepalive sent on sync_tx as "no buttons pressed" notification
    syncTxCharacteristic->setValue(ALL_RELEASED, ALL_RELEASED_LEN);
    syncTxCharacteristic->indicate();
    DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_CUSTOM_SERVICE_UUID), syncTxCharacteristic->getUUID(), const_cast<uint8_t*>(ALL_RELEASED), ALL_RELEASED_LEN);
    _lastKeepaliveTime = now;
  }

  return true;
}

void BLE_Zwift_Service::setupService(NimBLEServer* pServer) {
  // Battery Level Service (0x180F) - Zwift expects this on Click controllers
  NimBLEService* pBatteryService         = pServer->createService(NimBLEUUID((uint16_t)0x180F));
  NimBLECharacteristic* batteryLevelChar = pBatteryService->createCharacteristic(NimBLEUUID((uint16_t)0x2A19), NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  uint8_t batteryLevel                   = 100;
  batteryLevelChar->setValue(&batteryLevel, 1);
  pBatteryService->start();

  // Zwift Custom Service (use 0xFC82 to match advertisement)
  pZwiftService = pServer->createService(ZWIFT_RIDE_CUSTOM_SERVICE_UUID);

  // Async characteristic: NOTIFY - sends button presses to Zwift
  asyncCharacteristic = pZwiftService->createCharacteristic(ZWIFT_ASYNC_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::NOTIFY);

  // Sync RX: WRITE_WITHOUT_RESPONSE - receives handshake from Zwift
  syncRxCharacteristic = pZwiftService->createCharacteristic(ZWIFT_SYNC_RX_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::WRITE_NR);
  syncRxCharacteristic->setCallbacks(&zwiftSyncRxCallbacks);

  // Sync TX: READ | INDICATE - sends handshake response and keepalive
  syncTxCharacteristic = pZwiftService->createCharacteristic(ZWIFT_SYNC_TX_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::INDICATE);

  // Unknown characteristic 5: NOTIFY
  unknownCharacteristic5 = pZwiftService->createCharacteristic(ZWIFT_UNKNOWN_CHARACTERISTIC5_UUID, NIMBLE_PROPERTY::NOTIFY);

  // Unknown characteristic 6: READ | WRITE | WRITE_NR | INDICATE
  unknownCharacteristic6 = pZwiftService->createCharacteristic(ZWIFT_UNKNOWN_CHARACTERISTIC6_UUID,
                                                               NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::INDICATE);

  pZwiftService->start();

  // Register with DirCon for service discovery and write handling
  DirConManager::registerService(pZwiftService->getUUID(), [](NimBLECharacteristic* characteristic, const uint8_t* data, size_t length, DirConWriteResult* result) -> bool {
    if (characteristic->getUUID().equals(NimBLEUUID(ZWIFT_SYNC_RX_CHARACTERISTIC_UUID))) {
      result->autoSubscribeUuids[0] = NimBLEUUID(ZWIFT_SYNC_TX_CHARACTERISTIC_UUID);
      result->autoSubscribeUuids[1] = NimBLEUUID(ZWIFT_ASYNC_CHARACTERISTIC_UUID);
      result->autoSubscribeCount    = 2;
      std::string value(reinterpret_cast<const char*>(data), length);
      zwiftService.handleSyncRxWrite(value, true);
      return true;
    }
    return false;
  });

  SS2K_LOG(getLogTag(), "Zwift Custom Service started");
}

void BLE_Zwift_Service::update() {
  unsigned long now = millis();

  if (!keepAlive(now)) {
    return;
  }

  uint8_t zData[48];
  size_t pos                   = 0;
  const uint16_t powerWatts    = static_cast<uint16_t>(rtConfig->watts.getValue());
  const uint16_t cadence       = static_cast<uint16_t>(rtConfig->cad.getValue());
  const uint16_t heartRate     = static_cast<uint16_t>(rtConfig->hr.getValue());
  const uint32_t field5Counter = getNextZwiftField5Counter(powerWatts);

  zData[pos++] = toUnderlying(CommandCode::HubRidingData);

  // Field 1: Power (tag 0x08)
  zData[pos++] = makeTag(ZwiftProtocol::HubRidingData::Field::Power, WireType::Varint);
  pos += encodeUleb128(powerWatts, &zData[pos]);

  // Field 2: Cadence (tag 0x10)
  zData[pos++] = makeTag(ZwiftProtocol::HubRidingData::Field::Cadence, WireType::Varint);
  pos += encodeUleb128(cadence, &zData[pos]);

  // Field 3: SpeedX100 (tag 0x18)
  int speedX100 = rtConfig->getSimulatedSpeed();
  zData[pos++] = makeTag(ZwiftProtocol::HubRidingData::Field::SpeedX100, WireType::Varint);
  pos += encodeUleb128(static_cast<uint64_t>(speedX100), &zData[pos]);

  // Field 4: HR (tag 0x20)
  zData[pos++] = makeTag(ZwiftProtocol::HubRidingData::Field::HeartRate, WireType::Varint);
  pos += encodeUleb128(heartRate, &zData[pos]);

  // Field 5: Rolling counter derived from the observed trainer payloads.
  zData[pos++] = makeTag(ZwiftProtocol::HubRidingData::Field::Unknown1, WireType::Varint);
  pos += encodeUleb128(field5Counter, &zData[pos]);

  // Field 6: Unknown2 (tag 0x30) - constant 25714 from SHIFTR reference
  zData[pos++] = makeTag(ZwiftProtocol::HubRidingData::Field::Unknown2, WireType::Varint);
  pos += encodeUleb128(30091, &zData[pos]);

  //every 5 seconds, print the zData for debugging.
  static unsigned long lastDebugTime = 0;
  if (now - lastDebugTime >= 5000) {
    lastDebugTime = now;
    SS2K_LOG(getLogTag(), "Zwift riding data: %s", toHexString(zData, pos).c_str());
  }

  asyncCharacteristic->setValue(zData, pos);
  asyncCharacteristic->notify();
  DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_RIDE_CUSTOM_SERVICE_UUID), asyncCharacteristic->getUUID(), zData, pos);
}

bool BLE_Zwift_Service::isConnected() {
  unsigned long now = millis();
  return now - _lastActivityTime < kZwiftSessionTimeoutMs;
}

uint32_t BLE_Zwift_Service::getGearRatioX10000() { return _gearRatioX10000; }

void BLE_Zwift_Service::sendShiftUp() {
  SS2K_LOG(getLogTag(), "Sending shift up to Zwift");
  sendButtonNotification(RideButtonMask::ShiftUpRight);
  // Small delay then release - Zwift needs to see the transition
  vTaskDelay(50 / portTICK_PERIOD_MS);
  sendAllButtonsReleased();
}

void BLE_Zwift_Service::sendShiftDown() {
  SS2K_LOG(getLogTag(), "Sending shift down to Zwift");
  sendButtonNotification(RideButtonMask::ShiftUpLeft);
  // Small delay then release
  vTaskDelay(50 / portTICK_PERIOD_MS);
  sendAllButtonsReleased();
}

void BLE_Zwift_Service::sendButtonNotification(RideButtonMask buttonMask) {
  // Build Ride format: opcode(1) + tag(1) + varint(max5) + analog(26) = max 33 bytes
  uint32_t buttonMap = toUnderlying(~buttonMask);

  static const uint8_t analogData[] = {
      kRideKeypadAnalogButtonsTag, kRideKeypadAnalogButtonsTag + 0x06,
      kRideAnalogGroupStatusTag, 0x04, kRideAnalogLocationTag, toUnderlying(ZwiftProtocol::RideAnalogLocation::Left), kRideAnalogValueTag, 0x00,
      kRideAnalogGroupStatusTag, 0x04, kRideAnalogLocationTag, toUnderlying(ZwiftProtocol::RideAnalogLocation::Right), kRideAnalogValueTag, 0x00,
      kRideAnalogGroupStatusTag, 0x04, kRideAnalogLocationTag, toUnderlying(ZwiftProtocol::RideAnalogLocation::Up), kRideAnalogValueTag, 0x00,
      kRideAnalogGroupStatusTag, 0x04, kRideAnalogLocationTag, toUnderlying(ZwiftProtocol::RideAnalogLocation::Down), kRideAnalogValueTag, 0x00
  };

  uint8_t buf[33];
  buf[0]           = toUnderlying(CommandCode::RideKeyPadStatus);
  buf[1]           = kRideKeypadButtonMapTag;
  size_t varintLen = encodeVarint32(buttonMap, &buf[2]);
  size_t pos       = 2 + varintLen;
  memcpy(&buf[pos], analogData, sizeof(analogData));
  pos += sizeof(analogData);

  asyncCharacteristic->setValue(buf, pos);
  asyncCharacteristic->notify();
  DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_RIDE_CUSTOM_SERVICE_UUID), asyncCharacteristic->getUUID(), buf, pos);
}

size_t BLE_Zwift_Service::encodeVarint32(uint32_t value, uint8_t* buffer) {
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
  DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_RIDE_CUSTOM_SERVICE_UUID), asyncCharacteristic->getUUID(), const_cast<uint8_t*>(ALL_RELEASED), ALL_RELEASED_LEN);
}

void BLE_Zwift_Service::handleSyncRxWrite(const std::string& value, bool _isDirCon) {
  isDirCon = _isDirCon;
  if (value.length() < 2) {
    SS2K_LOG(getLogTag(), "Received short write on sync_rx, ignoring");
    return;
  }

  _lastActivityTime = millis();

  const uint8_t* data = reinterpret_cast<const uint8_t*>(value.data());

  // Check if it starts with "RideOn" (handshake)
  if (value.length() >= 6 && memcmp(data, RideOn, 6) == 0) {
    SS2K_LOG(getLogTag(), "Received RideOn handshake from Zwift (%s)", _isDirCon ? "DirCon" : "BLE");

    // Send sync_tx response: "RideOn" + trainer response start bytes {0x02, 0x00}
    uint8_t syncResponse[6 + RESPONSE_START_LEN];
    memcpy(syncResponse, RideOn, 6);
    memcpy(syncResponse + 6, RESPONSE_START, RESPONSE_START_LEN);
    syncTxCharacteristic->setValue(syncResponse, sizeof(syncResponse));
    syncTxCharacteristic->indicate();

    // Send async RideOn answer (protobuf "RIDE_ON(0)") to signal trainer capability
    asyncCharacteristic->setValue(ASYNC_RIDEON_ANSWER, ASYNC_RIDEON_ANSWER_LEN);
    asyncCharacteristic->notify();

    if (_isDirCon) {
      DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_RIDE_CUSTOM_SERVICE_UUID), syncTxCharacteristic->getUUID(), syncResponse, sizeof(syncResponse), false);
      DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_RIDE_CUSTOM_SERVICE_UUID), asyncCharacteristic->getUUID(), const_cast<uint8_t*>(ASYNC_RIDEON_ANSWER),
                                          ASYNC_RIDEON_ANSWER_LEN, false);
    }

    resetSession();
    _lastActivityTime   = millis();
    _lastKeepaliveTime  = _lastActivityTime;
    _lastRidingDataTime = _lastActivityTime;

    SS2K_LOG(getLogTag(), "Handshake complete - %s mode active", _isDirCon ? "DirCon" : "BLE");
    return;
  }
  // Handshake is already complete, handle commands
  handleZwiftCommand(data, value.length());
}

void BLE_Zwift_Service::handleZwiftCommand(const uint8_t* data, size_t length) {
  if (length < 1) return;

  uint8_t opcode = data[0];

  switch (opcode) {
    case toUnderlying(CommandCode::HubRequest): {
      // Parse DataId from HubRequest: field 1 (tag 0x08) = DataId varint
      uint64_t param = 0;
      if (length >= 3 && data[1] == makeTag(ZwiftProtocol::HubRequest::Field::DataId, WireType::Varint)) {
        // Standard protobuf: 0x00 0x08 <DataId varint>
        decodeUleb128(&data[2], length - 2, &param);
      } else if (length >= 2) {
        // Fallback: raw ULEB128 without protobuf tag
        decodeUleb128(&data[1], length - 1, &param);
      }
      SS2K_LOG(getLogTag(), "Info request (0x%02X) param=%llu", opcode, param);

      if (param == toUnderlying(ZwiftProtocol::HubRequest::DataId::GearRatio)) {
        // Gear ratio query - build DeviceInformation response directly into single buffer
        uint32_t ratio = (_gearRatioX10000 > 0) ? _gearRatioX10000 : 15300;
        uint8_t ratioEnc[5];
        size_t ratioLen      = encodeUleb128(static_cast<uint64_t>(ratio), ratioEnc);
        size_t contentLen    = 1 + ratioLen;
        size_t subContentLen = 2 + contentLen;

        uint8_t resp[20];
        size_t pos  = 0;
        resp[pos++] = toUnderlying(CommandCode::DeviceInformation);
        resp[pos++] = makeTag(ZwiftProtocol::DeviceInformation::Field::InformationId, WireType::Varint);
        pos += encodeUleb128(toUnderlying(ZwiftProtocol::HubRequest::DataId::GearRatio), &resp[pos]);
        resp[pos++] = makeTag(ZwiftProtocol::DeviceInformation::Field::SubContent, WireType::LengthDelimited);
        pos += encodeUleb128(static_cast<uint64_t>(subContentLen), &resp[pos]);
        resp[pos++] = makeTag(ZwiftProtocol::SubContent::Field::Content, WireType::LengthDelimited);
        pos += encodeUleb128(static_cast<uint64_t>(contentLen), &resp[pos]);
        resp[pos++] = makeTag(ZwiftProtocol::DeviceInformationContent::Field::ReplyData, WireType::Varint);
        memcpy(&resp[pos], ratioEnc, ratioLen);
        pos += ratioLen;

        syncTxCharacteristic->setValue(resp, pos);
        syncTxCharacteristic->indicate();
        DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_RIDE_CUSTOM_SERVICE_UUID), syncTxCharacteristic->getUUID(), resp, pos);

      } else if (param == toUnderlying(ZwiftProtocol::HubRequest::DataId::GeneralInfo)) {
        // Device info query - build DeviceInformation response directly into single buffer
        const char* deviceName = userConfig->getDeviceName();
        size_t nameLen         = strlen(deviceName);
        if (nameLen > 20) nameLen = 20;
        size_t contentLen    = 6 + nameLen;
        size_t subContentLen = 2 + contentLen;

        uint8_t resp[40];
        size_t pos  = 0;
        resp[pos++] = toUnderlying(CommandCode::DeviceInformation);
        resp[pos++] = makeTag(ZwiftProtocol::DeviceInformation::Field::InformationId, WireType::Varint);
        pos += encodeUleb128(toUnderlying(ZwiftProtocol::HubRequest::DataId::GeneralInfo), &resp[pos]);
        resp[pos++] = makeTag(ZwiftProtocol::DeviceInformation::Field::SubContent, WireType::LengthDelimited);
        pos += encodeUleb128(static_cast<uint64_t>(subContentLen), &resp[pos]);
        resp[pos++] = makeTag(ZwiftProtocol::SubContent::Field::Content, WireType::LengthDelimited);
        pos += encodeUleb128(static_cast<uint64_t>(contentLen), &resp[pos]);
        resp[pos++] = makeTag(ZwiftProtocol::DeviceInformationContent::Field::Unknown1, WireType::Varint);
        pos += encodeUleb128(1ULL, &resp[pos]);
        resp[pos++] = makeTag(ZwiftProtocol::DeviceInformationContent::Field::SoftwareVersion, WireType::Varint);
        pos += encodeUleb128(100ULL, &resp[pos]);
        resp[pos++] = makeTag(ZwiftProtocol::DeviceInformationContent::Field::DeviceName, WireType::LengthDelimited);
        pos += encodeUleb128(static_cast<uint64_t>(nameLen), &resp[pos]);
        memcpy(&resp[pos], deviceName, nameLen);
        pos += nameLen;

        syncTxCharacteristic->setValue(resp, pos);
        syncTxCharacteristic->indicate();
        DirConManager::notifyCharacteristic(NimBLEUUID(ZWIFT_RIDE_CUSTOM_SERVICE_UUID), syncTxCharacteristic->getUUID(), resp, pos);
        SS2K_LOG(getLogTag(), "Responded with device info");
      }
      break;
    }

    case toUnderlying(CommandCode::HubCommand): {
      uint8_t subtype = data[1];

      switch (subtype) {
        case makeTag(ZwiftProtocol::HubCommand::Field::PowerTarget, WireType::Varint): {
          if (length >= 3) {
            uint64_t power = 0;
            decodeUleb128(&data[2], length - 2, &power);
            SS2K_LOG(getLogTag(), "ERG power: %dW", static_cast<int>(power));
            rtConfig->setFTMSMode(FitnessMachineControlPointProcedure::SetTargetPower);
            rtConfig->watts.setTarget(static_cast<int>(power));
          }
          break;
        }

        case makeTag(ZwiftProtocol::HubCommand::Field::Simulation, WireType::LengthDelimited): {
          if (length >= 4) {
            uint8_t subLen = data[2];
            size_t pos     = 3;
            while (pos < static_cast<size_t>(3 + subLen) && pos < length) {
              uint8_t fieldTag    = data[pos++];
              uint64_t fieldValue = 0;
              size_t decoded      = decodeUleb128(&data[pos], length - pos, &fieldValue);
              if (decoded == 0) break;
              pos += decoded;

              if (fieldTag == makeTag(ZwiftProtocol::SimulationParam::Field::InclineX100, WireType::Varint)) {
                int64_t grade = static_cast<int64_t>((fieldValue >> 1) ^ -(fieldValue & 1));
                SS2K_LOG(getLogTag(), "SIM grade: %.2f%%", grade / 100.0);
                rtConfig->setFTMSMode(FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters);
                rtConfig->setTargetIncline(static_cast<int>(grade));
              }
            }
          }
          break;
        }

        case makeTag(ZwiftProtocol::HubCommand::Field::Physical, WireType::LengthDelimited): {
          if (length >= 4) {
            uint8_t subLen = data[2];
            size_t pos     = 3;
            while (pos < static_cast<size_t>(3 + subLen) && pos < length) {
              uint8_t fieldTag    = data[pos++];
              uint64_t fieldValue = 0;
              size_t decoded      = decodeUleb128(&data[pos], length - pos, &fieldValue);
              if (decoded == 0) break;
              pos += decoded;

              if (fieldTag == makeTag(ZwiftProtocol::PhysicalParam::Field::GearRatioX10000, WireType::Varint)) {
                _gearRatioX10000 = static_cast<uint32_t>(fieldValue);
                SS2K_LOG(getLogTag(), "Gear ratio: %.4f", _gearRatioX10000 / 10000.0);
                applyGearRatio();
              }
              // Fields 0x20 (bike weight) and 0x28 (rider weight) - logged for debugging
              else if (fieldTag == makeTag(ZwiftProtocol::PhysicalParam::Field::BikeWeightX100, WireType::Varint)) {
                SS2K_LOG(getLogTag(), "Bike weight: %.2fkg", fieldValue / 100.0);
              } else if (fieldTag == makeTag(ZwiftProtocol::PhysicalParam::Field::RiderWeightX100, WireType::Varint)) {
                SS2K_LOG(getLogTag(), "Rider weight: %.2fkg", fieldValue / 100.0);
              }
            }
          }
          break;
        }

        default:
          SS2K_LOG(getLogTag(), "Unknown control subtype: 0x%02X", subtype);
          break;
      }
      break;
    }

    case 0x41:  // Unknown request type (similar to info request)
      SS2K_LOG(getLogTag(), "Request 0x41");
      break;

    default:
      SS2K_LOG(getLogTag(), "Unknown command opcode: 0x%02X", opcode);
      break;
  }
}

void BLE_Zwift_Service::applyGearRatio() {
  if (_gearRatioX10000 == 0) return;

  // Zwift virtual gear ratios (24 gears, from 0.75 to 5.49)
  static const uint32_t gearRatios[] = {7500,  8700,  9900,  11100, 12300, 13800, 15300, 16800, 18600, 20400, 22200, 24000,
                                        26100, 28200, 30300, 32400, 34900, 37400, 39900, 42400, 45400, 48400, 51400, 54900};
  // static const int kDefaultGearIndex = 11;  // ratio 2.40 (≈ 34T/14T)
  static const int kNumGears = sizeof(gearRatios) / sizeof(gearRatios[0]);

  // Find closest gear index
  int closestIndex     = 0;
  uint32_t closestDist = 0xFFFFFFFFU;
  for (int i = 0; i < kNumGears; i++) {
    uint32_t dist = static_cast<uint32_t>(abs(static_cast<int>(_gearRatioX10000) - static_cast<int>(gearRatios[i])));
    if (dist < closestDist) {
      closestDist  = dist;
      closestIndex = i + 1;
    }
  }

  int newShifterPos = closestIndex;
  rtConfig->setShifterPosition(newShifterPos);
  // Also update lastShifterPosition so FTMSModeShiftModifier doesn't
  // see this Zwift-driven change as a user shift and echo it back.
  ss2k->setLastShifterPosition(newShifterPos);
  SS2K_LOG(getLogTag(), "Gear %d -> shifter position %d", closestIndex + 1, newShifterPos);
}

size_t BLE_Zwift_Service::decodeUleb128(const uint8_t* buf, size_t bufLen, uint64_t* result) {
  *result        = 0;
  size_t i       = 0;
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

size_t BLE_Zwift_Service::encodeUleb128(uint64_t value, uint8_t* buffer) {
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

void ZwiftSyncRxCallbacks::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
  NimBLEAttValue value = pCharacteristic->getValue();
  // log the entire data recieved in 0x
  std::string hexValue;
  for (size_t i = 0; i < value.length(); i++) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", value[i]);
    hexValue.append(buf);
  }
  SS2K_LOG(zwiftService.getLogTag(), "Sync RX write from %s (len=%d) %s", connInfo.getAddress().toString().c_str(), value.length(), hexValue.c_str());
  zwiftService.handleSyncRxWrite(value);
}

void ZwiftSyncRxCallbacks::onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) {
  SS2K_LOG(zwiftService.getLogTag(), "Subscription change on %s: %d", pCharacteristic->getUUID().toString().c_str(), subValue);
}
