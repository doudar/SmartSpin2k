/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Assioma Pedal Information for later
BLE Advertised Device found: Name: ASSIOMA17287L, Address: e8:fe:6e:91:9f:16,
appearance: 1156, manufacturer data: 640302018743, serviceUUID:
00001818-0000-1000-8000-00805f9b34fb
*/

#include "Main.h"
#include "BLE_Common.h"
#include "BLE_Fitness_Machine_Service.h"
#include "SS2KLog.h"

#include <ArduinoJson.h>
#include <Constants.h>
#include <memory>
#include <NimBLEDevice.h>

TaskHandle_t BLEClientTask;

SpinBLEClient spinBLEClient;

static MyClientCallback myClientCallback;
static ScanCallbacks myScanCallbacks;

void SpinBLEClient::start() {
  // Create the task for the BLE Client loop
  xTaskCreatePinnedToCore(bleClientTask,    /* Task function. */
                          "BLEClientTask",  /* name of task. */
                          BLE_CLIENT_STACK, /* Stack size of task */
                          NULL,             /* parameter of the task */
                          9,                /* priority of the task  */
                          &BLEClientTask,   /* Task handle to keep track of created task */
                          1);               /* pin task to core */

  NimBLEScan* pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setScanCallbacks(&myScanCallbacks, false);
  pBLEScan->setInterval(49);  // 97
  pBLEScan->setWindow(33);    // 67
  pBLEScan->setDuplicateFilter(true);
  pBLEScan->setActiveScan(true);
}

/**
 * @brief Callback function for BLE notifications.
 *
 * This function is called whenever a notification is received from a BLE characteristic.
 * It handles specific notifications for the HID service and enqueues sensor data for further processing.
 *
 * @param pBLERemoteCharacteristic Pointer to the remote characteristic that generated the notification.
 * @param pData Pointer to the data received in the notification.
 * @param length Length of the data received.
 * @param isNotify Boolean indicating if the notification is a notify or indicate.
 */
static void notifyCB(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  // Parse BLE shifter info.
  if (pBLERemoteCharacteristic->getRemoteService()->getUUID() == HID_SERVICE_UUID) {
    Serial.print(pData[0], HEX);
    if (pData[0] == 0x04) {
      rtConfig->setShifterPosition(rtConfig->getShifterPosition() + 1);
    }
    if (pData[0] == 0x08) {
      rtConfig->setShifterPosition(rtConfig->getShifterPosition() - 1);
    }
  }
  // Enqueue sensor data
  for (size_t i = 0; i < NUM_BLE_DEVICES; i++) {
    if (pBLERemoteCharacteristic->getClient()->getPeerAddress() == spinBLEClient.myBLEDevices[i].peerAddress) {
      spinBLEClient.myBLEDevices[i].enqueueData(pData, length, pBLERemoteCharacteristic->getRemoteService()->getUUID(), pBLERemoteCharacteristic->getUUID());
    }
  }
}

/**
 * @brief Subscribes to all notifications for supported BLE services on the given client.
 *
 * This function iterates through all supported BLE services and subscribes to notifications
 * for each characteristic that supports notifications or indications.
 *
 * @param pClient Pointer to the NimBLEClient object representing the BLE client.
 *                The client must be connected for the function to proceed.
 */
bool subscribeToAllNotifications(NimBLEClient* pClient) {
  bool isSubscribed = false;
  if (!pClient || !pClient->isConnected()) {
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Client not connected for notifications");
    return false;
  }  // The Issue with Echelon is that there are multiple services
  for (const auto& service : BLEServices::SUPPORTED_SERVICES) {
    // Re-check connection health before each service to prevent racing with a disconnect
    if (!pClient->isConnected()) {
      SS2K_LOGW(BLE_CLIENT_LOG_TAG, "Aborting subscription loop: client disconnected mid-iteration");
      break;
    }
    NimBLERemoteService* pSvc = pClient->getService(service.serviceUUID);
    if (pSvc) {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Found %s", service.name.c_str());
      auto chars = pSvc->getCharacteristics(true);
      for (auto* pChr : chars) {
        if (!pChr) {
          continue;
        }
        // Defensive: Ensure characteristic still belongs to this service (avoid stale pointer after service refresh)
        if (pChr->getRemoteService() != pSvc) {
          SS2K_LOGW(BLE_CLIENT_LOG_TAG, "Characteristic parent mismatch while subscribing %s", pChr->getUUID().toString().c_str());
          continue;
        }
        if (!pClient->isConnected()) {
          SS2K_LOGW(BLE_CLIENT_LOG_TAG, "Stopping subscription: client disconnected before subscribing to %s", pChr->getUUID().toString().c_str());
          break;
        }
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Found %s, %s", service.serviceUUID.toString().c_str(), pChr->getUUID().toString().c_str());
        if (pChr->canNotify() || pChr->canIndicate()) {
          bool wantNotify = pChr->canNotify();
          // Wrap subscribe in a try/catch-equivalent pattern: NimBLE doesn't throw, but we guard return status
          if (wantNotify ? pChr->subscribe(true, notifyCB) : pChr->subscribe(false, notifyCB)) {
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "Subscribed to %s %s handle: %d", service.name.c_str(), pChr->getUUID().toString().c_str(), pChr->getHandle());
            isSubscribed = true;
          } else {
            // Additional debug: check CCCD descriptor presence which subscribe() internally uses
            NimBLERemoteDescriptor* cccd = pChr->getDescriptor(NimBLEUUID((uint16_t)0x2902));
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "Failed to subscribe to %s %s (handle %d, hasCCCD=%s)", service.name.c_str(), pChr->getUUID().toString().c_str(), pChr->getHandle(),
                     cccd ? "Y" : "N");
          }
        }
      }
    }
  }
  return isSubscribed;
}

// BLE Client loop task.
// Manages device connections and scanning.
/**
 * @brief Task function to manage BLE client operations.
 *
 * This function handles the BLE client operations including scanning for BLE devices,
 * connecting to BLE servers, and managing BLE connections. It runs in an infinite loop
 * with a delay between iterations.
 *
 * @param pvParameters Pointer to the parameters passed to the task (unused).
 *
 * The function performs the following operations:
 * - Checks and manages BLE reconnections.
 * - Disconnects all connected servers if an update is in progress.
 * - Scans for BLE devices to connect to the client.
 * - Connects BLE servers to the client.
 * - Manages the spin down process for the server.
 *
 * The function uses the following global variables and objects:
 * - spinBLEClient: Manages BLE client operations.
 * - ss2k: Represents the main application state and configuration.
 * - rtConfig: Runtime configuration for the application.
 * - spinBLEServer: Manages BLE server operations.
 *
 * The function also includes debug logging and stack high water mark monitoring
 * when the DEBUG_STACK macro is defined.
 */
void bleClientTask(void* pvParameters) {
  for (;;) {
    delay(BLE_CLIENT_DELAY);  // Delay between loops.

    // disconnect all connected servers if we're updating via BLE
    if (ss2k->isUpdating) {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Disconnecting all connected servers due to update.");
      for (auto& _BLEd : spinBLEClient.myBLEDevices) {  // loop through discovered devices
        if (_BLEd.connectedClientID != BLE_HS_CONN_HANDLE_NONE) {
          if (_BLEd.advertisedDevice) {                                                                // is device registered?
            if ((_BLEd.connectedClientID != BLE_HS_CONN_HANDLE_NONE) && (_BLEd.doConnect == false)) {  // client must not be in connection process
              if (BLEDevice::getClientByPeerAddress(_BLEd.peerAddress)) {                              // nullptr check
                NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(_BLEd.peerAddress);
                pClient->disconnect();
              }
            }
          }
        }
      }
      while (ss2k->isUpdating) {  // wait until the update is done
        if (NimBLEDevice::getScan()->isScanning()) {
          NimBLEDevice::getScan()->stop();  // IMPORTANT! Stop scanning to allow the update to complete
        }
        delay(100);
      }
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Update complete, re-enabling BLE scanning.");
    }

    // Post connect previously connected clients. This needs to be before connect, as it takes a while to complete the connection (let it loop once.)
    spinBLEClient.postConnect();

    // Connect BLE Servers to this client
    for (int x = 0; x < NUM_BLE_DEVICES; x++) {
      if (spinBLEClient.myBLEDevices[x].doConnect == true) {
        // stop in process scans
        NimBLEScan* pBLEScan = NimBLEDevice::getScan();
        if (pBLEScan->isScanning()) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Stopping scan before connecting %s on slot %d ...", spinBLEClient.myBLEDevices[x].uniqueName.c_str(), x);
          while (pBLEScan->isScanning()) {
            pBLEScan->stop();
            delay(100);
          }
        }
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Connecting %s on slot %d ...", spinBLEClient.myBLEDevices[x].uniqueName.c_str(), x);
        if (spinBLEClient.connectToServer()) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "We are now connected to %s", spinBLEClient.myBLEDevices[x].uniqueName.c_str());
        } else {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s connection failed", spinBLEClient.myBLEDevices[x].uniqueName.c_str());
        }
      }
    }

    // Scan for BLE devices that we should connect to this client
    static unsigned long scanDelay = millis();
    if (((millis() - scanDelay) > BLE_RECONNECT_SCAN_INTERVAL)) {
      spinBLEClient.checkBLEReconnect();
      if (spinBLEClient.doScan) {
        spinBLEClient.scanProcess(DEFAULT_SCAN_DURATION);
      }
      scanDelay = millis();
    }

    // Spin Down process for the Server. It's here because it needs to be non-blocking for the maintenance loop.
    // Checking for cadence also so that we don't home when nobody is around.
    if (spinBLEServer.spinDownFlag && rtConfig->cad.getValue()) {
      if (spinBLEServer.spinDownFlag >= 2) {  // Home Both Directions
        ss2k->goHome(true);
      } else {  // Startup Homing
        ss2k->goHome(false);
      }
      spinBLEServer.spinDownFlag = 0;
    }
  }
}

bool SpinBLEClient::connectToServer() {
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Initiating Server Connection");
  NimBLEUUID serviceUUID;
  NimBLEUUID charUUID;

  const NimBLEAdvertisedDevice* myDevice = nullptr;
  int device_number                      = -1;

  for (int i = 0; i < NUM_BLE_DEVICES; i++) {
    if (spinBLEClient.myBLEDevices[i].doConnect == true) {   // Client wants to be connected
      if (spinBLEClient.myBLEDevices[i].advertisedDevice) {  // Client is assigned
        myDevice = spinBLEClient.myBLEDevices[i].advertisedDevice;
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Connecting slot %d", i);
        device_number = i;
        break;
      } else {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "doConnect and client out of alignment. Resetting device slot.");
        spinBLEClient.myBLEDevices[i].reset();
        return false;
      }
    } else {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "doConnect on slot %d not set", i);
    }
  }
  if (myDevice == nullptr) {
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "No Device Found to Connect");
    return false;
  }
  if (myDevice->getServiceUUIDCount() > 0) {
    String deviceName = myDevice->haveName() ? String(myDevice->getName().c_str()) : "Unknown";
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Getting service info for device: %s with %d services", deviceName.c_str(), myDevice->getServiceUUIDCount());

    const BLEServiceInfo* serviceInfo = getDeviceServiceInfo(myDevice, deviceName.c_str());
    if (!serviceInfo) {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "No supported service UUID found for device: %s", deviceName.c_str());
      spinBLEClient.myBLEDevices[device_number].reset();
      return false;
    }

    serviceUUID = serviceInfo->serviceUUID;
    charUUID    = serviceInfo->characteristicUUID;
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Trying to connect to %s (Service UUID: %s)", serviceInfo->name.c_str(), serviceUUID.toString().c_str());
  } else {
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Device has no Service UUID");
    spinBLEClient.myBLEDevices[device_number].reset();
    return false;
  }

  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Forming a connection to: %s", this->adevName2UniqueName(myDevice).c_str());

  NimBLEClient* pClient          = nullptr;
  auto handleFailedClientConnect = [&]() {
    SS2K_LOG(BLE_CLIENT_LOG_TAG, " - Failed to connect client");
    /** Created a client but failed to connect, don't need to keep it as it has no data */
    spinBLEClient.myBLEDevices[device_number].reset();
    return false;
  };
  // Always create a brand-new client for each connection attempt.
  if (NimBLEDevice::getCreatedClientCount() >= NIMBLE_MAX_CONNECTIONS) {
    Serial.println("Max clients reached - no more connections available");
    return false;
  }

  pClient = NimBLEDevice::createClient();
  SS2K_LOG(BLE_CLIENT_LOG_TAG, " - Created new client");
  pClient->setClientCallbacks(&myClientCallback, false);
  pClient->setSelfDelete(true, true);
  // Initial connection parameters: 15ms interval, 0 latency, 1000ms timeout (kept from previous logic)
  pClient->setConnectionParams(connectionParams[0], connectionParams[1], connectionParams[2], 1000);
  pClient->setConnectTimeout(10000);  // 10 seconds
  if (!pClient->connect(myDevice, true, false, false)) {
    return handleFailedClientConnect();
  }

  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Connected to: %s - %s RSSI %d", this->adevName2UniqueName(myDevice).c_str(), pClient->getPeerAddress().toString().c_str(), pClient->getRssi());
  if (serviceUUID == HID_SERVICE_UUID) {
    connectBLE_HID(pClient);
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Successful remote subscription.");
  }

  // Update the advertised device info
  spinBLEClient.myBLEDevices[device_number].doConnect = false;
  spinBLEClient.myBLEDevices[device_number].set(myDevice, pClient->getConnHandle(), serviceUUID, charUUID);
  spinBLEClient.myBLEDevices[device_number].peerAddress = pClient->getPeerAddress();
  removeDuplicates(pClient);

  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Device Connected");
  return true;
}

/**  None of these are required as they will be handled by the library with defaults. **
 **                       Remove as you see fit for your needs                        */

void MyClientCallback::onConnect(NimBLEClient* pClient) { SS2K_LOG(BLE_CLIENT_LOG_TAG, "Connected, %s", pClient->getPeerAddress().toString().c_str()); }

void MyClientCallback::onDisconnect(NimBLEClient* pClient, int reason) {
  NimBLEAddress addr = pClient->getPeerAddress();
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Client %s Disconnected, reason = %d", addr.toString().c_str(), reason);
  for (size_t i = 0; i < NUM_BLE_DEVICES; i++) {
    if (addr == spinBLEClient.myBLEDevices[i].peerAddress) {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Detected %s Disconnect", spinBLEClient.myBLEDevices[i].serviceUUID.toString().c_str());
      if ((spinBLEClient.myBLEDevices[i].charUUID == CYCLINGPOWERMEASUREMENT_UUID) || (spinBLEClient.myBLEDevices[i].charUUID == FITNESSMACHINEINDOORBIKEDATA_UUID) ||
          (spinBLEClient.myBLEDevices[i].charUUID == FLYWHEEL_UART_RX_UUID) || (spinBLEClient.myBLEDevices[i].charUUID == ECHELON_SERVICE_UUID) ||
          (spinBLEClient.myBLEDevices[i].charUUID == CYCLINGPOWERSERVICE_UUID) || (spinBLEClient.myBLEDevices[i].charUUID == CSCSERVICE_UUID)) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Deregistered PM on Disconnect");
      }
      if ((spinBLEClient.myBLEDevices[i].charUUID == HEARTCHARACTERISTIC_UUID)) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Deregistered HR on Disconnect");
      }
      if ((spinBLEClient.myBLEDevices[i].charUUID == HID_REPORT_DATA_UUID)) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Deregistered Remote on Disconnect");
      }
      spinBLEClient.myBLEDevices[i].reset(true);  // fully clear
    }
  }
  return;
}

/**
 * @brief Callback function that is called when a BLE device is found during scanning.
 *
 * This function processes the advertised BLE device, checks if it matches the supported devices,
 * and attempts to connect to it if it matches the user configuration.
 *
 * @param advertisedDevice Pointer to the NimBLEAdvertisedDevice object representing the found device.
 *
 * The function performs the following steps:
 * - Logs the found device.
 * - Checks if the device has a service UUID and if it is supported.
 * - Depending on the service UUID, it checks if the device matches the user configuration for
 *   connected remote, heart monitor, or power meter.
 * - If the device matches the user configuration, it attempts to connect to the device and logs the result.
 * - If the device does not match the user configuration, it ignores the device.
 */
void ScanCallbacks::onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
  // Defensive check - we've seen null devices causing crashes
  if (!advertisedDevice) {
    SS2K_LOGE(BLE_CLIENT_LOG_TAG, "Scan onResult received NULL advertisedDevice!");
    return;
  }

  // Define granular constants for maximal reuse during logging
  const char* const MATCHED               = "Matched ";
  const char* const DIDNT_MATCH_THE_SAVED = " didn't match the saved: ";
  const char* const STRING_MATCHED_ANY    = " String Matched Any";
  const char* const NAME                  = "Name ";
  const char* const REMOTE                = "Remote";
  const char* const HRM                   = "HRM";
  const char* const PM                    = "PM";
  String aDevName                         = spinBLEClient.adevName2UniqueName(advertisedDevice);
  std::string addrStr                     = advertisedDevice->getAddress().toString();
  const char* aDevAddr                    = addrStr.c_str();

  if (advertisedDevice->haveServiceUUID()) {
    const BLEServiceInfo* serviceInfo = getDeviceServiceInfo(advertisedDevice, aDevName);
    String servicesStr                = "services: ";
    for (size_t i = 0; i < advertisedDevice->getServiceUUIDCount(); i++) {
      std::string uuidStr = advertisedDevice->getServiceUUID(i).to16().toString();
      servicesStr += uuidStr.c_str();
      servicesStr += " ";
    }
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Found device %s with %s", aDevName.c_str(), servicesStr.c_str());
    if (serviceInfo) {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Supported Device: %s with service %s", aDevName.c_str(), serviceInfo->name.c_str());
      const NimBLEUUID& primaryServiceUUID = serviceInfo->serviceUUID;
      //check to see if we're already connected to this device
      for (size_t i = 0; i < NUM_BLE_DEVICES; i++) {
        if (spinBLEClient.myBLEDevices[i].advertisedDevice != nullptr) {
          if (aDevName.c_str() == spinBLEClient.myBLEDevices[i].uniqueName.c_str()) {
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s already connected on slot %d", aDevName.c_str(), i);
            return;  // Already connected to this device
          }
        }
      }
      // Handling for BLE connected remotes
      if (primaryServiceUUID == HID_SERVICE_UUID) {
        if (strcmp(userConfig->getConnectedRemote(), ANY) == 0) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s %s%s", aDevName.c_str(), REMOTE, STRING_MATCHED_ANY);
        } else {
          bool nameMatched = (aDevName == userConfig->getConnectedRemote()) ? true : false;
          bool addrMatched = strcmp(aDevAddr, userConfig->getConnectedRemote()) == 0;
          if (!nameMatched && !addrMatched || strcmp(userConfig->getConnectedRemote(), NONE) == 0) {
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s %s%s%s", REMOTE, aDevName.c_str(), DIDNT_MATCH_THE_SAVED, userConfig->getConnectedRemote());
            return;  // Ignore this device;
          } else {
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s %s%s%s", REMOTE, NAME, MATCHED, aDevName.c_str());
          }
        }
      } else if (primaryServiceUUID == HEARTSERVICE_UUID) {
        if (strcmp(userConfig->getConnectedHeartMonitor(), ANY) == 0) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s %s%s", aDevName.c_str(), HRM, STRING_MATCHED_ANY);
        } else {
          bool nameMatched = (aDevName == userConfig->getConnectedHeartMonitor()) ? true : false;
          bool addrMatched = strcmp(aDevAddr, userConfig->getConnectedHeartMonitor()) == 0;
          if (!nameMatched && !addrMatched || strcmp(userConfig->getConnectedHeartMonitor(), NONE) == 0) {
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s %s%s%s", HRM, aDevName.c_str(), DIDNT_MATCH_THE_SAVED, userConfig->getConnectedHeartMonitor());
            return;  // Ignore this device;
          } else {
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s %s%s%s", HRM, NAME, MATCHED, aDevName.c_str());
          }
        }
      } else {
        // Power Meter
        if (strcmp(userConfig->getConnectedPowerMeter(), ANY) == 0) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s, %s%s", aDevName.c_str(), PM, STRING_MATCHED_ANY);
        } else {
          bool nameMatched = (aDevName == userConfig->getConnectedPowerMeter()) ? true : false;
          if (!nameMatched || strcmp(userConfig->getConnectedPowerMeter(), NONE) == 0) {
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s %s%s%s", PM, aDevName.c_str(), DIDNT_MATCH_THE_SAVED, userConfig->getConnectedPowerMeter());
            return;  // Ignore this device;
          } else {
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s %s%s%s", PM, NAME, MATCHED, aDevName.c_str());
          }
        }
      }

      for (size_t i = 0; i < NUM_BLE_DEVICES; i++) {
        // Check if slot is available or if this device is already assigned to this slot
        // For randomized addresses (Android devices), use uniqueName for comparison
        // For traditional devices, fall back to address comparison for backward compatibility
        bool slotAvailable = (spinBLEClient.myBLEDevices[i].advertisedDevice == nullptr);
        bool deviceMatches = false;

        if (!slotAvailable) {
          // Check if this is the same device using stable identifier
          if (!spinBLEClient.myBLEDevices[i].uniqueName.empty()) {
            // Use unique name comparison for stable identification
            deviceMatches = (aDevName == String(spinBLEClient.myBLEDevices[i].uniqueName.c_str()));
          } else {
            // Fall back to address comparison for backward compatibility
            deviceMatches = (advertisedDevice->getAddress() == spinBLEClient.myBLEDevices[i].peerAddress);
          }
        }

        if (slotAvailable || deviceMatches) {
          spinBLEClient.myBLEDevices[i].set(advertisedDevice, BLE_HS_CONN_HANDLE_NONE, primaryServiceUUID);
          spinBLEClient.myBLEDevices[i].doConnect = true;
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s assigned slot: %d", aDevName.c_str(), i);
          break;
        }
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Slot %d contains %s", i, spinBLEClient.myBLEDevices[i].uniqueName.c_str());
      }
    }
  }
}

/**
 * @brief Scan for BLE servers and add them to a list.
 *
 * This function initiates a Bluetooth Low Energy scan process to discover
 * available devices. It ensures that scans are not overlapping by checking
 * if a scan is already in progress and implementing a waiting mechanism.
 *
 * @details The function uses a static boolean 'waitForScanToComplete' to add a delay
 * of one program loop cycle (BLE_CLIENT_DELAY) after a scan ends before starting
 * a new one. This delay is crucial because starting a new scan clears the vector
 * of previously found devices, which might still be getting processed in the
 * onScanEnd callback. Without this delay, data could be getting read as it is deleted, causing a crash.
 *
 * @param duration The duration in seconds for which the scan should run
 */
void SpinBLEClient::scanProcess(int duration) {
  this->doScan = false;  // Confirming we did the scan

  NimBLEScan* pBLEScan = NimBLEDevice::getScan();

  static bool waitForOnScanEndToComplete = false;
  if (pBLEScan->isScanning()) {
    waitForOnScanEndToComplete = true;
    return;
  }
  if (waitForOnScanEndToComplete) {
    waitForOnScanEndToComplete = false;
    return;
  }

  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Scanning for BLE servers and putting them into a list...");
  pBLEScan->start(duration, false, true);
}

void ScanCallbacks::onScanEnd(const NimBLEScanResults& results, int reason) {
  int count = results.getCount();
  JsonDocument devices;

  // Check if 'devices' JSON document already exists and has content; if so, deserialize it.
  const char* foundDevicesJson = userConfig->getFoundDevices();
  if (foundDevicesJson[0] != '\0') {
    deserializeJson(devices, userConfig->getFoundDevices());
  }

  for (int i = 0; i < count; i++) {
    const NimBLEAdvertisedDevice* d = results.getDevice(i);

    // Check for duplicates by name or address before adding
    bool isDuplicate = false;
    for (JsonPair kv : devices.as<JsonObject>()) {
      JsonObject obj = kv.value().as<JsonObject>();
      if (obj["name"] && obj["name"] == spinBLEClient.adevName2UniqueName(d)) {
        isDuplicate = true;
        break;
      }
    }

    if (!isDuplicate && d->haveServiceUUID() && isDeviceSupported(d, spinBLEClient.adevName2UniqueName(d).c_str())) {
      String device = "device " + String(devices.size());  // Use the current size to index the new device

      devices[device]["name"] = spinBLEClient.adevName2UniqueName(d);

      // Workaround for IC4 not advertising FTMS as the first service.
      // Potentially others may need to be added in the future.
      // The symptom was the bike name not showing up in the HTML.
      if (d->haveServiceUUID() && d->isAdvertisingService(FITNESSMACHINESERVICE_UUID)) {
        devices[device]["UUID"] = FITNESSMACHINESERVICE_UUID.toString();
      } else {
        devices[device]["UUID"] = d->getServiceUUID().toString();
      }
    }
  }

  String output;
  serializeJson(devices, output);
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Found Devices: %s", output.c_str());
  userConfig->setFoundDevices(output);  // Save the updated JSON document
}

// remove the last connected BLE Power Meter
void SpinBLEClient::removeDuplicates(NimBLEClient* pClient) {
  // BLEAddress thisAddress = pClient->getPeerAddress();
  SpinBLEAdvertisedDevice tBLEd;
  SpinBLEAdvertisedDevice oldBLEd;
  for (size_t i = 0; i < NUM_BLE_DEVICES; i++) {  // Disconnect oldest PM to avoid two connected.
    tBLEd = this->myBLEDevices[i];
    if (tBLEd.peerAddress == pClient->getPeerAddress()) {
      break;
    }
  }

  for (size_t i = 0; i < NUM_BLE_DEVICES; i++) {  // Disconnect oldest PM to avoid two connected.
    oldBLEd = this->myBLEDevices[i];
    if (oldBLEd.advertisedDevice) {
      if ((tBLEd.serviceUUID == oldBLEd.serviceUUID) && (tBLEd.peerAddress != oldBLEd.peerAddress)) {
        if (BLEDevice::getClientByPeerAddress(oldBLEd.peerAddress)) {
          if (BLEDevice::getClientByPeerAddress(oldBLEd.peerAddress)->isConnected()) {
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s Detected as a duplicate.  Disconnecting: %s", tBLEd.peerAddress.toString().c_str(), oldBLEd.peerAddress.toString().c_str());
            NimBLEDevice::deleteClient(BLEDevice::getClientByPeerAddress(oldBLEd.peerAddress));
            oldBLEd.reset(true);
            return;
          }
        }
      }
    }
  }
}

void SpinBLEClient::resetDevices(NimBLEClient* pClient) {
  for (auto& _BLEd : spinBLEClient.myBLEDevices) {
    if (pClient->getPeerAddress() == _BLEd.peerAddress) {
      SS2K_LOGW(BLE_CLIENT_LOG_TAG, "Reset Client: %s", _BLEd.peerAddress.toString().c_str());
      _BLEd.reset();
    }
  }
}

// Control a connected FTMS trainer. If no args are passed, treat it like an external stepper motor.
void SpinBLEClient::FTMSControlPointWrite(const uint8_t* pData, int length) {
  if (userConfig->getFTMSControlPointWrite()) {
    NimBLEClient* pClient = nullptr;
    uint8_t modData[7];
    for (int i = 0; i < length; i++) {
      modData[i] = pData[i];
    }
    for (int i = 0; i < NUM_BLE_DEVICES; i++) {
      if (myBLEDevices[i].isPostConnected && (myBLEDevices[i].serviceUUID == FITNESSMACHINESERVICE_UUID)) {
        if (NimBLEDevice::getClientByPeerAddress(myBLEDevices[i].peerAddress)->getService(FITNESSMACHINESERVICE_UUID)) {
          pClient = NimBLEDevice::getClientByPeerAddress(myBLEDevices[i].peerAddress);
          break;
        }
      }
    }
    if (pClient) {
      NimBLERemoteCharacteristic* writeCharacteristic = pClient->getService(FITNESSMACHINESERVICE_UUID)->getCharacteristic(FITNESSMACHINECONTROLPOINT_UUID);
      int logBufLength                                = 0;
      if (writeCharacteristic) {
        const int kLogBufCapacity = length + 40;
        char logBuf[kLogBufCapacity];
        if (modData[0] == FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters) {  // use virtual Shifting
          int incline = ss2k->getTargetPosition() / userConfig->getInclineMultiplier();
          modData[3]  = (uint8_t)(incline & 0xff);
          modData[4]  = (uint8_t)(incline >> 8);
          writeCharacteristic->writeValue(modData, length);
          logBufLength = ss2k_log_hex_to_buffer(modData, length, logBuf, 0, kLogBufCapacity);
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Shifted Sim Data: %d", rtConfig->getShifterPosition());
        } else {
          writeCharacteristic->writeValue(modData, length);
          logBufLength = ss2k_log_hex_to_buffer(modData, length, logBuf, 0, kLogBufCapacity);
          logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "-> Shifted ERG Data: %d", rtConfig->getShifterPosition());
        }
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s", logBuf);
      }
    }
  }
}

void SpinBLEClient::postConnect() {
  for (auto& _BLEd : spinBLEClient.myBLEDevices) {
    // Check that the device has been assigned and it hasn't been post connected.
    if ((_BLEd.connectedClientID != BLE_HS_CONN_HANDLE_NONE) && !_BLEd.isPostConnected) {
      // Guard against stale / cleared advertisedDevice pointers (can happen after disconnect + erase())
      if (_BLEd.advertisedDevice == nullptr) {
        SS2K_LOGW(BLE_CLIENT_LOG_TAG, "Skipping postConnect: null advertisedDevice (ConnID %d)", _BLEd.connectedClientID);
        continue;
      }
      // Prefer the stored uniqueName (captured at discovery) to avoid dereferencing the NimBLEAdvertisedDevice
      // unnecessarily (haveName()->findAdvField() has caused crashes when backing storage was freed).
      String adevName = _BLEd.uniqueName.empty() ? this->adevName2UniqueName(_BLEd.advertisedDevice) : String(_BLEd.uniqueName.c_str());
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Post connecting: %s , ConnID %d, PrimaryChar %s", adevName.c_str(), _BLEd.connectedClientID, _BLEd.charUUID.toString().c_str());
      NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(_BLEd.peerAddress);
      if (pClient) {
        BLEDevice::getServer()->updateConnParams(pClient->getConnHandle(), connectionParams[0], connectionParams[1], connectionParams[2], connectionParams[3]);
        _BLEd.isPostConnected = subscribeToAllNotifications(pClient);
        if (!_BLEd.isPostConnected) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Failed to subscribe to notifications for %s", adevName.c_str());
          return;
        }
        spinBLEClient.handleBattInfo(pClient, false);
        if (_BLEd.charUUID == ECHELON_SERVICE_UUID) {
          NimBLERemoteCharacteristic* writeCharacteristic = pClient->getService(ECHELON_SERVICE_UUID)->getCharacteristic(ECHELON_WRITE_UUID);
          if (writeCharacteristic == nullptr) {
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "Failed to find Echelon write characteristic UUID: %s", ECHELON_WRITE_UUID.toString().c_str());
            pClient->disconnect();
            return;
          }
          // Enable device notifications
          byte message[] = {0xF0, 0xB0, 0x01, 0x01, 0xA2};
          writeCharacteristic->writeValue(message, 5);
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Activated Echelon callbacks.");
          rtConfig->setMinResistance(MIN_ECHELON_RESISTANCE);
          rtConfig->setMaxResistance(MAX_ECHELON_RESISTANCE);
        }

        if ((_BLEd.charUUID == FITNESSMACHINEINDOORBIKEDATA_UUID)) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Updating Connection Params for: %s", _BLEd.peerAddress.toString().c_str());
          spinBLEClient.handleBattInfo(pClient, true);

          auto featuresCharacteristic = pClient->getService(FITNESSMACHINESERVICE_UUID)->getCharacteristic(FITNESSMACHINEFEATURE_UUID);
          if (featuresCharacteristic == nullptr) {
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "Failed to find FTMS features characteristic UUID: %s", FITNESSMACHINEFEATURE_UUID.toString().c_str());
            return;
          }

          if (featuresCharacteristic->canRead()) {
            auto value = featuresCharacteristic->readValue();
            if (value.size() < sizeof(uint64_t)) {
              SS2K_LOG(BLE_CLIENT_LOG_TAG, "Failed to read FTMS features characteristic");
              return;
            }

            // We're only interested in the machine fitness features, not the target setting features.
            auto features = *reinterpret_cast<const uint32_t*>(value.data());
            if (!(features & FitnessMachineFeatureFlags::Types::ElapsedTimeSupported) || !(features & FitnessMachineFeatureFlags::Types::RemainingTimeSupported)) {
              SS2K_LOG(BLE_CLIENT_LOG_TAG, "FTMS Control Point StartOrResume not supported");
              return;
            }
          }

          NimBLERemoteCharacteristic* writeCharacteristic = pClient->getService(FITNESSMACHINESERVICE_UUID)->getCharacteristic(FITNESSMACHINECONTROLPOINT_UUID);
          if (writeCharacteristic == nullptr) {
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "Failed to find FTMS control characteristic UUID: %s", FITNESSMACHINECONTROLPOINT_UUID.toString().c_str());
            return;
          }

          // If we would like to control an external FTMS trainer. With most spin bikes we would want this off, but it's useful if you want to use the SmartSpin2k as an
          // appliance.
          if (userConfig->getFTMSControlPointWrite()) {
            writeCharacteristic->writeValue(FitnessMachineControlPointProcedure::RequestControl, 1);
            delay(BLE_NOTIFY_DELAY);
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "Activated FTMS Training.");
          }
          writeCharacteristic->writeValue(FitnessMachineControlPointProcedure::StartOrResume, 1);
        }
      }
    }
  }
}

bool SpinBLEAdvertisedDevice::enqueueData(uint8_t* data, size_t length, NimBLEUUID serviceUUID, NimBLEUUID charUUID) {
  NotifyData notifyData;

  if (!uxQueueSpacesAvailable(this->dataBufferQueue)) {
    // Serial.println("No space available in queue. Skipping enqueue of data.");
    return pdFALSE;
  }

  // Ensure we don't exceed the buffer size to prevent stack smashing
  if (length > NOTIFY_DATA_QUEUE_SIZE) {
    SS2K_LOGW(BLE_CLIENT_LOG_TAG, "BLE data length (%d) exceeds buffer size (%d), truncating", length, NOTIFY_DATA_QUEUE_SIZE);
    length = NOTIFY_DATA_QUEUE_SIZE;
  }

  notifyData.length      = length;
  notifyData.charUUID    = charUUID;
  notifyData.serviceUUID = serviceUUID;
  for (size_t i = 0; i < length; i++) {
    notifyData.data[i] = data[i];
    // Serial.printf("%02x ", notifyData.data[i]);
  }
  // Serial.printf("\n");

  if (xQueueSendToBack(this->dataBufferQueue, &notifyData, 10) == pdFALSE) {
    //  Serial.println("Failed to enqueue data.  Freeing data.");
    return pdFALSE;
  }
  // Serial.printf("Successfully enqueued data. %d. \n", notifyData.length);
  return pdTRUE;
}

NotifyData SpinBLEAdvertisedDevice::dequeueData() {
  NotifyData receivedNotifyData;
  // Initialize to safe values
  receivedNotifyData.length = 0;
  memset(receivedNotifyData.data, 0, NOTIFY_DATA_QUEUE_SIZE);

  if (this->dataBufferQueue == nullptr) {
    SS2K_LOGW(BLE_CLIENT_LOG_TAG, "Queue not created. Skipping dequeue of data.");
    return receivedNotifyData;
  }

  if (xQueueReceive(this->dataBufferQueue, &receivedNotifyData, 0) == pdTRUE) {
    // Validate data length to prevent buffer overruns
    if (receivedNotifyData.length > NOTIFY_DATA_QUEUE_SIZE) {
      SS2K_LOGE(BLE_CLIENT_LOG_TAG, "Invalid data length %d (max %d). Discarding.", receivedNotifyData.length, NOTIFY_DATA_QUEUE_SIZE);
      receivedNotifyData.length = 0;
    }
  }

  // Return data (either valid dequeued data or empty initialized data)
  return receivedNotifyData;
}

void SpinBLEClient::connectBLE_HID(NimBLEClient* pClient) {
  NimBLERemoteService* pSvc = nullptr;
  pSvc                      = pClient->getService(HID_SERVICE_UUID);
  if (pSvc) { /** make sure it's not null */
    // This returns the HID report descriptor like this
    // HID_REPORT_MAP 0x2a4b Value: 5,1,9,2,A1,1,9,1,A1,0,5,9,19,1,29,5,15,0,25,1,75,1,
    // Copy and paste the value digits to http://eleccelerator.com/usbdescreqparser/
    // to see the decoded report descriptor.
    /*NimBLERemoteCharacteristic *pChr = nullptr;
    pChr = pSvc->getCharacteristic(HID_REPORT_MAP_UUID);
    if (pChr) { /** make sure it's not null */
    /*  Serial.print("HID_REPORT_MAP ");
      if (pChr->canRead()) {
        std::string value = pChr->readValue();
        Serial.print(pChr->getUUID().toString().c_str());
        Serial.print(" Value: ");
        uint8_t *p = (uint8_t *)value.data();
        for (size_t i = 0; i < value.length(); i++) {
          Serial.print(p[i], HEX);
          Serial.print(',');
        }
        Serial.println();
      }
    } else {
      Serial.println("HID REPORT MAP char not found.");
    }
*/
    // Subscribe to characteristics HID_REPORT_DATA.
    // One real device reports 2 with the same UUID but
    // different handles. Using getCharacteristic() results
    // in subscribing to only one.
    std::vector<NimBLERemoteCharacteristic*> charVector = pSvc->getCharacteristics(true);
    for (auto& it : charVector) {
      if (it->getUUID() == NimBLEUUID(HID_REPORT_DATA_UUID)) {
        Serial.println(it->toString().c_str());
        if (it->canNotify()) {
          if (!it->subscribe(true, notifyCB)) {
            /** Disconnect if subscribe failed */
            Serial.println("HID subscribe notification failed");
            NimBLEDevice::deleteClient(pClient);
            return;  // false;
          } else {
            Serial.println("subscribed to HID");
          }
        }
      }
    }
  }
  Serial.println("Done with this device!");
  return;  // true;
}

void SpinBLEClient::keepAliveBLE_HID(NimBLEClient* pClient) {
  static int intervalTimer = millis();
  if ((millis() - intervalTimer) < 6000) {
    return;
  }
  NimBLERemoteService* pSvc        = nullptr;
  NimBLERemoteCharacteristic* pChr = nullptr;
  pSvc                             = pClient->getService(HID_SERVICE_UUID);
  if (pSvc) { /** make sure it's not null */
    pChr = pSvc->getCharacteristic(HID_REPORT_MAP_UUID);
    if (pChr) {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "BLE HID Keep Alive");
      pClient->setConnectionParams(12, 12, 0, 3200);
      intervalTimer = millis();
    } else {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Keep Alive failed");
    }
  }
}

void SpinBLEClient::checkBLEReconnect() {
  char notConnectedDevices[32] = {0};
  size_t offset                = 0;

  // Diagnostic: capture current config strings and states
  const char* cfgHRM    = userConfig->getConnectedHeartMonitor();
  const char* cfgPM     = userConfig->getConnectedPowerMeter();
  const char* cfgRemote = userConfig->getConnectedRemote();
  bool wantHRM          = (strcmp(cfgHRM, NONE) != 0);
  bool wantPM           = (strcmp(cfgPM, NONE) != 0);
  bool wantRemote       = (strcmp(cfgRemote, NONE) != 0);

  if (wantHRM && !spinBLEClient.connectedHRM) {
    offset += snprintf(notConnectedDevices + offset, sizeof(notConnectedDevices) - offset, "HRM ");
  }
  if (wantPM && !(spinBLEClient.connectedPM || spinBLEClient.connectedCD)) {
    offset += snprintf(notConnectedDevices + offset, sizeof(notConnectedDevices) - offset, "PM ");
  }
  if (wantRemote && !(spinBLEClient.connectedRemote)) {
    offset += snprintf(notConnectedDevices + offset, sizeof(notConnectedDevices) - offset, "Remote ");
  }
  if (offset > 0) {
    SS2K_LOG(BLE_CLIENT_LOG_TAG,
             "Devices not connected: %s (cfgHRM='%s' needHRM=%d hrmState=%d cfgPM='%s' needPM=%d pmState=%d cadState=%d cfgRemote='%s' needRemote=%d remoteState=%d)",
             notConnectedDevices, cfgHRM, wantHRM, spinBLEClient.connectedHRM, cfgPM, wantPM, spinBLEClient.connectedPM, spinBLEClient.connectedCD, cfgRemote, wantRemote,
             spinBLEClient.connectedRemote);
    this->doScan = true;
  } else {
    // SS2K_LOG(BLE_CLIENT_LOG_TAG, "All required BLE devices connected or disabled (cfgHRM='%s' hrm=%d cfgPM='%s' pm=%d cad=%d cfgRemote='%s' remote=%d)", cfgHRM,
    //          spinBLEClient.connectedHRM, cfgPM, spinBLEClient.connectedPM, spinBLEClient.connectedCD, cfgRemote, spinBLEClient.connectedRemote);
  }
}

void SpinBLEClient::reconnectAllDevices() {
  for (auto i : spinBLEClient.myBLEDevices) {
    if (NimBLEDevice::getClientByHandle(i.connectedClientID)) {
      if (NimBLEDevice::getClientByHandle(i.connectedClientID)->isConnected()) {
        NimBLEDevice::getClientByHandle(i.connectedClientID)->disconnect();
        i.reset(true);
      }
    }
  }
}

// Poll BLE devices for battCharacteristic if available and read value.
void SpinBLEClient::handleBattInfo(NimBLEClient* pClient, bool updateNow = false) {
  static unsigned long last_battery_update = 0;
  if (pClient->getService(BATTERYSERVICE_UUID) == nullptr) {
    return;
  }
  if (pClient->getService(BATTERYSERVICE_UUID)->getCharacteristic(BATTERYCHARACTERISTIC_UUID) == nullptr) {
    return;
  }
  BLERemoteCharacteristic* battCharacteristic = pClient->getService(BATTERYSERVICE_UUID)->getCharacteristic(BATTERYCHARACTERISTIC_UUID);
  if (battCharacteristic != nullptr) {
    std::string value = battCharacteristic->readValue();
    rtConfig->batt.setValue((uint8_t)value[0]);
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s Battery updated %d", pClient->getPeerAddress().toString().c_str(), (int)value[0]);
  }
}

// Helper function to detect if a BLE address is randomized (typically Android devices)
// Updated: only treat as randomized if it's a private random address (dynamic), not static random.
bool SpinBLEClient::isRandomizedAddress(const NimBLEAdvertisedDevice* inDev) {
  if (!inDev) {
    return false;
  }

  // Address string format "xx:xx:xx:xx:xx:xx"; take the first byte ("xx")
  const std::string addrStr = inDev->getAddress().toString();
  if (addrStr.size() < 2) {
    return false;
  }

  char firstByteStr[3] = {addrStr[0], addrStr[1], '\0'};
  int msb              = strtol(firstByteStr, nullptr, 16);
  if (msb < 0) {
    return false;
  }

  // For Random Device Addresses, the two MSBs of the most significant byte define subtype:
  // 0b11 (0xC0): Static Random (stable)        -> NOT randomized for our purposes
  // 0b01 (0x40): Resolvable Private (dynamic)  -> randomized
  // 0b00 (0x00): Non-Resolvable Private        -> randomized
  // 0b10        Reserved
  uint8_t topBits = static_cast<uint8_t>(msb) & 0xC0;
  return (topBits == 0x40) || (topBits == 0x00);
}

// Returns a device name with a stable suffix when possible.
// - Public or static-random addresses: preserve old behavior (append last 2 hex of address)
// - Private random addresses: try manufacturer data last byte as suffix; else just the base name
String SpinBLEClient::adevName2UniqueName(const NimBLEAdvertisedDevice* inDev) {
  if (!inDev || !inDev->getAddress()) {
    return "null";
  }

  // Defensive: Some crashes have been traced to NimBLEAdvertisedDevice::haveName()
  // calling findAdvField() after the underlying advertisement data has been
  // invalidated (e.g. after a disconnect + erase()). We attempt to detect an
  // obviously invalid state: zeroed address and no services.
  // (Address string of all zeros can indicate cleared object.)
  const std::string addrCheck = inDev->getAddress().toString();
  if (addrCheck == "00:00:00:00:00:00") {
    return String("unknown 00");
  }

  if (inDev->haveName()) {
    String outName = String(inDev->getName().c_str());

    // Private random address (dynamic) path
    if (isRandomizedAddress(inDev)) {
      // Prefer last byte of manufacturer data as a stable-ish suffix if present
      if (inDev->haveManufacturerData()) {
        const std::string& mfg = inDev->getManufacturerData();
        if (!mfg.empty()) {
          uint8_t last = static_cast<uint8_t>(mfg.back());
          char buf[3];
          // lower-case hex to match address style
          snprintf(buf, sizeof(buf), "%02x", last);
          outName += " " + String(buf);
          return outName;
        }
      }
      // Fallback: just the base name (no changing MAC-based suffix)
      return outName;
    }

    // Backward-compatible path for public or static-random addresses
    const std::string addrStrStd = inDev->getAddress().toString();
    if (addrStrStd.size() >= 2) {
      String addrStr = String(addrStrStd.c_str());
      outName += " " + addrStr.substring(addrStr.length() - 2);
    }
    return outName;
  } else {
    // No name; keep using the address as the identifier
    String outName = inDev->getAddress().toString().c_str();
    return outName;
  }
}

void SpinBLEAdvertisedDevice::set(const NimBLEAdvertisedDevice* device, int id, BLEUUID inServiceUUID, BLEUUID inCharUUID) {
  // Defensive null check to prevent crashes
  if (!device) {
    SS2K_LOGE(BLE_CLIENT_LOG_TAG, "ERROR: Attempt to set null device!");
    return;
  }
  String adevName = spinBLEClient.adevName2UniqueName(device);
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Setting Device %s", adevName.c_str());
  this->advertisedDevice = const_cast<const NimBLEAdvertisedDevice*>(device);
  this->peerAddress      = device->getAddress();
  // Set the unique name for stable device identification
  this->uniqueName        = adevName.c_str();
  this->connectedClientID = id;
  this->serviceUUID       = BLEUUID(inServiceUUID);
  this->charUUID          = BLEUUID(inCharUUID);

  // Create the queue if it doesn't exist
  if (this->dataBufferQueue == nullptr) {
    this->dataBufferQueue = xQueueCreate(6, sizeof(NotifyData));
    if (this->dataBufferQueue == nullptr) {
      SS2K_LOGE(BLE_CLIENT_LOG_TAG, "Failed to create data buffer queue!");
    }
  }

  // Only register services when we have a connected client
  if (id != BLE_HS_CONN_HANDLE_NONE) {
    NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(device->getAddress());
    if (pClient) {
      // Get all services
      const std::vector<NimBLERemoteService*>& services = pClient->getServices(true);
      for (auto& pService : services) {
        BLEUUID serviceUUID = pService->getUUID();

        if (serviceUUID == HEARTSERVICE_UUID) {
          this->isHRM                = true;
          spinBLEClient.connectedHRM = true;
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Registered HRM on Connect");
        } else if (serviceUUID == CSCSERVICE_UUID) {
          this->isCSC               = true;
          spinBLEClient.connectedCD = true;
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Registered CSC on Connect");
        } else if (serviceUUID == CYCLINGPOWERSERVICE_UUID || serviceUUID == FITNESSMACHINESERVICE_UUID ||
                   (serviceUUID == FLYWHEEL_UART_SERVICE_UUID && this->uniqueName.find("Flywheel") != std::string::npos) || serviceUUID == ECHELON_DEVICE_UUID ||
                   serviceUUID == PELOTON_DATA_UUID) {
          this->isPM                = true;
          spinBLEClient.connectedPM = true;
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Registered PM on Connect");
        } else if (serviceUUID == HID_SERVICE_UUID) {
          this->isRemote                = true;
          spinBLEClient.connectedRemote = true;
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Registered Remote on Connect");
        }
      }
    } else {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "No pClient in Set()");
    }
  } else {
    // During initial discovery, just store the device info without registering services
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Set %s with no current connection.", adevName.c_str());
  }
}

/**
 * @brief Resets the state of the SpinBLEAdvertisedDevice instance.
 *
 * This method clears the internal state of the device, including connection
 * identifiers, service and characteristic UUIDs, and various flags indicating
 * the type of device and its connection status. Optionally, it can also reset
 * the advertised device reference.
 *
 * @param resetAdvertisedDevice If true, the advertised device reference will
 *                              be set to nullptr.
 */
void SpinBLEAdvertisedDevice::clearState(bool resetAdvertisedDevice) {
  if (resetAdvertisedDevice) {
    advertisedDevice = nullptr;
    peerAddress      = NimBLEAddress();  // zero / cleared
    this->uniqueName.clear();            // Clear the unique name
  }
  connectedClientID  = BLE_HS_CONN_HANDLE_NONE;
  serviceUUID        = (uint16_t)0x0000;
  charUUID           = (uint16_t)0x0000;
  isHRM              = false;
  isPM               = false;
  isCSC              = false;
  isCT               = false;
  isRemote           = false;
  doConnect          = false;
  isPostConnected    = false;
  batt               = Measurement();
  lastDataUpdateTime = 0;  // Reset disconnect detection timestamp
  if (dataBufferQueue) {
    xQueueReset(dataBufferQueue);  // safe to centralize
  }
}

void SpinBLEAdvertisedDevice::reset(bool resetAdvertisedDevice) {
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Resetting Device: %s", this->uniqueName.c_str());

  // Adjust global flags BEFORE clearing local flags.
  if (isHRM) spinBLEClient.connectedHRM = false;
  if (isPM || isCSC) {
    spinBLEClient.connectedPM    = false;
    spinBLEClient.connectedCD    = false;
    spinBLEClient.connectedSpeed = false;
  }

  clearState(resetAdvertisedDevice);
}
