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
#include "SS2KLog.h"

#include <ArduinoJson.h>
#include <Constants.h>
#include <memory>
#include <NimBLEDevice.h>

TaskHandle_t BLEClientTask;

SpinBLEClient spinBLEClient;

static MyClientCallback myClientCallback;
static MyAdvertisedDeviceCallback myAdvertisedDeviceCallbacks;

void SpinBLEClient::start() {
  // Create the task for the BLE Client loop
  xTaskCreatePinnedToCore(bleClientTask,    /* Task function. */
                          "BLEClientTask",  /* name of task. */
                          BLE_CLIENT_STACK, /* Stack size of task */
                          NULL,             /* parameter of the task */
                          1,                /* priority of the task  */
                          &BLEClientTask,   /* Task handle to keep track of created task */
                          1);               /* pin task to core */
}

static void onNotify(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify) {
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

  // enqueue sensor data
  for (size_t i = 0; i < NUM_BLE_DEVICES; i++) {
    if (pBLERemoteCharacteristic->getUUID() == spinBLEClient.myBLEDevices[i].charUUID) {
      spinBLEClient.myBLEDevices[i].enqueueData(pData, length);
    }
  }
}

// BLE Client loop task
void bleClientTask(void *pvParameters) {
  long int scanDelay = millis();
  for (;;) {
    vTaskDelay(BLE_CLIENT_DELAY / portTICK_PERIOD_MS);  // Delay between loops.

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
    }

    // Scan for BLE devices that we should connect to this client
    if ((millis() - scanDelay) > ((BLE_RECONNECT_SCAN_DURATION * 1000) * 2)) {
      spinBLEClient.checkBLEReconnect();
      scanDelay = millis();
    }

    if (spinBLEClient.doScan && (!ss2k->isUpdating)) {
      spinBLEClient.scanProcess();
    }

// Connect BLE Servers to this client
#ifdef DEBUG_STACK
    Serial.printf("BLEClient: %d \n", uxTaskGetStackHighWaterMark(BLEClientTask));
#endif  // DEBUG_STACK
    for (int x = 0; x < NUM_BLE_DEVICES; x++) {
      if (spinBLEClient.myBLEDevices[x].doConnect == true && !ss2k->isUpdating) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Connecting device on slot %d ...", x);
        if (spinBLEClient.connectToServer()) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "We are now connected to the BLE Server.");
        } else {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "slot %d connection failed", x);
        }
      }
    }
  }
}

bool SpinBLEClient::connectToServer() {
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Initiating Server Connection");
  NimBLEUUID serviceUUID;
  NimBLEUUID charUUID;

  int successful                = 0;
  BLEAdvertisedDevice *myDevice = nullptr;
  int device_number             = -1;

  for (int i = 0; i < NUM_BLE_DEVICES; i++) {
    if (spinBLEClient.myBLEDevices[i].doConnect == true) {   // Client wants to be connected
      if (spinBLEClient.myBLEDevices[i].advertisedDevice) {  // Client is assigned
        // If this device is advertising HR service AND not advertising FTMS service AND there is no connected PM AND the next slot is set to connect, connect that one first and
        // connect the HRM last.
        // if (spinBLEClient.myBLEDevices[i].advertisedDevice->isAdvertisingService(HEARTSERVICE_UUID) &&
        //     (!spinBLEClient.myBLEDevices[i].advertisedDevice->isAdvertisingService(FITNESSMACHINESERVICE_UUID)) && (!connectedPM) &&
        //     (spinBLEClient.myBLEDevices[i + 1].doConnect == true)) {
        //   myDevice      = spinBLEClient.myBLEDevices[i + 1].advertisedDevice;
        //   device_number = i + 1;
        //   SS2K_LOG(BLE_CLIENT_LOG_TAG, "Connecting HRM last.");
        // } else {
        myDevice      = spinBLEClient.myBLEDevices[i].advertisedDevice;
        device_number = i;
        //   }
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
  // FUTURE - Iterate through an array of UUID's we support instead of all the if checks.
  if (myDevice->getServiceUUIDCount() > 0) {
    if (myDevice->isAdvertisingService(FLYWHEEL_UART_SERVICE_UUID) && (myDevice->getName() == FLYWHEEL_BLE_NAME)) {
      serviceUUID = FLYWHEEL_UART_SERVICE_UUID;
      charUUID    = FLYWHEEL_UART_TX_UUID;
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "trying to connect to Flywheel Bike");
    } else if (myDevice->isAdvertisingService(FITNESSMACHINESERVICE_UUID)) {
      serviceUUID = FITNESSMACHINESERVICE_UUID;
      charUUID    = FITNESSMACHINEINDOORBIKEDATA_UUID;
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "trying to connect to Fitness Machine Service");
    } else if (myDevice->isAdvertisingService(CYCLINGPOWERSERVICE_UUID)) {
      serviceUUID = CYCLINGPOWERSERVICE_UUID;
      charUUID    = CYCLINGPOWERMEASUREMENT_UUID;
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "trying to connect to Cycling Power Service");
    } else if (myDevice->isAdvertisingService(ECHELON_DEVICE_UUID)) {
      serviceUUID = ECHELON_SERVICE_UUID;
      charUUID    = ECHELON_DATA_UUID;
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Trying to connect to Echelon Bike");
    } else if (myDevice->isAdvertisingService(HEARTSERVICE_UUID)) {
      serviceUUID = HEARTSERVICE_UUID;
      charUUID    = HEARTCHARACTERISTIC_UUID;
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Trying to connect to HRM");
    } else if (myDevice->isAdvertisingService(HID_SERVICE_UUID)) {
      serviceUUID = HID_SERVICE_UUID;
      charUUID    = HID_REPORT_DATA_UUID;
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Trying to connect to BLE HID remote");
    } else {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "No advertised UUID found");
      spinBLEClient.myBLEDevices[device_number].reset();
      return false;
    }
  } else {
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Device has no Service UUID");
    spinBLEClient.myBLEDevices[device_number].reset();
    // spinBLEClient.serverScan(true);
    return false;
  }

  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Forming a connection to: %s", this->adevName2UniqueName(myDevice).c_str());

  NimBLEClient *pClient = nullptr;

  /** Check if we have a client we should reuse first **/
  if (NimBLEDevice::getClientListSize()) {
    /** Special case when we already know this device, we send false as the
     *  second argument in connect() to prevent refreshing the service database.
     *  This saves considerable time and power.
     */
    pClient = NimBLEDevice::getClientByPeerAddress(myDevice->getAddress());
    if (pClient) {
      pClient->setConnectTimeout(2);
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Reusing Client");
      if (!pClient->connect(myDevice, false)) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Reconnect failed ");
        this->reconnectTries--;
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "%d left.", reconnectTries);
        if (reconnectTries < 1) {
          spinBLEClient.myBLEDevices[device_number].reset();
          spinBLEClient.resetDevices(pClient);
          pClient->deleteServices();
          NimBLEDevice::getScan()->erase(pClient->getPeerAddress());
          NimBLEDevice::deleteClient(pClient);
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
    if (NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
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
    pClient->setConnectionParams(6, 6, 0, 200);
    /** Set how long we are willing to wait for the connection to complete (seconds), default is 30. */
    pClient->setConnectTimeout(5);  // 5

    if (!pClient->connect(myDevice->getAddress())) {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, " - Failed to connect client");
      /** Created a client but failed to connect, don't need to keep it as it has no data */
      spinBLEClient.myBLEDevices[device_number].reset();
      pClient->deleteServices();
      NimBLEDevice::getScan()->erase(pClient->getPeerAddress());
      NimBLEDevice::deleteClient(pClient);
      return false;
    }
  }

  if (!pClient->isConnected()) {
    if (!pClient->connect(myDevice)) {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Failed to connect");
      return false;
    }
  }

  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Connected to: %s - %s RSSI %d", this->adevName2UniqueName(myDevice).c_str(), pClient->getPeerAddress().toString().c_str(), pClient->getRssi());

  if (serviceUUID == HID_SERVICE_UUID) {
    connectBLE_HID(pClient);
    this->reconnectTries = MAX_RECONNECT_TRIES;
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Successful remote subscription.");
    spinBLEClient.myBLEDevices[device_number].doConnect = false;
    this->reconnectTries                                = MAX_RECONNECT_TRIES;
    spinBLEClient.myBLEDevices[device_number].set(myDevice, pClient->getConnId(), serviceUUID, charUUID);
    spinBLEClient.myBLEDevices[device_number].peerAddress = pClient->getPeerAddress();
    removeDuplicates(pClient);
    return true;
  }

  /** Now we can read/write/subscribe the characteristics of the services we are interested in */
  NimBLERemoteService *pSvc        = nullptr;
  NimBLERemoteCharacteristic *pChr = nullptr;
  NimBLERemoteDescriptor *pDsc     = nullptr;

  pSvc = pClient->getService(serviceUUID);
  if (pSvc) { /** make sure it's not null */
    pChr = pSvc->getCharacteristic(charUUID);

    if (pChr) { /** make sure it's not null */
      if (pChr->canRead()) {
        NimBLEAttValue value       = pChr->readValue();
        const int kLogBufMaxLength = 250;
        char logBuf[kLogBufMaxLength];
        int logBufLength = ss2k_log_hex_to_buffer(value.data(), value.length(), logBuf, 0, kLogBufMaxLength);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufMaxLength - logBufLength, " <-initial Value");
        SS2K_LOG(BLE_COMMON_LOG_TAG, "%s", logBuf);
      }
      if (pChr->canNotify()) {
        if (!pChr->subscribe(true, onNotify)) {
          /** Disconnect if subscribe failed */
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Notifications Failed for %s", pClient->getPeerAddress().toString().c_str());
          spinBLEClient.myBLEDevices[device_number].reset();
          pClient->deleteServices();
          NimBLEDevice::getScan()->erase(pClient->getPeerAddress());
          NimBLEDevice::deleteClient(pClient);
          return false;
        }
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Notifications Subscribed for %s", pClient->getPeerAddress().toString().c_str());
      } else if (pChr->canIndicate()) {
        /** Send false as first argument to subscribe to indications instead of notifications */
        if (!pChr->subscribe(false, onNotify)) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Indications Failed for %s", pClient->getPeerAddress().toString().c_str());
          /** Disconnect if subscribe failed */
          spinBLEClient.myBLEDevices[device_number].reset();
          pClient->deleteServices();
          NimBLEDevice::getScan()->erase(pClient->getPeerAddress());
          NimBLEDevice::deleteClient(pClient);
          return false;
        }
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Indications Subscribed for %s", pClient->getPeerAddress().toString().c_str());
      }
      this->reconnectTries = MAX_RECONNECT_TRIES;
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Successful %s subscription.", pChr->getUUID().toString().c_str());
      spinBLEClient.myBLEDevices[device_number].doConnect = false;
      this->reconnectTries                                = MAX_RECONNECT_TRIES;
      spinBLEClient.myBLEDevices[device_number].set(myDevice, pClient->getConnId(), serviceUUID, charUUID);
      spinBLEClient.myBLEDevices[device_number].peerAddress = pClient->getPeerAddress();
      removeDuplicates(pClient);
    }

  } else {
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Failed to find service: %s", serviceUUID.toString().c_str());
  }

  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Device Connected");
  return true;
}

/**  None of these are required as they will be handled by the library with defaults. **
 **                       Remove as you see fit for your needs                        */

void MyClientCallback::onConnect(NimBLEClient *pClient) {
  // additional characteristic subscriptions.
}

void MyClientCallback::onDisconnect(NimBLEClient *pClient) {
  if (!pClient->isConnected()) {
    NimBLEAddress addr = pClient->getPeerAddress();
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "This disconnected client Address %s", addr.toString().c_str());
    for (size_t i = 0; i < NUM_BLE_DEVICES; i++) {
      if (addr == spinBLEClient.myBLEDevices[i].peerAddress) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Detected %s Disconnect", spinBLEClient.myBLEDevices[i].serviceUUID.toString().c_str());
        // did another task disconnect this device?
        if (!spinBLEClient.intentionalDisconnect) {
          spinBLEClient.myBLEDevices[i].doConnect = true;
        } else {
          spinBLEClient.intentionalDisconnect--;
        }
        if ((spinBLEClient.myBLEDevices[i].charUUID == CYCLINGPOWERMEASUREMENT_UUID) || (spinBLEClient.myBLEDevices[i].charUUID == FITNESSMACHINEINDOORBIKEDATA_UUID) ||
            (spinBLEClient.myBLEDevices[i].charUUID == FLYWHEEL_UART_RX_UUID) || (spinBLEClient.myBLEDevices[i].charUUID == ECHELON_SERVICE_UUID) ||
            (spinBLEClient.myBLEDevices[i].charUUID == CYCLINGPOWERSERVICE_UUID)) {
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
        spinBLEClient.myBLEDevices[i].reset();
      }
    }
    return;
  }
}

/***************** New - Security handled here ********************
****** Note: these are the same return values as defaults ********/
uint32_t MyClientCallback::onPassKeyRequest() {
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Client PassKeyRequest");
  return 123456;
}
bool MyClientCallback::onConfirmPIN(uint32_t pass_key) {
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "The passkey YES/NO number: %ud", pass_key);
  return true;
}

void MyClientCallback::onAuthenticationComplete(ble_gap_conn_desc desc) { SS2K_LOG(BLE_CLIENT_LOG_TAG, "Starting BLE work!"); }
/*******************************************************************/

/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */

void MyAdvertisedDeviceCallback::onResult(BLEAdvertisedDevice *advertisedDevice) {
  // Define granular constants for maximal reuse during logging
  const char *const MATCHED               = "Matched ";
  const char *const DIDNT_MATCH_THE_SAVED = " didn't match the saved: ";
  const char *const STRING_MATCHED_ANY    = " String Matched Any";
  const char *const THIS                  = "This ";
  const char *const NAME                  = "Name ";
  const char *const ADDRESS               = "Address ";
  const char *const REMOTE                = "Remote";
  const char *const HRM                   = "HRM";
  const char *const PM                    = "PM";
  String aDevName                         = spinBLEClient.adevName2UniqueName(advertisedDevice);
  const char *aDevAddr                    = advertisedDevice->getAddress().toString().c_str();

  if ((advertisedDevice->haveServiceUUID()) && (advertisedDevice->isAdvertisingService(CYCLINGPOWERSERVICE_UUID) ||
                                                (advertisedDevice->isAdvertisingService(FLYWHEEL_UART_SERVICE_UUID) && aDevName == String(FLYWHEEL_BLE_NAME)) ||
                                                advertisedDevice->isAdvertisingService(FITNESSMACHINESERVICE_UUID) || advertisedDevice->isAdvertisingService(HEARTSERVICE_UUID) ||
                                                advertisedDevice->isAdvertisingService(ECHELON_DEVICE_UUID) || advertisedDevice->isAdvertisingService(HID_SERVICE_UUID))) {
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Trying to match found device name: %s", aDevName.c_str());

    // Handling for BLE connected remotes
    if (advertisedDevice->getServiceUUID() == HID_SERVICE_UUID) {
      if (strcmp(userConfig->getConnectedRemote(), "any") == 0) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s%s", REMOTE, STRING_MATCHED_ANY);
      } else {
        bool nameMatched = (aDevName = userConfig->getConnectedRemote()) ? true : false;
        bool addrMatched = strcmp(aDevAddr, userConfig->getConnectedRemote()) == 0;
        if (!nameMatched && !addrMatched || strcmp(userConfig->getConnectedRemote(), "none") == 0) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s%s%s%s", THIS, REMOTE, DIDNT_MATCH_THE_SAVED, userConfig->getConnectedRemote());
          return;  // Ignore this device;
        } else {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s %s%s", REMOTE, NAME, MATCHED, aDevName);
        }
      }
    } else if (advertisedDevice->getServiceUUID() == HEARTSERVICE_UUID) {
      if (strcmp(userConfig->getConnectedHeartMonitor(), "any") == 0) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s%s", HRM, STRING_MATCHED_ANY);
      } else {
        bool nameMatched = (aDevName == userConfig->getConnectedHeartMonitor()) ? true : false;
        bool addrMatched = strcmp(aDevAddr, userConfig->getConnectedHeartMonitor()) == 0;
        if (!nameMatched && !addrMatched || strcmp(userConfig->getConnectedHeartMonitor(), "none") == 0) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s%s%s%s", THIS, HRM, DIDNT_MATCH_THE_SAVED, userConfig->getConnectedHeartMonitor());
          return;  // Ignore this device;
        } else {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s %s%s", HRM, NAME, MATCHED, aDevName);
        }
      }
    } else {
      // Power Meter
      if (strcmp(userConfig->getConnectedPowerMeter(), "any") == 0) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s%s", PM, STRING_MATCHED_ANY);
      } else {
        bool nameMatched = (aDevName == userConfig->getConnectedPowerMeter()) ? true : false;
        if (!nameMatched || strcmp(userConfig->getConnectedPowerMeter(), "none") == 0) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s%s%s%s", THIS, PM, DIDNT_MATCH_THE_SAVED, userConfig->getConnectedPowerMeter());
          return;  // Ignore this device;
        } else {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s %s%s", PM, NAME, MATCHED, aDevName);
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

  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Scanning for BLE servers and putting them into a list...");

  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->clearDuplicateCache();
  pBLEScan->clearResults();
  pBLEScan->setAdvertisedDeviceCallbacks(&myAdvertisedDeviceCallbacks);
  pBLEScan->setInterval(49);  // 97
  pBLEScan->setWindow(33);    // 67
  pBLEScan->setDuplicateFilter(true);
  pBLEScan->setActiveScan(true);  // might cause memory leak if true - undetermined. We don't get device names without it.
  BLEScanResults foundDevices = pBLEScan->start(duration, false);
  this->dontBlockScan         = false;

  int count = foundDevices.getCount();

  StaticJsonDocument<1000> devices;

  // Check if 'devices' JSON document already exists and has content; if so, deserialize it.
  const char *foundDevicesJson = userConfig->getFoundDevices();
  if (foundDevicesJson[0] != '\0') {
    deserializeJson(devices, userConfig->getFoundDevices());
  }

  for (int i = 0; i < count; i++) {
    BLEAdvertisedDevice d = foundDevices.getDevice(i);

    // Check for duplicates by name or address before adding
    bool isDuplicate = false;
    for (JsonPair kv : devices.as<JsonObject>()) {
      JsonObject obj = kv.value().as<JsonObject>();
      if (obj.containsKey("name") && obj["name"] == this->adevName2UniqueName(&d)) {
        isDuplicate = true;
        break;
      }
    }

    // is this device advertising something we're interested in?
    if (!isDuplicate && (d.isAdvertisingService(CYCLINGPOWERSERVICE_UUID) || d.isAdvertisingService(HEARTSERVICE_UUID) || d.isAdvertisingService(FLYWHEEL_UART_SERVICE_UUID) ||
                         d.isAdvertisingService(FITNESSMACHINESERVICE_UUID) || d.isAdvertisingService(ECHELON_DEVICE_UUID) || d.isAdvertisingService(HID_SERVICE_UUID))) {
      String device = "device " + String(devices.size());  // Use the current size to index the new device

      devices[device]["name"] = this->adevName2UniqueName(&d);

      // Workaround for IC4 not advertising FTMS as the first service.
      // Potentially others may need to be added in the future.
      // The symptom was the bike name not showing up in the HTML.
      if (d.haveServiceUUID() && d.isAdvertisingService(FITNESSMACHINESERVICE_UUID)) {
        devices[device]["UUID"] = FITNESSMACHINESERVICE_UUID.toString();
      } else {
        devices[device]["UUID"] = d.getServiceUUID().toString();
      }
    }
  }

  String output;
  serializeJson(devices, output);
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Bluetooth Client Found Devices: %s", output.c_str());
#ifdef USE_TELEGRAM
  SEND_TO_TELEGRAM("Bluetooth Client Found Devices: " + output);
#endif
  userConfig->setFoundDevices(output);  // Save the updated JSON document

  pBLEScan = nullptr;  // free up memory
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
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s Matched another service.  Disconnecting: %s", tBLEd.peerAddress.toString().c_str(), oldBLEd.peerAddress.toString().c_str());
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
          int incline = ss2k->targetPosition / userConfig->getInclineMultiplier();
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
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Post connecting: %s , ConnID %d", _BLEd.peerAddress.toString().c_str(), _BLEd.connectedClientID);
      if (NimBLEDevice::getClientByPeerAddress(_BLEd.peerAddress)) {
        _BLEd.setPostConnected(true);
        NimBLEClient *pClient = NimBLEDevice::getClientByPeerAddress(_BLEd.peerAddress);
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
          NimBLERemoteCharacteristic *writeCharacteristic = pClient->getService(FITNESSMACHINESERVICE_UUID)->getCharacteristic(FITNESSMACHINECONTROLPOINT_UUID);
          if (writeCharacteristic == nullptr) {
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "Failed to find FTMS control characteristic UUID: %s", FITNESSMACHINECONTROLPOINT_UUID.toString().c_str());
            return;
          }

          // If we would like to control an external FTMS trainer. With most spin bikes we would want this off, but it's useful if you want to use the SmartSpin2k as an appliance.
          if (userConfig->getFTMSControlPointWrite()) {
            writeCharacteristic->writeValue(FitnessMachineControlPointProcedure::RequestControl, 1);
            vTaskDelay(BLE_NOTIFY_DELAY / portTICK_PERIOD_MS);
            SS2K_LOG(BLE_CLIENT_LOG_TAG, "Activated FTMS Training.");
          }
          writeCharacteristic->writeValue(FitnessMachineControlPointProcedure::StartOrResume, 1);
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Updating Connection Params for: %s", _BLEd.peerAddress.toString().c_str());
          BLEDevice::getServer()->updateConnParams(pClient->getConnId(), 120, 120, 2, 1000);
          spinBLEClient.handleBattInfo(pClient, true);
        }
      }
    }
  }
}

bool SpinBLEAdvertisedDevice::enqueueData(uint8_t *data, size_t length) {
  NotifyData notifyData;

  if (!uxQueueSpacesAvailable(this->dataBufferQueue)) {
    // Serial.println("No space available in queue. Skipping enqueue of data.");
    return pdFALSE;
  }

  notifyData.length = length;
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
  if (this->dataBufferQueue == nullptr) {
    //  Serial.println("Queue not created.  Skipping dequeue of data.");
    receivedNotifyData.length = 0;
    return receivedNotifyData;
  }
  if (xQueueReceive(this->dataBufferQueue, &receivedNotifyData, 0) == pdTRUE) {
    //  Serial.printf("Successfully dequeued data from queue. %d \n", receivedNotifyData.length);
    return receivedNotifyData;
  }
  // Serial.println("buffer was empty");
  receivedNotifyData.length = 0;
  return receivedNotifyData;
}

// Was changed in notify-Buffer - - may not be needed **********************************************************************
void SpinBLEAdvertisedDevice::print() {
  char logBuf[250];
  char *logBufP = logBuf;
  logBufP += sprintf(logBufP, "Address: (%s)", peerAddress.toString().c_str());
  logBufP += sprintf(logBufP, " Client ID: (%d)", connectedClientID);
  logBufP += sprintf(logBufP, " SerUUID: (%s)", serviceUUID.toString().c_str());
  logBufP += sprintf(logBufP, " CharUUID: (%s)", charUUID.toString().c_str());
  logBufP += sprintf(logBufP, " HRM: (%s)", isHRM ? "true" : "false");
  logBufP += sprintf(logBufP, " PM: (%s)", isPM ? "true" : "false");
  logBufP += sprintf(logBufP, " CSC: (%s)", isCSC ? "true" : "false");
  logBufP += sprintf(logBufP, " CT: (%s)", isCT ? "true" : "false");
  logBufP += sprintf(logBufP, " doConnect: (%s)", doConnect ? "true" : "false");
  strcat(logBufP, "|");
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s", String(logBuf));
}

void SpinBLEClient::connectBLE_HID(NimBLEClient *pClient) {
  NimBLERemoteService *pSvc        = nullptr;
  NimBLERemoteCharacteristic *pChr = nullptr;
  pSvc                             = pClient->getService(HID_SERVICE_UUID);
  if (pSvc) { /** make sure it's not null */
    // This returns the HID report descriptor like this
    // HID_REPORT_MAP 0x2a4b Value: 5,1,9,2,A1,1,9,1,A1,0,5,9,19,1,29,5,15,0,25,1,75,1,
    // Copy and paste the value digits to http://eleccelerator.com/usbdescreqparser/
    // to see the decoded report descriptor.
    /*pChr = pSvc->getCharacteristic(HID_REPORT_MAP_UUID);
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
    std::vector<NimBLERemoteCharacteristic *> *charVector;
    charVector = pSvc->getCharacteristics(true);
    for (auto &it : *charVector) {
      if (it->getUUID() == NimBLEUUID(HID_REPORT_DATA_UUID)) {
        Serial.println(it->toString().c_str());
        if (it->canNotify()) {
          if (!it->subscribe(true, onNotify)) {
            /** Disconnect if subscribe failed */
            Serial.println("subscribe notification failed");
            NimBLEDevice::deleteClient(pClient);
            return;  // false;
          } else {
            Serial.println("subscribed");
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
  if ((String(userConfig->getConnectedHeartMonitor()) != "none") && !(spinBLEClient.connectedHRM)) {
    this->doScan = true;
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "No HRM Connected");
  }
  if ((String(userConfig->getConnectedPowerMeter()) != "none") && !(spinBLEClient.connectedPM)) {
    this->doScan = true;
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "No PM Connected");
  }
  if ((String(userConfig->getConnectedRemote()) != "none") && !(spinBLEClient.connectedRemote)) {
    this->doScan = true;
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "No Rem Connected");
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
String SpinBLEClient::adevName2UniqueName(NimBLEAdvertisedDevice *inDev) {
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

void SpinBLEAdvertisedDevice::set(BLEAdvertisedDevice *device, int id, BLEUUID inServiceUUID, BLEUUID inCharUUID) {
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Setting Device %s", device->getAddress().toString().c_str());
  this->advertisedDevice  = device;
  this->peerAddress       = device->getAddress();
  this->connectedClientID = id;
  this->serviceUUID       = BLEUUID(inServiceUUID);
  this->charUUID          = BLEUUID(inCharUUID);
  this->dataBufferQueue   = xQueueCreate(4, sizeof(NotifyData));
  if (inServiceUUID == HEARTSERVICE_UUID) {
    this->isHRM                = true;
    spinBLEClient.connectedHRM = true;
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Registered HRM on Connect");
  } else if (inServiceUUID == CSCSERVICE_UUID) {
    this->isCSC               = true;
    spinBLEClient.connectedCD = true;
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Registered CSC on Connect");
  } else if (inServiceUUID == CYCLINGPOWERSERVICE_UUID || inServiceUUID == FITNESSMACHINESERVICE_UUID || inServiceUUID == FLYWHEEL_UART_SERVICE_UUID ||
             inServiceUUID == ECHELON_SERVICE_UUID || inServiceUUID == PELOTON_DATA_UUID) {
    this->isPM                = true;
    spinBLEClient.connectedPM = true;
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Registered PM on Connect");
  } else if (inServiceUUID == HID_SERVICE_UUID) {
    this->isRemote                = true;
    spinBLEClient.connectedRemote = true;
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Registered Remote on Connect");
  } else {
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Failed to set service!");
  }
}

void SpinBLEAdvertisedDevice::reset() {
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Resetting Device: %d", this->connectedClientID);
  if (this->isHRM) spinBLEClient.connectedHRM = false;
  if (this->isPM) spinBLEClient.connectedPM = false;
  if (this->isCSC) spinBLEClient.connectedCD = false;
  spinBLEClient.connectedSpeed = false;

  advertisedDevice = nullptr;
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
