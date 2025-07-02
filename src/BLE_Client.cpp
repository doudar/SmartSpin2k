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
                          21,                /* priority of the task  */
                          &BLEClientTask,   /* Task handle to keep track of created task */
                          1);               /* pin task to core */

  NimBLEScan *pBLEScan = NimBLEDevice::getScan();
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
static void notifyCB(NimBLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify) {
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
bool subscribeToAllNotifications(NimBLEClient *pClient) {
  bool isSubscribed = false;
  if (!pClient || !pClient->isConnected()) {
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Client not connected for notifications");
    return false;
  }
  for (const auto &service : BLEServices::SUPPORTED_SERVICES) {
    NimBLERemoteService *pSvc = pClient->getService(service.serviceUUID);
    if (pSvc) {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Found %s", service.name.c_str());
      for (const auto &pChr : pSvc->getCharacteristics(true)) {
        if (pChr) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Found %s, %s", service.serviceUUID.toString().c_str(), pChr->getUUID().toString().c_str());
          if (pChr->canNotify() || pChr->canIndicate()) {
            if (pChr->canNotify() ? pChr->subscribe(true, notifyCB) : pChr->subscribe(false, notifyCB)) {
              SS2K_LOG(BLE_CLIENT_LOG_TAG, "Subscribed to %s %s handle: %d", service.name.c_str(), pChr->getUUID().toString().c_str(), pChr->getHandle());
              isSubscribed = true;
            } else {
              SS2K_LOG(BLE_CLIENT_LOG_TAG, "Failed to subscribe to %s %s", service.name.c_str(), pChr->getUUID().toString().c_str());
            }
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
void bleClientTask(void *pvParameters) {
  for (;;) {
    delay(BLE_CLIENT_DELAY);  // Delay between loops.

    // disconnect all connected servers if we're updating via BLE
    if (ss2k->isUpdating) {
      for (auto &_BLEd : spinBLEClient.myBLEDevices) {  // loop through discovered devices
        if (_BLEd.connectedClientID != BLE_HS_CONN_HANDLE_NONE) {
          if (_BLEd.advertisedDevice) {                                                                // is device registered?
            if ((_BLEd.connectedClientID != BLE_HS_CONN_HANDLE_NONE) && (_BLEd.doConnect == false)) {  // client must not be in connection process
              if (BLEDevice::getClientByPeerAddress(_BLEd.peerAddress)) {                              // nullptr check
                NimBLEClient *pClient = NimBLEDevice::getClientByPeerAddress(_BLEd.peerAddress);
                pClient->disconnect();
              }
            }
          }
        }
      }
      while (ss2k->isUpdating) {  // wait until the update is done
        delay(100);
      }
    }

    // Post connect previously connected clients. This needs to be before connect, as it takes a while to complete the connection (let it loop once.)
    spinBLEClient.postConnect();

    // Connect BLE Servers to this client
    for (int x = 0; x < NUM_BLE_DEVICES; x++) {
      if (spinBLEClient.myBLEDevices[x].doConnect == true && !ss2k->isUpdating) {
        // stop in process scans
        NimBLEScan *pBLEScan = NimBLEDevice::getScan();
        if (pBLEScan->isScanning()) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Stopping scan before connecting to device on slot %d ...", x);
          pBLEScan->stop();
        }
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Connecting device on slot %d ...", x);
        if (spinBLEClient.connectToServer()) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "We are now connected to the BLE Server.");
        } else {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "slot %d connection failed", x);
        }
      }
    }

    // Scan for BLE devices that we should connect to this client
    static unsigned long scanDelay = millis();
    if ((millis() - scanDelay) > BLE_RECONNECT_SCAN_INTERVAL) {
      spinBLEClient.checkBLEReconnect();
      if (spinBLEClient.doScan && (!ss2k->isUpdating)) {
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

  const NimBLEAdvertisedDevice *myDevice = nullptr;
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

    const BLEServiceInfo *serviceInfo = getDeviceServiceInfo(myDevice, deviceName);
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
    // spinBLEClient.serverScan(true);
    return false;
  }

  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Forming a connection to: %s", this->adevName2UniqueName(myDevice).c_str());

  NimBLEClient *pClient          = nullptr;
  auto handleFailedClientConnect = [&]() {
    SS2K_LOG(BLE_CLIENT_LOG_TAG, " - Failed to connect client");
    /** Created a client but failed to connect, don't need to keep it as it has no data */
    spinBLEClient.myBLEDevices[device_number].reset();
    spinBLEClient.resetDevices(pClient);
    pClient->deleteServices();
    NimBLEDevice::getScan()->erase(pClient->getPeerAddress());
    NimBLEDevice::deleteClient(pClient);
    return false;
  };

  /** Check if we have a client we should reuse first **/
  if (NimBLEDevice::getCreatedClientCount()) {
    /** Special case when we already know this device, we send false as the
     *  second argument in connect() to prevent refreshing the service database.
     *  This saves considerable time and power.
     */
    pClient = NimBLEDevice::getClientByPeerAddress(myDevice->getAddress());
    if (pClient) {
      pClient->setConnectTimeout(10000);
      pClient->setConnectionParams(connectionParams[0], connectionParams[1], connectionParams[2], 1000);
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Reusing Client");
      if (!pClient->connect(myDevice, false, false, true)) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Reconnect failed ");
        this->reconnectTries--;
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "%d left.", reconnectTries);
        if (reconnectTries < 1) {
          handleFailedClientConnect();
        }
        return false;
      }
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Reconnected client");
    }
    /** We don't already have a client that knows this device,
     *  we will check for a client that is disconnected that we can use.
     */
    else {
      pClient = NimBLEDevice::getDisconnectedClient();
    }
  }

  /** No client to reuse? Create a new one. */
  if (!pClient) {
    if (NimBLEDevice::getCreatedClientCount() >= NIMBLE_MAX_CONNECTIONS) {
      Serial.println("Max clients reached - no more connections available");
      return false;
    }

    pClient = NimBLEDevice::createClient();

    SS2K_LOG(BLE_CLIENT_LOG_TAG, " - Created client");

    pClient->setClientCallbacks(&myClientCallback, false);
    /** Set initial connection parameters: These settings are 15ms interval, 0 latency, 120ms timout.
     *  These settings are safe for 3 clients to connect reliably, can go faster if you have less
     *  connections. Timeout should be a multiple of the interval, minimum is 100ms.
     *  Min interval: 12 * 1.25ms = 15, Max interval: 12 * 1.25ms = 15, 0 latency, 51 * 10ms = 510ms timeout
     */
    pClient->setConnectionParams(connectionParams[0], connectionParams[1], connectionParams[2], 1000);
    /** Set how long we are willing to wait for the connection to complete (seconds), default is 30. */
    pClient->setConnectTimeout(5000);  // 5 seconds

    if (!pClient->connect(myDevice)) {
      return handleFailedClientConnect();
    }
  }

  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Connected to: %s - %s RSSI %d", this->adevName2UniqueName(myDevice).c_str(), pClient->getPeerAddress().toString().c_str(), pClient->getRssi());
  if (serviceUUID == HID_SERVICE_UUID) {
    connectBLE_HID(pClient);
    this->reconnectTries = MAX_RECONNECT_TRIES;
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Successful remote subscription.");
    spinBLEClient.myBLEDevices[device_number].doConnect = false;
    this->reconnectTries                                = MAX_RECONNECT_TRIES;
    spinBLEClient.myBLEDevices[device_number].set(myDevice, pClient->getConnHandle(), serviceUUID, charUUID);
    spinBLEClient.myBLEDevices[device_number].peerAddress = pClient->getPeerAddress();
    removeDuplicates(pClient);
    return true;
  }

  /** Now we can read/write/subscribe the characteristics of the services we are interested in */
  NimBLERemoteService *pSvc = nullptr;
  pSvc                      = pClient->getService(serviceUUID);
  if (!pSvc) {
    pClient->getServices(true);
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Refreshing services");
    pSvc = pClient->getService(serviceUUID);
  }
  if (pSvc) { /** make sure it's not null */
    this->reconnectTries                                = MAX_RECONNECT_TRIES;
    spinBLEClient.myBLEDevices[device_number].doConnect = false;
    this->reconnectTries                                = MAX_RECONNECT_TRIES;
    spinBLEClient.myBLEDevices[device_number].set(myDevice, pClient->getConnHandle(), serviceUUID, charUUID);
    spinBLEClient.myBLEDevices[device_number].peerAddress = pClient->getPeerAddress();
    removeDuplicates(pClient);
  } else {
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Failed to find service: %s", serviceUUID.toString().c_str());
    return handleFailedClientConnect();
  }
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Device Connected");
  return true;
}

/**  None of these are required as they will be handled by the library with defaults. **
 **                       Remove as you see fit for your needs                        */

void MyClientCallback::onConnect(NimBLEClient *pClient) { SS2K_LOG(BLE_CLIENT_LOG_TAG, "Connected, %s", pClient->getPeerAddress().toString().c_str()); }

void MyClientCallback::onDisconnect(NimBLEClient *pClient, int reason) {
  if (!pClient->isConnected() && !ss2k->isUpdating) {
    NimBLEAddress addr = pClient->getPeerAddress();
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Client %s Disconnected, reason = %d", addr.toString().c_str(), reason);
    for (size_t i = 0; i < NUM_BLE_DEVICES; i++) {
      if (addr == spinBLEClient.myBLEDevices[i].peerAddress) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Detected %s Disconnect", spinBLEClient.myBLEDevices[i].serviceUUID.toString().c_str());
        if ((spinBLEClient.myBLEDevices[i].charUUID == CYCLINGPOWERMEASUREMENT_UUID) || (spinBLEClient.myBLEDevices[i].charUUID == FITNESSMACHINEINDOORBIKEDATA_UUID) ||
            (spinBLEClient.myBLEDevices[i].charUUID == FLYWHEEL_UART_RX_UUID) || (spinBLEClient.myBLEDevices[i].charUUID == ECHELON_SERVICE_UUID) ||
            (spinBLEClient.myBLEDevices[i].charUUID == CYCLINGPOWERSERVICE_UUID) || (spinBLEClient.myBLEDevices[i].charUUID == CSCSERVICE_UUID)) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Deregistered PM on Disconnect");
          rtConfig->pm_batt.setValue(0);
        }
        if ((spinBLEClient.myBLEDevices[i].charUUID == HEARTCHARACTERISTIC_UUID)) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Deregistered HR on Disconnect");
          rtConfig->hr_batt.setValue(0);
        }
        if ((spinBLEClient.myBLEDevices[i].charUUID == HID_REPORT_DATA_UUID)) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Deregistered Remote on Disconnect");
        }
        // did another task disconnect this device?
        if (!spinBLEClient.intentionalDisconnect) {
          spinBLEClient.myBLEDevices[i].doConnect = true;
          spinBLEClient.myBLEDevices[i].reset(false);
        } else {
          spinBLEClient.intentionalDisconnect--;
          spinBLEClient.myBLEDevices[i].reset();
        }
      }
    }
    return;
  }
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
void ScanCallbacks::onResult(const NimBLEAdvertisedDevice *advertisedDevice) {
  // Defensive check - we've seen null devices causing crashes
  if (!advertisedDevice) {
    SS2K_LOGE(BLE_CLIENT_LOG_TAG, "onResult received NULL advertisedDevice!");
    return;
  }

  Serial.printf("Device found: %s\n", advertisedDevice->haveName() ? advertisedDevice->getName().c_str() : advertisedDevice->getAddress().toString().c_str());
  // Define granular constants for maximal reuse during logging
  const char *const MATCHED               = "Matched ";
  const char *const DIDNT_MATCH_THE_SAVED = " didn't match the saved: ";
  const char *const STRING_MATCHED_ANY    = " String Matched Any";
  const char *const THIS                  = "This ";
  const char *const NAME                  = "Name ";
  const char *const REMOTE                = "Remote";
  const char *const HRM                   = "HRM";
  const char *const PM                    = "PM";
  String aDevName                         = spinBLEClient.adevName2UniqueName(advertisedDevice);
  const char *aDevAddr                    = advertisedDevice->getAddress().toString().c_str();

  if (advertisedDevice->haveServiceUUID() && isDeviceSupported(advertisedDevice, aDevName)) {
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Found Device: %s", aDevName.c_str());

    // Handling for BLE connected remotes
    if (advertisedDevice->getServiceUUID() == HID_SERVICE_UUID) {
      if (strcmp(userConfig->getConnectedRemote(), ANY) == 0) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s%s", REMOTE, STRING_MATCHED_ANY);
      } else {
        bool nameMatched = (aDevName = userConfig->getConnectedRemote()) ? true : false;
        bool addrMatched = strcmp(aDevAddr, userConfig->getConnectedRemote()) == 0;
        if (!nameMatched && !addrMatched || strcmp(userConfig->getConnectedRemote(), NONE) == 0) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s%s%s%s", THIS, REMOTE, DIDNT_MATCH_THE_SAVED, userConfig->getConnectedRemote());
          return;  // Ignore this device;
        } else {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s %s%s%s", REMOTE, NAME, MATCHED, aDevName.c_str());
        }
      }
    } else if (advertisedDevice->getServiceUUID() == HEARTSERVICE_UUID) {
      if (strcmp(userConfig->getConnectedHeartMonitor(), ANY) == 0) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s%s", HRM, STRING_MATCHED_ANY);
      } else {
        bool nameMatched = (aDevName == userConfig->getConnectedHeartMonitor()) ? true : false;
        bool addrMatched = strcmp(aDevAddr, userConfig->getConnectedHeartMonitor()) == 0;
        if (!nameMatched && !addrMatched || strcmp(userConfig->getConnectedHeartMonitor(), NONE) == 0) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s%s%s%s", THIS, HRM, DIDNT_MATCH_THE_SAVED, userConfig->getConnectedHeartMonitor());
          return;  // Ignore this device;
        } else {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s %s%s%s", HRM, NAME, MATCHED, aDevName.c_str());
        }
      }
    } else {
      // Power Meter
      if (strcmp(userConfig->getConnectedPowerMeter(), ANY) == 0) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s%s", PM, STRING_MATCHED_ANY);
      } else {
        bool nameMatched = (aDevName == userConfig->getConnectedPowerMeter()) ? true : false;
        if (!nameMatched || strcmp(userConfig->getConnectedPowerMeter(), NONE) == 0) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s%s%s%s", THIS, PM, DIDNT_MATCH_THE_SAVED, userConfig->getConnectedPowerMeter());
          return;  // Ignore this device;
        } else {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s %s%s%s", PM, NAME, MATCHED, aDevName.c_str());
        }
      }
    }

    for (size_t i = 0; i < NUM_BLE_DEVICES; i++) {
      if ((spinBLEClient.myBLEDevices[i].advertisedDevice == nullptr) || (advertisedDevice->getAddress() == spinBLEClient.myBLEDevices[i].peerAddress)) {
        spinBLEClient.myBLEDevices[i].set(advertisedDevice, BLE_HS_CONN_HANDLE_NONE, advertisedDevice->getServiceUUID());
        spinBLEClient.myBLEDevices[i].doConnect = true;
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "doConnect set on device: %d", i);
        return;
      }
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Checking Slot %d", i);
    }
  }
}

void SpinBLEClient::scanProcess(int duration) {
  this->doScan = false;  // Confirming we did the scan

  NimBLEScan *pBLEScan = NimBLEDevice::getScan();
  if (pBLEScan->isScanning()) {
    return;
  }

  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Scanning for BLE servers and putting them into a list...");
  pBLEScan->start(duration, false, true);
}

void ScanCallbacks::onScanEnd(const NimBLEScanResults &results, int reason) {
  int count = results.getCount();
  JsonDocument devices;

  // Check if 'devices' JSON document already exists and has content; if so, deserialize it.
  const char *foundDevicesJson = userConfig->getFoundDevices();
  if (foundDevicesJson[0] != '\0') {
    deserializeJson(devices, userConfig->getFoundDevices());
  }

  for (int i = 0; i < count; i++) {
    const NimBLEAdvertisedDevice *d = results.getDevice(i);

    // Check for duplicates by name or address before adding
    bool isDuplicate = false;
    for (JsonPair kv : devices.as<JsonObject>()) {
      JsonObject obj = kv.value().as<JsonObject>();
      if (obj["name"] && obj["name"] == spinBLEClient.adevName2UniqueName(d)) {
        isDuplicate = true;
        break;
      }
    }

    if (!isDuplicate && d->haveServiceUUID() && isDeviceSupported(d, spinBLEClient.adevName2UniqueName(d))) {
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
void SpinBLEClient::removeDuplicates(NimBLEClient *pClient) {
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
            oldBLEd.reset();
            spinBLEClient.intentionalDisconnect++;
            return;
          }
        }
      }
    }
  }
}

void SpinBLEClient::resetDevices(NimBLEClient *pClient) {
  for (auto &_BLEd : spinBLEClient.myBLEDevices) {
    if (pClient->getPeerAddress() == _BLEd.peerAddress) {
      SS2K_LOGW(BLE_CLIENT_LOG_TAG, "Reset Client: %s", _BLEd.peerAddress.toString().c_str());
      _BLEd.reset();
    }
  }
}

// Control a connected FTMS trainer. If no args are passed, treat it like an external stepper motor.
void SpinBLEClient::FTMSControlPointWrite(const uint8_t *pData, int length) {
  if (userConfig->getFTMSControlPointWrite()) {
    NimBLEClient *pClient = nullptr;
    uint8_t modData[7];
    for (int i = 0; i < length; i++) {
      modData[i] = pData[i];
    }
    for (int i = 0; i < NUM_BLE_DEVICES; i++) {
      if (myBLEDevices[i].getPostConnected() && (myBLEDevices[i].serviceUUID == FITNESSMACHINESERVICE_UUID)) {
        if (NimBLEDevice::getClientByPeerAddress(myBLEDevices[i].peerAddress)->getService(FITNESSMACHINESERVICE_UUID)) {
          pClient = NimBLEDevice::getClientByPeerAddress(myBLEDevices[i].peerAddress);
          break;
        }
      }
    }
    if (pClient) {
      NimBLERemoteCharacteristic *writeCharacteristic = pClient->getService(FITNESSMACHINESERVICE_UUID)->getCharacteristic(FITNESSMACHINECONTROLPOINT_UUID);
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
  for (auto &_BLEd : spinBLEClient.myBLEDevices) {
    // Check that the device has been assigned and it hasn't been post connected.
    if ((_BLEd.connectedClientID != BLE_HS_CONN_HANDLE_NONE) && !_BLEd.getPostConnected()) {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Post connecting: %s , ConnID %d, PrimaryChar %s", _BLEd.peerAddress.toString().c_str(), _BLEd.connectedClientID,
               _BLEd.charUUID.toString().c_str());
      if (NimBLEDevice::getClientByPeerAddress(_BLEd.peerAddress)) {
        NimBLEClient *pClient = NimBLEDevice::getClientByPeerAddress(_BLEd.peerAddress);
        BLEDevice::getServer()->updateConnParams(pClient->getConnHandle(), connectionParams[0], connectionParams[1], connectionParams[2], connectionParams[3]);
        _BLEd.setPostConnected(subscribeToAllNotifications(pClient));
        if (!_BLEd.getPostConnected()) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Failed to subscribe to notifications for %s", _BLEd.peerAddress.toString().c_str());
          return;
        }
        if (_BLEd.charUUID == ECHELON_DATA_UUID) {
          NimBLERemoteCharacteristic *writeCharacteristic = pClient->getService(ECHELON_SERVICE_UUID)->getCharacteristic(ECHELON_WRITE_UUID);
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
            auto features = *reinterpret_cast<const uint32_t *>(value.data());
            if (!(features & FitnessMachineFeatureFlags::Types::ElapsedTimeSupported) || !(features & FitnessMachineFeatureFlags::Types::RemainingTimeSupported)) {
              SS2K_LOG(BLE_CLIENT_LOG_TAG, "FTMS Control Point StartOrResume not supported");
              return;
            }
          }

          NimBLERemoteCharacteristic *writeCharacteristic = pClient->getService(FITNESSMACHINESERVICE_UUID)->getCharacteristic(FITNESSMACHINECONTROLPOINT_UUID);
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

bool SpinBLEAdvertisedDevice::enqueueData(uint8_t *data, size_t length, NimBLEUUID serviceUUID, NimBLEUUID charUUID) {
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

void SpinBLEClient::connectBLE_HID(NimBLEClient *pClient) {
  NimBLERemoteService *pSvc = nullptr;
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
    std::vector<NimBLERemoteCharacteristic *> charVector = pSvc->getCharacteristics(true);
    for (auto &it : charVector) {
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

void SpinBLEClient::keepAliveBLE_HID(NimBLEClient *pClient) {
  static int intervalTimer = millis();
  if ((millis() - intervalTimer) < 6000) {
    return;
  }
  NimBLERemoteService *pSvc        = nullptr;
  NimBLERemoteCharacteristic *pChr = nullptr;
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

  if ((strcmp(userConfig->getConnectedHeartMonitor(), NONE) != 0) && !spinBLEClient.connectedHRM) {
    this->doScan = true;
    offset += snprintf(notConnectedDevices + offset, sizeof(notConnectedDevices) - offset, "HRM ");
  }
  if ((strcmp(userConfig->getConnectedPowerMeter(), NONE) != 0) && !(spinBLEClient.connectedPM || spinBLEClient.connectedCD)) {
    this->doScan = true;
    offset += snprintf(notConnectedDevices + offset, sizeof(notConnectedDevices) - offset, "PM ");
  }
  if ((strcmp(userConfig->getConnectedRemote(), NONE) != 0) && !(spinBLEClient.connectedRemote)) {
    this->doScan = true;
    offset += snprintf(notConnectedDevices + offset, sizeof(notConnectedDevices) - offset, "Remote ");
  }
  if (offset > 0) {
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Devices not connected: %s", notConnectedDevices);
  }
}

void SpinBLEClient::reconnectAllDevices() {
  for (auto i : spinBLEClient.myBLEDevices) {
    if (NimBLEDevice::getClientByPeerAddress(i.peerAddress)) {
      if (NimBLEDevice::getClientByPeerAddress(i.peerAddress)->isConnected()) {
        NimBLEDevice::getClientByPeerAddress(i.peerAddress)->disconnect();
        i.reset();
        spinBLEClient.intentionalDisconnect++;
      }
    }
  }
}

// Poll BLE devices for battCharacteristic if available and read value.
void SpinBLEClient::handleBattInfo(NimBLEClient *pClient, bool updateNow = false) {
  static unsigned long last_battery_update = 0;
  if ((millis() - last_battery_update >= BATTERY_UPDATE_INTERVAL_MILLIS) || (last_battery_update == 0) || updateNow) {
    last_battery_update = millis();
    if (pClient->getService(BATTERYSERVICE_UUID) == nullptr) {
      return;
    }
    if (pClient->getService(BATTERYSERVICE_UUID)->getCharacteristic(BATTERYCHARACTERISTIC_UUID) == nullptr) {
      return;
    }
    if (pClient->getService(HEARTSERVICE_UUID) && pClient->getService(BATTERYSERVICE_UUID)) {  // get battery level at first connect
      BLERemoteCharacteristic *battCharacteristic = pClient->getService(BATTERYSERVICE_UUID)->getCharacteristic(BATTERYCHARACTERISTIC_UUID);
      if (battCharacteristic != nullptr) {
        std::string value = battCharacteristic->readValue();
        rtConfig->hr_batt.setValue((uint8_t)value[0]);
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "HRM battery updated %d", (int)value[0]);
      } else {
        rtConfig->hr_batt.setValue(0);
      }
    } else if ((pClient->getService(CYCLINGPOWERMEASUREMENT_UUID) || pClient->getService(CYCLINGPOWERSERVICE_UUID)) &&
               pClient->getService(BATTERYSERVICE_UUID)) {  // get batterylevel at first connect
      BLERemoteCharacteristic *battCharacteristic = pClient->getService(BATTERYSERVICE_UUID)->getCharacteristic(BATTERYCHARACTERISTIC_UUID);
      if (battCharacteristic != nullptr) {
        std::string value = battCharacteristic->readValue();
        rtConfig->pm_batt.setValue((uint8_t)value[0]);
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "PM battery updated %d", (int)value[0]);
      } else {
        rtConfig->pm_batt.setValue(0);
      }
    }
  }
}
// Returns a device name with the las two of the peer address attached. This lets us distinguish between multiple devices with the same device name.
String SpinBLEClient::adevName2UniqueName(const NimBLEAdvertisedDevice *inDev) {
  if (inDev->haveName()) {
    String _outDevName = String(inDev->getName().c_str());
    // add the last two of the string
    _outDevName += +" " + String(inDev->getAddress().toString().c_str()).substring(inDev->getAddress().toString().length() - 2);
    return _outDevName;
  } else {
    String _outDevName = inDev->getAddress().toString().c_str();
    return _outDevName;
  }
}

void SpinBLEAdvertisedDevice::set(const NimBLEAdvertisedDevice *device, int id, BLEUUID inServiceUUID, BLEUUID inCharUUID) {
  // Defensive null check to prevent crashes
  if (!device) {
    SS2K_LOGE(BLE_CLIENT_LOG_TAG, "ERROR: Attempt to set null device!");
    return;
  }

  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Setting Device %s", device->getAddress().toString().c_str());
  this->advertisedDevice  = const_cast<const NimBLEAdvertisedDevice *>(device);
  this->peerAddress       = device->getAddress();
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
    NimBLEClient *pClient = NimBLEDevice::getClientByPeerAddress(device->getAddress());
    if (pClient) {
      // Get all services
      const std::vector<NimBLERemoteService *> &services = pClient->getServices(true);
      for (auto &pService : services) {
        BLEUUID serviceUUID = pService->getUUID();

        if (serviceUUID == HEARTSERVICE_UUID) {
          this->isHRM                = true;
          spinBLEClient.connectedHRM = true;
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Registered HRM on Connect");
        } else if (serviceUUID == CSCSERVICE_UUID) {
          this->isCSC               = true;
          spinBLEClient.connectedCD = true;
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Registered CSC on Connect");
        } else if (serviceUUID == CYCLINGPOWERSERVICE_UUID || serviceUUID == FITNESSMACHINESERVICE_UUID || serviceUUID == FLYWHEEL_UART_SERVICE_UUID ||
                   serviceUUID == ECHELON_SERVICE_UUID || serviceUUID == PELOTON_DATA_UUID) {
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
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Set %s with no current connection.", device->getAddress().toString().c_str());
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
void SpinBLEAdvertisedDevice::reset(bool resetAdvertisedDevice) {
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Resetting Device: %d", this->connectedClientID);
  if (this->isHRM) spinBLEClient.connectedHRM = false;
  if (this->isPM) spinBLEClient.connectedPM = false;
  if (this->isCSC) spinBLEClient.connectedCD = false;
  spinBLEClient.connectedSpeed = false;
  if (resetAdvertisedDevice) advertisedDevice = nullptr;
  // NimBLEAddress peerAddress;
  this->connectedClientID = BLE_HS_CONN_HANDLE_NONE;
  this->serviceUUID       = (uint16_t)0x0000;
  this->charUUID          = (uint16_t)0x0000;
  this->isHRM             = false;  // Heart Rate Monitor
  this->isPM              = false;  // Power Meter
  this->isCSC             = false;  // Cycling Speed/Cadence
  this->isCT              = false;  // Controllable Trainer
  this->isRemote          = false;  // BLE Remote
  this->doConnect         = false;  // Initiate connection flag
  this->isPostConnected   = false;  // Has Post Connect Been Run?
  if (this->dataBufferQueue != nullptr) {
    // Serial.println("Resetting queue");
    xQueueReset(this->dataBufferQueue);
  }
}
