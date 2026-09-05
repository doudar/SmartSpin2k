/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "SS2KLog.h"
#include "BLE_Common.h"
#include <ArduinoJson.h>
#include <Constants.h>
#include <NimBLEDevice.h>
#include <NimBLEUtils.h>
#include <WiFi.h>
#include <host/ble_gatt.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include "BLE_Cycling_Speed_Cadence.h"
#include "BLE_Cycling_Power_Service.h"
#include "BLE_Heart_Service.h"
#include "BLE_Fitness_Machine_Service.h"
#include "BLE_Custom_Characteristic.h"
#include "BLE_Device_Information_Service.h"
#include "BLE_Zwift_Service.h"
#include "BLE_OpenBikeControl_Service.h"

// BLE Server Settings
SpinBLEServer spinBLEServer;

static MyCharacteristicCallbacks chrCallbacks;

BLE_Cycling_Speed_Cadence cyclingSpeedCadenceService;
BLE_Cycling_Power_Service cyclingPowerService;
BLE_Heart_Service heartService;
BLE_Fitness_Machine_Service fitnessMachineService;
BLE_ss2kCustomCharacteristic ss2kCustomCharacteristic;
BLE_Device_Information_Service deviceInformationService;
BLE_Zwift_Service zwiftService;
BLE_OpenBikeControl_Service openBikeControlService;
// BLE_Wattbike_Service wattbikeService;
// BLE_SB20_Service sb20Service;

namespace {
constexpr uint8_t SMARTSPIN2K_IP_ADVERTISEMENT_VERSION = 1;
// Leaves room for the 128-bit SmartSpin2k service UUID in the 31-byte scan response.
constexpr size_t BLE_ADVERTISED_NAME_MAX_SIZE = 11;

std::string bleAdvertisementName(const char* deviceName) {
  std::string name = deviceName;
  if (name.size() <= BLE_ADVERTISED_NAME_MAX_SIZE) {
    return name;
  }

  size_t length = BLE_ADVERTISED_NAME_MAX_SIZE;
  while (length > 0 && (static_cast<uint8_t>(name[length]) & 0xc0) == 0x80) {
    --length;
  }
  name.resize(length);
  return name;
}

bool addIpAddressToAdvertisement(NimBLEAdvertising* advertising) {
  IPAddress ipAddress = WiFi.status() == WL_CONNECTED ? WiFi.localIP() : WiFi.softAPIP();
  const uint8_t manufacturerData[] = {
      0xff, 0xff,  // Reserved Bluetooth SIG company identifier for development/testing.
      'S', 'S',    // SmartSpin2k payload marker.
      SMARTSPIN2K_IP_ADVERTISEMENT_VERSION,
      ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3],
  };

  if (advertising->setManufacturerData(manufacturerData, sizeof(manufacturerData))) {
    SS2K_LOG(BLE_SERVER_LOG_TAG, "Advertising WiFi IP address %s", ipAddress.toString().c_str());
    return true;
  } else {
    SS2K_LOGW(BLE_SERVER_LOG_TAG, "Unable to fit WiFi IP address in BLE advertisement data");
    return false;
  }
}

bool configureBLEAdvertisement(NimBLEAdvertising* advertising) {
  // NimBLEAdvertising::setManufacturerData() appends rather than replaces an
  // existing field. Rebuild both payloads so an IP refresh cannot retain the
  // original 0.0.0.0 manufacturer data or overflow the 31-byte advertisement.
  advertising->clearData();
  advertising->enableScanResponse(true);
  advertising->addServiceUUID(CYCLINGPOWERSERVICE_UUID);
  advertising->addServiceUUID(CSCSERVICE_UUID);
  // Garmin watches won't connect if HR is advertised.
  // advertising->addServiceUUID(HEARTSERVICE_UUID);
  // Most apps look for FTMS to recognize the device as a smart trainer.
  advertising->addServiceUUID(FITNESSMACHINESERVICE_UUID);
  advertising->setAppearance(0x0484);  // Cycling Power Sensor.
  if (!addIpAddressToAdvertisement(advertising)) return false;

  NimBLEAdvertisementData scanResponseData;
  const std::string advertisedName = bleAdvertisementName(userConfig->getDeviceName());
  if (advertisedName.size() < std::strlen(userConfig->getDeviceName())) {
    scanResponseData.setShortName(advertisedName);
    SS2K_LOGW(BLE_SERVER_LOG_TAG, "BLE device name shortened to '%s' to fit scan response", advertisedName.c_str());
  } else {
    scanResponseData.setName(advertisedName);
  }
  scanResponseData.setCompleteServices(SMARTSPIN2K_SERVICE_UUID);
  if (!advertising->setScanResponseData(scanResponseData)) {
    SS2K_LOGE(BLE_SERVER_LOG_TAG, "Unable to configure BLE scan response data");
    return false;
  }

  advertising->setMaxInterval(250);
  advertising->setMinInterval(160);
  return true;
}
}  // namespace

void BLERequestMtuExchange(uint16_t connectionHandle) {
  const int result = ble_gattc_exchange_mtu(connectionHandle, nullptr, nullptr);
  if (result == 0) {
    SS2K_LOG(BLE_SERVER_LOG_TAG, "Requested ATT MTU exchange for connection %u", connectionHandle);
  } else if (result != BLE_HS_EALREADY) {
    SS2K_LOGW(BLE_SERVER_LOG_TAG, "Unable to request ATT MTU exchange for connection %u: %d", connectionHandle, result);
  }
}

void startBLEServer() {
  // Server Setup
  SS2K_LOG(BLE_SERVER_LOG_TAG, "Starting BLE Server");
  spinBLEServer.pServer = BLEDevice::createServer();
  spinBLEServer.pServer->setCallbacks(new MyServerCallbacks());

  // start services
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  cyclingSpeedCadenceService.setupService(spinBLEServer.pServer, &chrCallbacks);
  cyclingPowerService.setupService(spinBLEServer.pServer, &chrCallbacks);
  heartService.setupService(spinBLEServer.pServer, &chrCallbacks);
  fitnessMachineService.setupService(spinBLEServer.pServer, &chrCallbacks);
  ss2kCustomCharacteristic.setupService(spinBLEServer.pServer);
  deviceInformationService.setupService(spinBLEServer.pServer);
  // zwiftService.setupService(spinBLEServer.pServer);
  // openBikeControlService.setupService(spinBLEServer.pServer);
  // uncoment to enable as controller. Zwift won't pair as ct and controller at the same time.
  // pAdvertising->addServiceUUID(ZWIFT_RIDE_CUSTOM_SERVICE_UUID);
  // pAdvertising->addServiceUUID(OPENBIKECONTROL_SERVICE_UUID);
  if (!configureBLEAdvertisement(pAdvertising)) {
    SS2K_LOGE(BLE_SERVER_LOG_TAG, "Unable to configure BLE advertisement data");
  }

  // wattbikeService.setupService(spinBLEServer.pServer);  // No callback needed
  // sb20Service.begin();
  BLEFirmwareSetup(spinBLEServer.pServer);

  // const std::string fitnessData = {0b00000001, 0b00100000, 0b00000000};
  // pAdvertising->setServiceData(FITNESSMACHINESERVICE_UUID, fitnessData);
  pAdvertising->start();

  SS2K_LOG(BLE_SERVER_LOG_TAG, "Bluetooth Characteristics defined!");
}

void refreshBLEAdvertisementIp() {
  if (!NimBLEDevice::isInitialized()) {
    SS2K_LOGW(BLE_SERVER_LOG_TAG, "BLE is not initialized; IP advertisement refresh deferred");
    return;
  }

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  const bool wasAdvertising      = advertising->isAdvertising();
  if (wasAdvertising && !advertising->stop()) {
    SS2K_LOGW(BLE_SERVER_LOG_TAG, "Unable to stop BLE advertising for IP address refresh");
    return;
  }
  if (!configureBLEAdvertisement(advertising)) {
    SS2K_LOGE(BLE_SERVER_LOG_TAG, "Unable to rebuild BLE advertisement with current IP address");
  }
  if (wasAdvertising && !advertising->start()) {
    SS2K_LOGE(BLE_SERVER_LOG_TAG, "Unable to restart BLE advertising after IP address refresh");
  }
}

void SpinBLEServer::update() {
  // Wheel and crank is used in multiple characteristics. Update first.
  spinBLEServer.updateWheelAndCrankRev();
  // update the BLE information on the server
  heartService.update();
  cyclingPowerService.update();
  cyclingSpeedCadenceService.update();
  fitnessMachineService.update();
  ss2kCustomCharacteristic.update();
  // zwiftService.update();
  // OpenBikeControl sends event-driven notifications from shift handlers.
  // wattbikeService.parseNemit();  // Changed from update() to parseNemit()
  // sb20Service.notify();
}

double SpinBLEServer::calculateSpeed() {
  // Estimate flat-road speed from P = 0.5 * rho * CdA * v^3 + Crr * m * g * v.
  // These conservative defaults model an upright rider plus bike. This is only
  // used when no real or simulated speed source is available.
  constexpr double airDensity          = 1.225;    // kg/m^3
  constexpr double dragArea            = 0.50;     // CdA, m^2
  constexpr double rollingResistance   = 0.004;
  constexpr double totalMass           = 90.0;     // rider and bike, kg
  constexpr double gravity             = 9.80665;  // m/s^2
  constexpr double aerodynamicCoefficient = 0.5 * airDensity * dragArea;
  constexpr double rollingCoefficient     = rollingResistance * totalMass * gravity;

  const double power = rtConfig->watts.getValue();
  if (power <= 0.0) return 0.0;

  // Solve the monotonic equation with a bounded binary search. 30 m/s is well
  // above any expected indoor-bike estimate and keeps the calculation stable.
  double low  = 0.0;
  double high = 30.0;
  for (int i = 0; i < 24; ++i) {
    const double speed = (low + high) / 2.0;
    const double requiredPower = aerodynamicCoefficient * speed * speed * speed + rollingCoefficient * speed;
    if (requiredPower < power) {
      low = speed;
    } else {
      high = speed;
    }
  }

  return ((low + high) / 2.0) * 3.6;  // m/s to km/h
}

void SpinBLEServer::updateWheelAndCrankRev() {
  float wheelSize     = 2.127;  // 700cX28 circumference, typical in meters
  float wheelSpeedMps = 0.0;
  if (rtConfig->getSimulatedSpeed() > 5) {
    wheelSpeedMps = rtConfig->getSimulatedSpeed() / 3.6;
  } else {
    wheelSpeedMps = this->calculateSpeed() / 3.6;  // covert km/h to m/s
  }

  // Calculate wheel revolutions per minute
  float wheelRpm = (wheelSpeedMps / wheelSize) * 60;
  if (wheelRpm > 0) {
    double wheelRevPeriod = (60 * 1024) / wheelRpm;
    spinBLEClient.cscCumulativeWheelRev++;                // Increment cumulative wheel revolutions
    spinBLEClient.cscLastWheelEvtTime += wheelRevPeriod;  // Convert RPM to time, ensuring no division by zero
  }

  float cadence = rtConfig->cad.getValue();
  if (cadence > 0) {
    float crankRevPeriod = (60 * 1024) / cadence;
    spinBLEClient.cscCumulativeCrankRev++;
    spinBLEClient.cscLastCrankEvtTime += crankRevPeriod;
  }
}

// Creating Server Connection Callbacks
void MyServerCallbacks::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
  SS2K_LOG(BLE_SERVER_LOG_TAG, "Bluetooth Remote Client Connected: %s Connected Clients: %d", connInfo.getAddress().toString().c_str(), pServer->getConnectedCount());
  BLERequestMtuExchange(connInfo.getConnHandle());

  if (pServer->getConnectedCount() < CONFIG_BT_NIMBLE_MAX_CONNECTIONS - NUM_BLE_DEVICES) {
    BLEDevice::startAdvertising();
  } else {
    SS2K_LOG(BLE_SERVER_LOG_TAG, "Max Remote Client Connections Reached");
    BLEDevice::stopAdvertising();
  }
}

void MyServerCallbacks::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
  SS2K_LOG(BLE_SERVER_LOG_TAG, "Bluetooth Remote Client Disconnected. Reason: %d (%s) Remaining Clients: %d", reason, NimBLEUtils::returnCodeToString(reason),
           pServer->getConnectedCount());
  BLEFirmwareUpdateOnDisconnect(connInfo.getConnHandle());
  BLEDevice::startAdvertising();
}

void MyServerCallbacks::onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) {
  SS2K_LOG(BLE_SERVER_LOG_TAG, "ATT MTU updated to %u for connection %u", MTU, connInfo.getConnHandle());
}

bool MyServerCallbacks::onConnParamsUpdateRequest(uint16_t handle, const ble_gap_upd_params* params) {
  SS2K_LOG(BLE_SERVER_LOG_TAG, "Updated Server Connection Parameters for handle: %d", handle);
  return true;
}

// END SERVER CALLBACKS

void MyCharacteristicCallbacks::onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
  SS2K_LOG(BLE_SERVER_LOG_TAG, "Read from %s by client: %s", pCharacteristic->getUUID().toString().c_str(), connInfo.getAddress().toString().c_str());
}

void MyCharacteristicCallbacks::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
  if (pCharacteristic->getUUID() == FITNESSMACHINECONTROLPOINT_UUID) {
    spinBLEServer.writeCache.push(pCharacteristic->getValue());
  } else {
    SS2K_LOG(BLE_SERVER_LOG_TAG, "Write to %s is not supported", pCharacteristic->getUUID().toString().c_str());
  }
}

void MyCharacteristicCallbacks::onStatus(NimBLECharacteristic* pCharacteristic, int code) {
// loop through and accumulate the data into a C++ string
// only used for extensive logging.
#ifndef DEBUG_BLE_TX_RX
  return;
#endif
  std::string characteristicValue = pCharacteristic->getValue();
  std::string logValue;
  for (size_t i = 0; i < characteristicValue.length(); ++i) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02x ", (unsigned char)characteristicValue[i]);
    logValue += buf;
  }

  SS2K_LOG(BLE_SERVER_LOG_TAG, "%s -> %s", pCharacteristic->getUUID().toString().c_str(), logValue.c_str());
}

void MyCharacteristicCallbacks::onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) {
  String str       = "Client ID: ";
  NimBLEUUID pUUID = pCharacteristic->getUUID();
  str += connInfo.getConnHandle();
  str += " Address: ";
  str += connInfo.getAddress().toString().c_str();
  if (subValue == 0) {
    str += " Unsubscribed to ";
  } else if (subValue == 1) {
    str += " Subscribed to notifications for ";
  } else if (subValue == 2) {
    str += " Subscribed to indications for ";
  } else if (subValue == 3) {
    str += " Subscribed to notifications and indications for ";
  }
  str += std::string(pCharacteristic->getUUID()).c_str();

  SS2K_LOG(BLE_SERVER_LOG_TAG, "%s", str.c_str());
}

// Return number of clients connected to our server.
int SpinBLEServer::connectedClientCount() {
  if (BLEDevice::getServer()) {
    return BLEDevice::getServer()->getConnectedCount();
  } else {
    return 0;
  }
}

void logCharacteristic(char* buffer, const size_t bufferCapacity, const byte* data, const size_t dataLength, const NimBLEUUID serviceUUID, const NimBLEUUID charUUID,
                       const char* format, ...) {
#ifdef DEBUG_BLE_TX_RX
  int bufferLength = ss2k_log_hex_to_buffer(data, dataLength, buffer, 0, bufferCapacity);
  bufferLength += snprintf(buffer + bufferLength, bufferCapacity - bufferLength, "-> %s | %s | ", serviceUUID.toString().c_str(), charUUID.toString().c_str());
  va_list args;
  va_start(args, format);
  bufferLength += vsnprintf(buffer + bufferLength, bufferCapacity - bufferLength, format, args);
  va_end(args);

  SS2K_LOG(BLE_SERVER_LOG_TAG, "%s", buffer);
#endif
}
