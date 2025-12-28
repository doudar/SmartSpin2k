/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BLE_KickrBikeService.h"
#include "DirConManager.h"
#include "Main.h"
#include "BLE_Common.h"
#include <Constants.h>
#include <algorithm>
#include <array>
#include <vector>

namespace {
inline void appendVarint(std::vector<uint8_t>& buffer, uint32_t value) {
  while (value >= 0x80) {
    buffer.push_back(static_cast<uint8_t>(value | 0x80));
    value >>= 7;
  }
  buffer.push_back(static_cast<uint8_t>(value));
}

inline void appendVarintField(std::vector<uint8_t>& buffer, uint8_t fieldNumber, uint32_t value) {
  uint8_t key = static_cast<uint8_t>((fieldNumber << 3) | 0x00);
  buffer.push_back(key);
  appendVarint(buffer, value);
}

inline int32_t decodeZigZag32(uint32_t value) {
  return static_cast<int32_t>((value >> 1) ^ static_cast<uint32_t>(-static_cast<int32_t>(value & 0x01)));
}

inline bool decodeVarint32(const uint8_t* data, size_t endIndex, size_t& index, uint32_t& result) {
  result = 0;
  uint32_t shift = 0;
  while (index < endIndex) {
    uint8_t byte = data[index++];
    result |= static_cast<uint32_t>(byte & 0x7F) << shift;
    if ((byte & 0x80) == 0) {
      return true;
    }
    shift += 7;
    if (shift >= 32) {
      return false;  // Overflow
    }
  }
  return false;  // Incomplete varint
}

// Zwift Play opcodes/tokens derived from qdomyos-zwift reverse engineering.
constexpr uint8_t ZWIFT_OPCODE_GEAR_EVENT = 0x03;
constexpr uint8_t ZWIFT_OPCODE_GEAR_RESPONSE = 0x3C;
constexpr size_t ZWIFT_CHAINRING_COUNT = 2;
constexpr size_t ZWIFT_GEARS_PER_RING =
    KICKR_BIKE_NUM_GEARS >= ZWIFT_CHAINRING_COUNT ? (KICKR_BIKE_NUM_GEARS / ZWIFT_CHAINRING_COUNT) : KICKR_BIKE_NUM_GEARS;

struct GearProtoFields {
  uint16_t token = 0;
  uint8_t frontIndex = 0;
  uint8_t rearIndex = 0;
  uint8_t gearIndex = 0;
};

// Actual Zwift gear tokens captured from real Zwift communication.
// Gears 1-24 from easiest to hardest.
constexpr std::array<uint16_t, KICKR_BIKE_NUM_GEARS> zwiftGearTokens = {
    7500, 8700, 9900, 11100, 12300, 13800, 15300, 16800,
    18600, 20400, 22200, 24000, 26099, 28200, 30300, 32400,
    34900, 37400, 39900, 42399, 45400, 48400, 51400, 54899};

uint16_t gearTokenFromIndex(int gearIndex) {
  if (gearIndex < 0 || gearIndex >= static_cast<int>(zwiftGearTokens.size())) {
    return 0;
  }
  return zwiftGearTokens[gearIndex];
}

inline int gearNumberFromInboundToken(uint32_t token) {
  for (size_t i = 0; i < zwiftGearTokens.size(); ++i) {
    if (zwiftGearTokens[i] == static_cast<uint16_t>(token)) {
      return static_cast<int>(i) + 1;  // Return 1-based gear number
    }
  }
  return -1;  // Unknown token
}

GearProtoFields buildGearProtoFields(int gearIndex) {
  GearProtoFields fields;
  if (gearIndex < 0) {
    return fields;
  }
  fields.token = gearTokenFromIndex(gearIndex);
  if (fields.token == 0) {
    return fields;
  }
  fields.gearIndex = static_cast<uint8_t>(gearIndex + 1);
  const size_t perRing = ZWIFT_GEARS_PER_RING == 0 ? 1 : ZWIFT_GEARS_PER_RING;
  const size_t frontIdx = static_cast<size_t>(gearIndex) / perRing;
  const size_t rearIdx = static_cast<size_t>(gearIndex) % perRing;
  fields.frontIndex = static_cast<uint8_t>(frontIdx + 1);
  fields.rearIndex = static_cast<uint8_t>(rearIdx + 1);
  return fields;
}

uint16_t lastReportedGearToken = 0;

void emitGearFrame(NimBLECharacteristic* characteristic, const GearProtoFields& fields, uint8_t opcode) {
  if (!characteristic || fields.token == 0) {
    return;
  }

  std::vector<uint8_t> payload;
  payload.reserve(16);
  payload.push_back(opcode);
  appendVarintField(payload, 1, fields.token);
  appendVarintField(payload, 2, fields.frontIndex);
  appendVarintField(payload, 3, fields.rearIndex);
  appendVarintField(payload, 4, fields.gearIndex);
  
  SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: Sending gear event opcode 0x%02X, token %u, size %d", opcode, fields.token, payload.size());
  
  spinBLEServer.notifyBleAndDircon(characteristic, payload.data(), payload.size());
}
}  // namespace

// Gear ratio table: 24 gears from easiest (0.50) to hardest (1.65)
// These ratios are multiplied with the base gradient to simulate gear changes
const double BLE_KickrBikeService::gearRatios[KICKR_BIKE_NUM_GEARS] = {
    0.50, 0.55, 0.60, 0.65, 0.70, 0.75, 0.80, 0.85,  // Gears 1-8 (easy)
    0.90, 0.95, 1.00, 1.05, 1.10, 1.15, 1.20, 1.25,  // Gears 9-16 (medium)
    1.30, 1.35, 1.40, 1.45, 1.50, 1.55, 1.60, 1.65   // Gears 17-24 (hard)
};

BLE_KickrBikeService::BLE_KickrBikeService()
    : pKickrBikeService(nullptr),
      syncRxCharacteristic(nullptr),
      asyncTxCharacteristic(nullptr),
      syncTxCharacteristic(nullptr),
      debugCharacteristic(nullptr),
      unknown6Characteristic(nullptr),
      currentGear(KICKR_BIKE_DEFAULT_GEAR),
      lastShifterPosition(-1),
      baseGradient(0.0),
      effectiveGradient(0.0),
      targetPower(0),
      isHandshakeComplete(false),
      isEnabled(false),
      lastKeepAliveTime(0),
      lastGradientUpdateTime(0),
      lastRideDataTime(0) {}

void BLE_KickrBikeService::setupService(NimBLEServer *pServer, MyCharacteristicCallbacks *chrCallbacks) {
  // Create the Zwift Ride service (KICKR BIKE protocol)
  pKickrBikeService = spinBLEServer.pServer->createService(ZWIFT_CUSTOM_SERVICE_UUID);
  
  // Create the three characteristics according to KICKR BIKE specification:
  // 1. Sync RX - Write characteristic for receiving commands from Zwift
  syncRxCharacteristic = pKickrBikeService->createCharacteristic(
      ZWIFT_SYNC_RX_UUID, 
      NIMBLE_PROPERTY::WRITE_NR);
  
  // 2. Async TX - Notify characteristic for asynchronous events (button presses, battery)
  asyncTxCharacteristic = pKickrBikeService->createCharacteristic(
      ZWIFT_ASYNC_TX_UUID, 
      NIMBLE_PROPERTY::NOTIFY);
  
  // 3. Sync TX - Notify characteristic for synchronous responses
  syncTxCharacteristic = pKickrBikeService->createCharacteristic(
      ZWIFT_SYNC_TX_UUID, 
      NIMBLE_PROPERTY::INDICATE);

  // Optional: Debug characteristic for logging/debug info
  debugCharacteristic = pKickrBikeService->createCharacteristic(
      ZWIFT_DEBUG_CHARACTERISTIC_UUID, 
      NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);

  // Optional: Unknown characteristic 6
  unknown6Characteristic = pKickrBikeService->createCharacteristic(
      ZWIFT_UNKNOWN_6_CHARACTERISTIC_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
  
  // Set custom callback for Sync RX to handle RideOn handshake
  static KickrBikeCharacteristicCallbacks kickrBikeCallbacks;
  syncRxCharacteristic->setCallbacks(&kickrBikeCallbacks);
  
  // Start the service
  pKickrBikeService->start();
  
  // Add service UUID to DirCon MDNS (for discovery)
  // DirConManager::addBleServiceUuid(pKickrBikeService->getUUID());
  
  SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE Service initialized with %d gears", KICKR_BIKE_NUM_GEARS);
}

void BLE_KickrBikeService::update() {
  updateGearFromShifterPosition();

  // Send periodic keep-alive messages if handshake is complete
  if (isHandshakeComplete) {
    unsigned long currentTime = millis();
    // Send keep-alive every 5 seconds
    if (currentTime - lastKeepAliveTime >= 5000) {
      sendKeepAlive();
      lastKeepAliveTime = currentTime;
    }

    if (currentTime - lastRideDataTime >= 1000) {
      sendRideData();
      lastRideDataTime = currentTime;
    }
  }
}

void BLE_KickrBikeService::shiftUp() {
  if (currentGear < KICKR_BIKE_NUM_GEARS - 1) {
    currentGear++;
    applyGearChange();
    SS2K_LOG(BLE_SERVER_LOG_TAG, "Shifted UP to gear %d (ratio: %.2f)", 
             currentGear + 1, getCurrentGearRatio());
  } else {
    SS2K_LOG(BLE_SERVER_LOG_TAG, "Already in highest gear");
  }
}

void BLE_KickrBikeService::shiftDown() {
  if (currentGear > 0) {
    currentGear--;
    applyGearChange();
    SS2K_LOG(BLE_SERVER_LOG_TAG, "Shifted DOWN to gear %d (ratio: %.2f)", 
             currentGear + 1, getCurrentGearRatio());
  } else {
    SS2K_LOG(BLE_SERVER_LOG_TAG, "Already in lowest gear");
  }
}

double BLE_KickrBikeService::getCurrentGearRatio() const {
  if (currentGear >= 0 && currentGear < KICKR_BIKE_NUM_GEARS) {
    return gearRatios[currentGear];
  }
  return 1.0;  // Default to neutral ratio
}

void BLE_KickrBikeService::applyGearChange() {
  applyGearChange(false);
}

void BLE_KickrBikeService::applyGearChange(bool fromZwift) {
  // Recalculate effective gradient with new gear
  effectiveGradient = calculateEffectiveGrade(baseGradient, getCurrentGearRatio());
  
  // Apply to trainer if this service is enabled
  if (isEnabled) {
    applyGradientToTrainer();
  }

  const GearProtoFields gearFields = buildGearProtoFields(currentGear);
  if (gearFields.token != 0) {
    // Only send gear event notification if we initiated the change (not Zwift)
    if (!fromZwift) {
      emitGearFrame(asyncTxCharacteristic, gearFields, ZWIFT_OPCODE_GEAR_EVENT);
    }
    lastReportedGearToken = gearFields.token;
    const double ratio = getCurrentGearRatio();
    SS2K_LOG(BLE_SERVER_LOG_TAG,
             "KICKR BIKE: Zwift Play gear token 0x%03X -> gear %u (front %u, rear %u, ratio %.2f)",
             gearFields.token,
             gearFields.gearIndex,
             gearFields.frontIndex,
             gearFields.rearIndex,
             ratio);
  }
}

void BLE_KickrBikeService::setBaseGradient(double gradientPercent) {
  baseGradient = gradientPercent;
  effectiveGradient = calculateEffectiveGrade(baseGradient, getCurrentGearRatio());
  
  // Apply to trainer if enabled
  if (isEnabled) {
    applyGradientToTrainer();
  }
  
  SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: Base gradient set to %.2f%%", baseGradient);
}

double BLE_KickrBikeService::getEffectiveGradient() const {
  return effectiveGradient;
}

void BLE_KickrBikeService::applyGradientToTrainer() {
  // Only update if enough time has passed (100ms debounce)
  unsigned long currentTime = millis();
  if (currentTime - lastGradientUpdateTime < 100) {
    return;
  }
  lastGradientUpdateTime = currentTime;
  
  // Clamp to valid trainer limits (-20% to +20%)
  double clampedGradient = effectiveGradient;
  if (clampedGradient < -20.0) clampedGradient = -20.0;
  if (clampedGradient > 20.0) clampedGradient = 20.0;
  
  // Convert to 0.01% units for rtConfig
  int gradientUnits = static_cast<int>(clampedGradient * 100);
  
  // Update the target incline directly
  rtConfig->setTargetIncline(gradientUnits);
  
  SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: Applied gradient %.2f%% (gear %d, ratio %.2f)", 
           clampedGradient, currentGear + 1, getCurrentGearRatio());
}

void BLE_KickrBikeService::setTargetPower(int watts) {
  targetPower = watts;
  
  // In ERG mode, the power is fixed and gears affect the "feel"
  // This is handled by the trainer's power control logic
  SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: Target power set to %d watts", targetPower);
}

void BLE_KickrBikeService::updateTrainerPosition() {
  // This method updates the physical trainer position based on effective gradient
  // Only applies if the service is enabled and controlling the trainer
  if (!isEnabled) {
    return;
  }
  
  applyGradientToTrainer();
}

double BLE_KickrBikeService::calculateEffectiveGrade(double baseGrade, double gearRatio) {
  // Calculate effective grade by multiplying base grade with gear ratio
  // This simulates the feeling of shifting gears:
  // - Lower gear (ratio < 1.0) makes hills feel easier
  // - Higher gear (ratio > 1.0) makes hills feel harder
  return baseGrade * gearRatio;
}

void BLE_KickrBikeService::updateGearFromShifterPosition() {
  // Get current shifter position
  int currentShifterPosition = rtConfig->getShifterPosition();
  
  // Check if shifter position has changed
  if (lastShifterPosition == -1) {
    // First run, just store the position
    lastShifterPosition = currentShifterPosition;
    return;
  }
  
  if (currentShifterPosition == lastShifterPosition) {
    // No change, nothing to do
    return;
  }
  
  // Determine direction of shift
  if (currentShifterPosition > lastShifterPosition) {
    // Shifter moved up - shift to harder gear
    shiftUp();
  } else {
    // Shifter moved down - shift to easier gear
    shiftDown();
  }
  
  // Update last position
  lastShifterPosition = currentShifterPosition;
}

bool BLE_KickrBikeService::isRideOnMessage(const std::string& data) {
  // RideOn handshake prefix = 0x52 0x69 0x64 0x65 0x4F 0x6E
  if (data.length() < 6) {
    return false;
  }

  static const uint8_t rideOnPrefix[6] = {0x52, 0x69, 0x64, 0x65, 0x4F, 0x6E};
  for (size_t i = 0; i < 6; ++i) {
    if ((uint8_t)data[i] != rideOnPrefix[i]) {
      return false;
    }
  }

  if (data.length() == 6) {
    return true;
  }

  // Some Zwift clients append signature bytes (0x01 or 0x02, followed by 0x03)
  if (data.length() == 8) {
    uint8_t signatureMsb = (uint8_t)data[6];
    uint8_t signatureLsb = (uint8_t)data[7];
    if (signatureLsb == 0x03 && (signatureMsb == 0x01 || signatureMsb == 0x02)) {
      return true;
    }
  }

  return false;
}

void KickrBikeCharacteristicCallbacks::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
  std::string rxValue = pCharacteristic->getValue();
  kickrBikeService.processWrite(rxValue);
}

void BLE_KickrBikeService::processWrite(const std::string& value) {
  if (value.empty()) {
    SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: Received empty write");
    return;
  }
  
  // Check if this is the RideOn handshake (no opcode, just raw bytes)
  if (isRideOnMessage(value)) {
    SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: Received RideOn handshake");
    sendRideOnResponse();
    isHandshakeComplete = true;
    lastKeepAliveTime = millis();
    return;
  }
  
  // Process opcode-based messages
  uint8_t opcode = (uint8_t)value[0];
  const uint8_t* messageData = (const uint8_t*)value.data() + 1;
  size_t messageLength = value.length() - 1;
  
  switch (opcode) {
    case 0x00:  // INFO_REQUEST - device information query
      handleInfoRequest(messageData, messageLength);
      break;
      
    case 0x04:  // SET - Update trainer state
      handleSetRequest(messageData, messageLength);
      break;
      
    case 0x08:  // GET - Request data object
      handleGetRequest(messageData, messageLength);
      break;
      
    case 0x22:  // RESET - Reset device
      handleReset();
      break;
      
    case 0x41:  // LOG_LEVEL_SET - Set log level
      handleSetLogLevel(messageData, messageLength);
      break;
      
    case 0x32:  // VENDOR_MESSAGE - Vendor-specific message
      handleVendorMessage(messageData, messageLength);
      break;
      
    case 0x07:  // CONTROLLER_NOTIFICATION - Button events (shouldn't be written to us, we send these)
      SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: Unexpected CONTROLLER_NOTIFICATION write");
      break;
      
    case 0x19:  // BATTERY_NOTIF - Battery updates (shouldn't be written to us, we send these)
      SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: Unexpected BATTERY_NOTIF write");
      break;
      
    default:
      // Log full unknown message for debugging
      String hexDump;
      for (size_t i = 0; i < value.length(); ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", (uint8_t)value[i]);
        hexDump += buf;
      }
      SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: Received unknown message: %s", hexDump.c_str());
      break;
  }
}

void BLE_KickrBikeService::sendRideOnResponse() {
  // Respond with "RideOn" + signature bytes (0x01 0x03)
  uint8_t response[8] = {
    0x52, 0x69, 0x64, 0x65, 0x4F, 0x6E,  // "RideOn"
    0x01, 0x03                            // Signature
  };
  
  spinBLEServer.notifyBleAndDircon(syncTxCharacteristic, response, sizeof(response));
  
  SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: Sent RideOn response");
}

void BLE_KickrBikeService::sendKeepAlive() {
  // Keep-alive message to maintain connection with Zwift
  // This is a protobuf-encoded message that tells Zwift we're still alive
  // The exact format comes from the BikeControl reference implementation
  uint8_t keepAliveData[] = {
    0xB7, 0x01, 0x00, 0x00, 0x20, 0x41, 0x20, 0x1C, 
    0x00, 0x18, 0x00, 0x04, 0x00, 0x1B, 0x4F, 0x00, 
    0xB7, 0x01, 0x00, 0x00, 0x20, 0x79, 0x8E, 0xC5, 
    0xBD, 0xEF, 0xCB, 0xE4, 0x56, 0x34, 0x18, 0x26, 
    0x9E, 0x49, 0x26, 0xFB, 0xE1
  };
  
  spinBLEServer.notifyBleAndDircon(syncTxCharacteristic, keepAliveData, sizeof(keepAliveData));
  
  SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: Sent keep-alive");
}

void BLE_KickrBikeService::sendRideData() {
  if (!isHandshakeComplete) {
    return;
  }

  int power = std::max(0, rtConfig->watts.getValue());
  int cadence = std::max(0, static_cast<int>(rtConfig->cad.getValue()));
  double speedKmh = rtConfig->getSimulatedSpeed() > 0 ? rtConfig->getSimulatedSpeed() : spinBLEServer.calculateSpeed();
  if (speedKmh < 0) {
    speedKmh = 0;
  }
  uint32_t speedX100 = static_cast<uint32_t>(speedKmh * 100.0 + 0.5);
  int heartRate = std::max(0, rtConfig->hr.getValue());

  std::vector<uint8_t> payload;
  payload.reserve(20);
  payload.push_back(0x03);
  appendVarintField(payload, 1, static_cast<uint32_t>(power));
  appendVarintField(payload, 2, static_cast<uint32_t>(cadence));
  appendVarintField(payload, 3, speedX100);
  appendVarintField(payload, 4, static_cast<uint32_t>(heartRate));
  appendVarintField(payload, 5, 0);
  appendVarintField(payload, 6, 0);

  spinBLEServer.notifyBleAndDircon(asyncTxCharacteristic, payload.data(), payload.size());

}

// Opcode message handlers

void BLE_KickrBikeService::handleGetRequest(const uint8_t* data, size_t length) {
  // GET request - Zwift is requesting a data object
  // The data should contain an object ID (protobuf encoded)
  
  if (length < 1) {
    SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: GET request with no data");
    sendStatusResponse(0x02);  // Error status
    return;
  }
  
  // For now, we'll parse a simple object ID from the first bytes
  // In a full implementation, this would be protobuf decoded
  uint16_t objectId = 0;
  if (length >= 2) {
    objectId = ((uint16_t)data[1] << 8) | data[0];
  } else {
    objectId = data[0];
  }
  
  SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: GET request for object ID 0x%04X", objectId);
  
  // Respond with empty data for now (full implementation would return actual object data)
  sendGetResponse(objectId, nullptr, 0);
}

void BLE_KickrBikeService::handleSetRequest(const uint8_t* data, size_t length) {
  if (length < 3) {
    SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: SET request too short (%d)", length);
    sendStatusResponse(0x02);
    return;
  }

  // Zwift gear select (from some controllers) arrives as:
  // 2A <len> 10 <varint gearToken>
  if (data[0] == 0x2A && data[1] >= 2) {
    const uint8_t payloadLen = data[1];
    const size_t payloadEnd = 2 + payloadLen;
    if (payloadEnd <= length && data[2] == 0x10) {
      size_t index = 3;
      uint32_t token = 0;

      if (!decodeVarint32(data, payloadEnd, index, token)) {
        SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: SET gear token varint overflow");
        sendStatusResponse(0x02);
        return;
      }

      const int gearNumber = gearNumberFromInboundToken(token);
      if (gearNumber > 0) {
        // Sync internal + external representation immediately (avoid incremental shifting logic).
        currentGear = std::clamp(gearNumber - 1, 0, KICKR_BIKE_NUM_GEARS - 1);
        lastShifterPosition = gearNumber;
        rtConfig->setShifterPosition(gearNumber);
        applyGearChange(true);  // fromZwift = true, don't echo back

        SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: SET gear token %lu -> gear %d", static_cast<unsigned long>(token), gearNumber);
        sendStatusResponse(0x00);
        return;
      }

      SS2K_LOG(BLE_SERVER_LOG_TAG,
               "KICKR BIKE: SET unknown gear token %lu (add to zwiftInboundGearTokensObserved)",
               static_cast<unsigned long>(token));
      sendStatusResponse(0x00);
      return;
    }
  }

  // Zwift sends gradient updates as: 0x22 <len> 0x10 <varint gradient*100 (zigzag)>
  if (data[0] == 0x22 && data[1] >= 2) {
    uint8_t payloadLen = data[1];
    size_t payloadEnd = 2 + payloadLen;
    if (payloadEnd <= length && data[2] == 0x10) {
      size_t index = 3;
      uint32_t rawValue = 0;

      if (!decodeVarint32(data, payloadEnd, index, rawValue)) {
        SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: SET gradient varint overflow");
        sendStatusResponse(0x02);
        return;
      }

      int32_t signedValue = decodeZigZag32(rawValue);
      double gradientPercent = static_cast<double>(signedValue) / 100.0;
      SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: SET gradient to %.2f%% (raw %ld)", gradientPercent, static_cast<long>(signedValue));
      setBaseGradient(gradientPercent);
      sendStatusResponse(0x00);
      return;
    }
  }

  // Unknown SET payload; acknowledge to keep protocol flowing but log for future decoding.
  String hexDump;
  for (size_t i = 0; i < length; ++i) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02X ", data[i]);
    hexDump += buf;
  }
  SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: Unhandled SET payload: %s", hexDump.c_str());
  sendStatusResponse(0x00);
}

void BLE_KickrBikeService::handleInfoRequest(const uint8_t* data, size_t length) {
  if (length == 0) {
    SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: INFO request with no data");
    sendStatusResponse(0x02);
    return;
  }

  uint32_t requestId = 0;
  bool parsed = false;

  if (data[0] == 0x08 && length >= 2) {
    size_t index = 1;
    uint8_t shift = 0;
    while (index < length) {
      uint8_t byte = data[index++];
      requestId |= (static_cast<uint32_t>(byte & 0x7F) << shift);
      if ((byte & 0x80) == 0) {
        parsed = true;
        break;
      }
      shift += 7;
      if (shift > 28) {
        break;
      }
    }
  }

  if (!parsed) {
    String hexDump;
    for (size_t i = 0; i < length; ++i) {
      char buf[4];
      snprintf(buf, sizeof(buf), "%02X ", data[i]);
      hexDump += buf;
    }
    SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: INFO request unparsed payload: %s", hexDump.c_str());
    sendStatusResponse(0x02);
    return;
  }

  SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: INFO request for id %lu", static_cast<unsigned long>(requestId));

  // We don't yet build the protobuf reply for these queries, but acknowledging keeps the protocol flowing.
  sendStatusResponse(0x00);
}

void BLE_KickrBikeService::handleReset() {
  // RESET command - Reset the device to default state
  SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: RESET command received");
  
  // Reset to default gear
  currentGear = KICKR_BIKE_DEFAULT_GEAR;
  baseGradient = 0.0;
  effectiveGradient = 0.0;
  targetPower = 0;
  
  // Apply reset state to trainer
  if (isEnabled) {
    applyGradientToTrainer();
  }
  
  // Send success status
  sendStatusResponse(0x00);  // Success
}

void BLE_KickrBikeService::handleSetLogLevel(const uint8_t* data, size_t length) {
  // LOG_LEVEL_SET - Set logging level
  if (length < 1) {
    SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: SET_LOG_LEVEL with no data");
    return;
  }
  
  uint8_t logLevel = data[0];
  SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: SET_LOG_LEVEL to %d", logLevel);
  
  // For now, just acknowledge - full implementation would adjust logging
  sendStatusResponse(0x00);  // Success
}

void BLE_KickrBikeService::handleVendorMessage(const uint8_t* data, size_t length) {
  // VENDOR_MESSAGE - Vendor-specific message
  SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: VENDOR_MESSAGE received (%d bytes)", length);
  
  // Log the message content for debugging
  if (length > 0) {
    SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: Vendor message first byte: 0x%02X", data[0]);
  }
  
  // Send success status
  sendStatusResponse(0x00);
}

void BLE_KickrBikeService::sendGetResponse(uint16_t objectId, const uint8_t* data, size_t length) {
  // Send GET_RESPONSE (opcode 0x3C) with the requested object data
  std::vector<uint8_t> response;
  response.push_back(0x3C);  // GET_RESPONSE opcode
  
  // Add object ID (little-endian)
  response.push_back(objectId & 0xFF);
  response.push_back((objectId >> 8) & 0xFF);
  
  // Add data if provided
  if (data && length > 0) {
    response.insert(response.end(), data, data + length);
  }
  
  spinBLEServer.notifyBleAndDircon(syncTxCharacteristic, response.data(), response.size());
  
  SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: Sent GET_RESPONSE for object 0x%04X", objectId);
}

void BLE_KickrBikeService::sendStatusResponse(uint8_t status) {
  // Send STATUS_RESPONSE (opcode 0x12) with status code
  uint8_t response[2] = {
    0x12,   // STATUS_RESPONSE opcode
    status  // Status code (0x00 = success, others = error)
  };
  
  spinBLEServer.notifyBleAndDircon(syncTxCharacteristic, response, sizeof(response));
  
  SS2K_LOG(BLE_SERVER_LOG_TAG, "KICKR BIKE: Sent STATUS_RESPONSE (status: 0x%02X)", status);
}
