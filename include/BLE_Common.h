/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <memory>
#include <Arduino.h>
#include <queue>
#include <deque>
#include <vector>
#include "Main.h"
#include "BLE_Definitions.h"
// #include "BLE_Wattbike_Service.h"
// #include "BLE_SB20_Service.h"
#include "Constants.h"

// Client size allocated to the queue for receiving characteristic data
#define NOTIFY_DATA_QUEUE_SIZE   25
#define NOTIFY_DATA_QUEUE_LENGTH 10

// BLE Connection Parameters:
// minInterval – [in] The minimum connection interval in 1.25ms units.
// maxInterval – [in] The maximum connection interval in 1.25ms units.
// latency – [in] The number of packets allowed to skip (extends max interval).
// timeout – [in] The timeout time in 10ms units before disconnecting.
const uint16_t connectionParams[] = {24, 48, 0, 200};

// Vector of supported BLE services and their corresponding characteristic UUIDs
struct BLEServiceInfo {
  BLEUUID serviceUUID;
  BLEUUID characteristicUUID;
  String name;
};

namespace BLEServices {
const std::vector<BLEServiceInfo> SUPPORTED_SERVICES = {{CYCLINGPOWERSERVICE_UUID, CYCLINGPOWERMEASUREMENT_UUID, "Cycling Power Service"},
                                                        {CSCSERVICE_UUID, CSCMEASUREMENT_UUID, "Cycling Speed And Cadence Service"},
                                                        {HEARTSERVICE_UUID, HEARTCHARACTERISTIC_UUID, "Heart Rate Service"},
                                                        {ECHELON_DEVICE_UUID, ECHELON_SERVICE_UUID, "Echelon Device"},  // Two lines for Echelon
                                                        {ECHELON_SERVICE_UUID, ECHELON_DATA_UUID, "Echelon Service"},   // Because one is for search, the other for data
                                                        {FITNESSMACHINESERVICE_UUID, FITNESSMACHINEINDOORBIKEDATA_UUID, "Fitness Machine Service"},
                                                        {HID_SERVICE_UUID, HID_REPORT_DATA_UUID, "HID Service"},
                                                        {FLYWHEEL_UART_SERVICE_UUID, FLYWHEEL_UART_TX_UUID, "Flywheel UART Service"}};
}

using BLEServices::SUPPORTED_SERVICES;

#define BLE_CLIENT_LOG_TAG  "BLE_Client"
#define BLE_COMMON_LOG_TAG  "BLE_Common"
#define BLE_SERVER_LOG_TAG  "BLE_Server"
#define BLE_SETUP_LOG_TAG   "BLE_Setup"
#define FMTS_SERVER_LOG_TAG "FTMS_SERVER"
#define CUSTOM_CHAR_LOG_TAG "Custom_C"

// macros to convert different types of bytes into int The naming here sucks and
// should be fixed.
#define bytes_to_s16(MSB, LSB) (((signed int)((signed char)MSB))) << 8 | (((signed char)LSB))
#define bytes_to_u16(MSB, LSB) (((signed int)((signed char)MSB))) << 8 | (((unsigned char)LSB))
#define bytes_to_int(MSB, LSB) ((static_cast<int>((unsigned char)MSB))) << 8 | (((unsigned char)LSB))
// Potentially, something like this is a better way of doing this ^^

// Setup
void setupBLE();
extern TaskHandle_t BLEClientTask;
// ***********************Common**********************************
void BLECommunications();

// Check if a BLE device supports any of our supported services
bool isDeviceSupported(const NimBLEAdvertisedDevice* advertisedDevice, const String& deviceName = "");

// Get service info for a supported device
const BLEServiceInfo* getDeviceServiceInfo(const NimBLEAdvertisedDevice* advertisedDevice, const String& deviceName = "");
// *****************************Server****************************
class MyServerCallbacks : public NimBLEServerCallbacks {
 public:
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo);
  void onDisconnect(NimBLEServer* pServer);
  void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo);
  bool onConnParamsUpdateRequest(uint16_t handle, const ble_gap_upd_params* params);
};

// TODO add the rest of the server to this class
class SpinBLEServer {
 private:
  void updateWheelAndCrankRev();
  // Helper Function to clean up the BLE Services

 public:
  int spinDownFlag      = 0;
  NimBLEServer* pServer = nullptr;
  void notifyShift();
  double calculateSpeed();
  void update();
  int connectedClientCount();
  // Queue to store writes to any of the callbacks to the server
  std::queue<std::string> writeCache;
};

class MyCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
 public:
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
  void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
  void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override;
  void onStatus(NimBLECharacteristic* pCharacteristic, int code) override;
};

extern SpinBLEServer spinBLEServer;
// extern BLE_Wattbike_Service wattbikeService;

void startBLEServer();
void logCharacteristic(char* buffer, const size_t bufferCapacity, const byte* data, const size_t dataLength, const NimBLEUUID serviceUUID, const NimBLEUUID charUUID,
                       const char* format, ...);
void calculateInstPwrFromHR();

// BLE FIRMWARE UPDATER
void BLEFirmwareSetup(NimBLEServer* pServer);

// *****************************Client*****************************

// Keeping the task outside the class so we don't need a mask.
// We're only going to run one anyway.
void bleClientTask(void* pvParameters);

// UUID's the client has methods for
// BLEUUID serviceUUIDs[4] = {FITNESSMACHINESERVICE_UUID,
// CYCLINGPOWERSERVICE_UUID, HEARTSERVICE_UUID, FLYWHEEL_UART_SERVICE_UUID};
// BLEUUID charUUIDs[4] = {FITNESSMACHINEINDOORBIKEDATA_UUID,
// CYCLINGPOWERMEASUREMENT_UUID, HEARTCHARACTERISTIC_UUID,
// FLYWHEEL_UART_TX_UUID};

typedef struct NotifyData {
  NimBLEUUID serviceUUID;
  NimBLEUUID charUUID;
  uint8_t data[NOTIFY_DATA_QUEUE_SIZE];
  size_t length;
} NotifyData;

class SpinBLEAdvertisedDevice {
 private:
  QueueHandle_t dataBufferQueue = nullptr;

  void clearState(bool resetAdvertisedDevice);  // NEW

 public:
  SpinBLEAdvertisedDevice() { clearState(true); }  // NEW

  const NimBLEAdvertisedDevice* advertisedDevice = nullptr;
  NimBLEAddress peerAddress;

  std::string uniqueName = "";  // Stable identifier using adevName2UniqueName()

  int connectedClientID = BLE_HS_CONN_HANDLE_NONE;
  BLEUUID serviceUUID   = (uint16_t)0x0000;
  BLEUUID charUUID      = (uint16_t)0x0000;
  Measurement batt;
  bool isHRM           = false;
  bool isPM            = false;
  bool isCSC           = false;
  bool isCT            = false;
  bool isRemote        = false;
  bool doConnect       = false;
  bool isPostConnected = false;
  unsigned long lastDataUpdateTime = 0;  // Reset disconnect detection timestamp

  void set(const NimBLEAdvertisedDevice* device, int id = BLE_HS_CONN_HANDLE_NONE, BLEUUID inServiceUUID = (uint16_t)0x0000, BLEUUID inCharUUID = (uint16_t)0x0000);
  void reset(bool resetAdvertisedDevice = true);
  bool enqueueData(uint8_t* data, size_t length, NimBLEUUID serviceUUID, NimBLEUUID charUUID);
  NotifyData dequeueData();
};

class SpinBLEClient {
 private:
 public:  // Not all of these need to be public. This should be cleaned up
          // later.
  boolean connectedPM            = false;
  boolean connectedHRM           = false;
  boolean connectedCD            = false;
  boolean connectedCT            = false;
  boolean connectedSpeed         = false;
  boolean connectedRemote        = false;
  boolean doScan                 = true; //Set to true so there's an initial scan on startup
  long int cscCumulativeCrankRev = 0;
  double cscLastCrankEvtTime     = 0.0;
  long int cscCumulativeWheelRev = 0;
  double cscLastWheelEvtTime     = 0.0;

  BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;

  // BLEDevices myBLEDevices;
  SpinBLEAdvertisedDevice myBLEDevices[NUM_BLE_DEVICES];

  void start();
  bool connectToServer();
  // Check for duplicate services of BLEClient and remove the previously
  // connected one.
  void removeDuplicates(NimBLEClient* pClient);
  void resetDevices(NimBLEClient* pClient);
  void postConnect();
  void FTMSControlPointWrite(const uint8_t* pData, int length);
  void connectBLE_HID(NimBLEClient* pClient);
  void keepAliveBLE_HID(NimBLEClient* pClient);
  void handleBattInfo(NimBLEClient* pClient, bool updateNow);
  // Instead of using this directly, set the .doScan flag to start a scan.
  void scanProcess(int duration = DEFAULT_SCAN_DURATION);
  void checkBLEReconnect();
  // Disconnects all devices. They will then be reconnected if scanned and preferred again.
  void reconnectAllDevices();

  bool isRandomizedAddress(const NimBLEAdvertisedDevice* inDev);
  String adevName2UniqueName(const NimBLEAdvertisedDevice* inDev);
};

class ScanCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override;
  void onScanEnd(const NimBLEScanResults& results, int reason) override;

 private:
};

class MyClientCallback : public NimBLEClientCallbacks {
 public:
  void onConnect(NimBLEClient* pClient) override;
  void onDisconnect(NimBLEClient* pClient, int reason) override;
};

extern SpinBLEClient spinBLEClient;
