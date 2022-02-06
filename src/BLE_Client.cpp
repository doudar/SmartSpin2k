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

void SpinBLEClient::start() {
  // Create the task for the BLE Client loop
  xTaskCreatePinnedToCore(bleClientTask,   /* Task function. */
                          "BLEClientTask", /* name of task. */
                          4700,            /* Stack size of task */
                          NULL,            /* parameter of the task */
                          1,               /* priority of the task  */
                          &BLEClientTask,  /* Task handle to keep track of created task */
                          1);
}

static void onNotify(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify) {
  for (size_t i = 0; i < NUM_BLE_DEVICES; i++) {
    if (pBLERemoteCharacteristic->getUUID() == spinBLEClient.myBLEDevices[i].charUUID) {
      spinBLEClient.myBLEDevices[i].enqueueData(pData, length);
    }
  }
}

// BLE Client loop task
void bleClientTask(void *pvParameters) {
  for (;;) {
    if (spinBLEClient.doScan && (spinBLEClient.scanRetries > 0)) {
      spinBLEClient.scanRetries--;
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Initiating Scan from Client Task:");
      spinBLEClient.scanProcess();
    }

    vTaskDelay(BLE_CLIENT_DELAY / portTICK_PERIOD_MS);  // Delay a second between loops.
#ifdef DEBUG_STACK
    Serial.printf("BLEClient: %d \n", uxTaskGetStackHighWaterMark(BLEClientTask));
#endif  // DEBUG_STACK
    for (int x = 0; x < NUM_BLE_DEVICES; x++) {
      if (spinBLEClient.myBLEDevices[x].doConnect == true) {
        if (spinBLEClient.connectToServer()) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "We are now connected to the BLE Server.");
          vTaskDelay(5000 / portTICK_PERIOD_MS);
        } else {
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
  BLEAdvertisedDevice* myDevice = nullptr;
  int device_number             = -1;

  for (int i = 0; i < NUM_BLE_DEVICES; i++) {
    if (spinBLEClient.myBLEDevices[i].doConnect == true) {   // Client wants to be connected
      if (spinBLEClient.myBLEDevices[i].advertisedDevice) {  // Client is assigned
        if (spinBLEClient.myBLEDevices[i].advertisedDevice->isAdvertisingService(HEARTSERVICE_UUID) &&
            (!spinBLEClient.myBLEDevices[i].advertisedDevice->isAdvertisingService(FITNESSMACHINESERVICE_UUID)) && (!connectedPM) &&
            (spinBLEClient.myBLEDevices[i + 1].doConnect == true)) {  // Connect HRM last because it causes problems when connecting PM if it connects first.
          myDevice      = spinBLEClient.myBLEDevices[i + 1].advertisedDevice;
          device_number = i + 1;
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Connecting HRM last.");
        } else {
          myDevice      = spinBLEClient.myBLEDevices[i].advertisedDevice;
          device_number = i;
        }
        break;
      } else {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "doConnect and client out of alignment. Resetting device slot");
        spinBLEClient.myBLEDevices[i].reset();
        spinBLEClient.serverScan(true);
        return false;
      }
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
    } else if (myDevice->isAdvertisingService(CYCLINGPOWERSERVICE_UUID)) {
      serviceUUID = CYCLINGPOWERSERVICE_UUID;
      charUUID    = CYCLINGPOWERMEASUREMENT_UUID;
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "trying to connect to PM");
    } else if (myDevice->isAdvertisingService(FITNESSMACHINESERVICE_UUID)) {
      serviceUUID = FITNESSMACHINESERVICE_UUID;
      charUUID    = FITNESSMACHINEINDOORBIKEDATA_UUID;
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "trying to connect to Fitness machine service");
    } else if (myDevice->isAdvertisingService(ECHELON_DEVICE_UUID)) {
      serviceUUID = ECHELON_SERVICE_UUID;
      charUUID    = ECHELON_DATA_UUID;
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Trying to connect to Echelon Bike");
    } else if (myDevice->isAdvertisingService(HEARTSERVICE_UUID)) {
      serviceUUID = HEARTSERVICE_UUID;
      charUUID    = HEARTCHARACTERISTIC_UUID;
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Trying to connect to HRM");
    } else {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "No advertised UUID found");
      spinBLEClient.myBLEDevices[device_number].reset();
      return false;
    }
  } else {
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Device has no Service UUID");
    spinBLEClient.myBLEDevices[device_number].reset();
    spinBLEClient.serverScan(true);
    return false;
  }
  String t_name = "";
  if (myDevice->haveName()) {
    String t_name = myDevice->getName().c_str();
  }
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Forming a connection to: %s %s", t_name.c_str(), myDevice->getAddress().toString().c_str());

  NimBLEClient* pClient = nullptr;

  /** Check if we have a client we should reuse first **/
  if (NimBLEDevice::getClientListSize()) {
    /** Special case when we already know this device, we send false as the
     *  second argument in connect() to prevent refreshing the service database.
     *  This saves considerable time and power.
     */
    pClient = NimBLEDevice::getClientByPeerAddress(myDevice->getAddress());
    if (pClient) {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Reusing Client");
      if (!pClient->connect(myDevice, false)) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Reconnect failed ");
        this->reconnectTries--;
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "%d left.", reconnectTries);
        if (reconnectTries < 1) {
          spinBLEClient.myBLEDevices[device_number].reset();
          spinBLEClient.myBLEDevices[device_number].doConnect = false;
          connectedPM                                         = false;
          serverScan(false);
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
    pClient->setConnectionParams(12, 12, 0, 100);
    /** Set how long we are willing to wait for the connection to complete (seconds), default is 30. */
    pClient->setConnectTimeout(5);

    if (!pClient->connect(myDevice->getAddress())) {
      /** Created a client but failed to connect, don't need to keep it as it has no data */
      NimBLEDevice::deleteClient(pClient);
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Failed to connect, deleted client");
      return false;
    }
  }

  if (!pClient->isConnected()) {
    if (!pClient->connect(myDevice)) {
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Failed to connect");
      return false;
    }
  }

  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Connected to: %s RSSI %d", pClient->getPeerAddress().toString().c_str(), pClient->getRssi());

  /** Now we can read/write/subscribe the charateristics of the services we are interested in */
  NimBLERemoteService* pSvc        = nullptr;
  NimBLERemoteCharacteristic* pChr = nullptr;
  NimBLERemoteDescriptor* pDsc     = nullptr;

  pSvc = pClient->getService(serviceUUID);
  if (pSvc) { /** make sure it's not null */
    pChr = pSvc->getCharacteristic(charUUID);

    if (pChr) { /** make sure it's not null */
      if (pChr->canRead()) {
        std::string value = pChr->readValue();
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "The characteristic value was: %s", value.c_str());
      }

      /** registerForNotify() has been deprecated and replaced with subscribe() / unsubscribe().
       *  Subscribe parameter defaults are: notifications=true, notifyCallback=nullptr, response=false.
       *  Unsubscribe parameter defaults are: response=false.
       */
      if (pChr->canNotify()) {
        // if(!pChr->registerForNotify(notifyCB)) {
        if (!pChr->subscribe(true, onNotify)) {
          /** Disconnect if subscribe failed */
          pClient->disconnect();
          return false;
        }
      } else if (pChr->canIndicate()) {
        /** Send false as first argument to subscribe to indications instead of notifications */
        // if(!pChr->registerForNotify(notifyCB, false)) {
        if (!pChr->subscribe(false, onNotify)) {
          /** Disconnect if subscribe failed */
          pClient->disconnect();
          return false;
        }
      }
      this->reconnectTries = MAX_RECONNECT_TRIES;
      this->scanRetries    = MAX_SCAN_RETRIES;
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Successful %s subscription.", pChr->getUUID().toString().c_str());
      spinBLEClient.myBLEDevices[device_number].doConnect = false;
      this->reconnectTries                                = MAX_RECONNECT_TRIES;
      spinBLEClient.myBLEDevices[device_number].set(myDevice, pClient->getConnId(), serviceUUID, charUUID);
      // vTaskDelay(100 / portTICK_PERIOD_MS); //Give time for connection to finalize.
      removeDuplicates(pClient);
      postConnect(pClient);
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
  // Currently Not Used
}

void MyClientCallback::onDisconnect(NimBLEClient *pclient) {
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Disconnect Called");

  if (spinBLEClient.intentionalDisconnect) {
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Intentional Disconnect");
    spinBLEClient.intentionalDisconnect = false;
    return;
  }
  if (!pclient->isConnected()) {
    NimBLEAddress addr = pclient->getPeerAddress();
    // auto addr = BLEDevice::getDisconnectedClient()->getPeerAddress();
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "This disconnected client Address %s", addr.toString().c_str());
    for (size_t i = 0; i < NUM_BLE_DEVICES; i++) {
      if (addr == spinBLEClient.myBLEDevices[i].peerAddress) {
        // spinBLEClient.myBLEDevices[i].connectedClientID = BLE_HS_CONN_HANDLE_NONE;
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Detected %s Disconnect", spinBLEClient.myBLEDevices[i].serviceUUID.toString().c_str());
        spinBLEClient.myBLEDevices[i].doConnect = true;
        if ((spinBLEClient.myBLEDevices[i].charUUID == CYCLINGPOWERMEASUREMENT_UUID) || (spinBLEClient.myBLEDevices[i].charUUID == FITNESSMACHINEINDOORBIKEDATA_UUID) ||
            (spinBLEClient.myBLEDevices[i].charUUID == FLYWHEEL_UART_RX_UUID) || (spinBLEClient.myBLEDevices[i].charUUID == ECHELON_SERVICE_UUID)) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Deregistered PM on Disconnect");
          spinBLEClient.connectedPM = false;
          break;
        }
        if ((spinBLEClient.myBLEDevices[i].charUUID == HEARTCHARACTERISTIC_UUID)) {
          SS2K_LOG(BLE_CLIENT_LOG_TAG, "Deregistered HR on Disconnect");
          spinBLEClient.connectedHR = false;
          break;
        }
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
  auto advertisedDeviceInfo = advertisedDevice->toString();
  ss2k_remove_newlines(&advertisedDeviceInfo);
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "BLE Advertised Device found: %s", advertisedDeviceInfo.c_str());
  String aDevName;
  if (advertisedDevice->haveName()) {
    aDevName = String(advertisedDevice->getName().c_str());
  } else {
    aDevName = "";
  }
  if ((advertisedDevice->haveServiceUUID()) &&
      (advertisedDevice->isAdvertisingService(CYCLINGPOWERSERVICE_UUID) || (advertisedDevice->isAdvertisingService(FLYWHEEL_UART_SERVICE_UUID) && aDevName == FLYWHEEL_BLE_NAME) ||
       advertisedDevice->isAdvertisingService(FITNESSMACHINESERVICE_UUID) || advertisedDevice->isAdvertisingService(HEARTSERVICE_UUID) ||
       advertisedDevice->isAdvertisingService(ECHELON_DEVICE_UUID))) {
    // if ((aDevName == c_PM) || (advertisedDevice->getAddress().toString().c_str() == c_PM) || (aDevName == c_HR) || (advertisedDevice->getAddress().toString().c_str() == c_HR) ||
    // (String(c_PM) == ("any")) || (String(c_HR) == ("any"))) { //notice the subtle difference vv getServiceUUID(int) returns the index of the service in the list or the 0 slot if
    // not specified.
    SS2K_LOG(BLE_CLIENT_LOG_TAG, "Matching Device Name: %s", aDevName.c_str());
    if (advertisedDevice->getServiceUUID() == HEARTSERVICE_UUID) {
      if (String(userConfig.getconnectedHeartMonitor()) == "any") {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "HR String Matched Any");
        // continue
      } else if (aDevName != String(userConfig.getconnectedHeartMonitor()) || (String(userConfig.getconnectedHeartMonitor()) == "none")) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Skipping non-selected HRM |%s|%s", aDevName.c_str(), userConfig.getconnectedHeartMonitor());
        return;
      } else if (aDevName == String(userConfig.getconnectedHeartMonitor())) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "HR String Matched %s", aDevName.c_str());
      }
    } else {  // Already tested -->((advertisedDevice->getServiceUUID()(CYCLINGPOWERSERVICE_UUID) || advertisedDevice->getServiceUUID()(FLYWHEEL_UART_SERVICE_UUID) ||
              // advertisedDevice->getServiceUUID()(FITNESSMACHINESERVICE_UUID)))
      if (String(userConfig.getconnectedPowerMeter()) == "any") {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "PM String Matched Any");
        // continue
      } else if (aDevName != String(userConfig.getconnectedPowerMeter()) || (String(userConfig.getconnectedPowerMeter()) == "none")) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Skipping non-selected PM |%s|%s", aDevName.c_str(), userConfig.getconnectedPowerMeter());
        return;
      } else if (aDevName == String(userConfig.getconnectedPowerMeter())) {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "PM String Matched %s", aDevName.c_str());
      }
    }
    for (size_t i = 0; i < NUM_BLE_DEVICES; i++) {
      if ((spinBLEClient.myBLEDevices[i].advertisedDevice == nullptr) ||
          (advertisedDevice->getAddress() == spinBLEClient.myBLEDevices[i].peerAddress)) {  // found empty device slot
        spinBLEClient.myBLEDevices[i].set(advertisedDevice);
        spinBLEClient.myBLEDevices[i].doConnect = true;
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "doConnect set on device: %d", i);

        return;
      }
      SS2K_LOG(BLE_CLIENT_LOG_TAG, "Checking Slot %d", i);
    }
    return;
    //}
  }
}

void SpinBLEClient::scanProcess() {
  this->doScan = false;  // Confirming we did the scan
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Scanning for BLE servers and putting them into a list...");

  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallback());
  pBLEScan->setInterval(45); //45?
  pBLEScan->setWindow(15); //15?
  pBLEScan->setDuplicateFilter(true);
  pBLEScan->setActiveScan(true);
  BLEScanResults foundDevices = pBLEScan->start(10, false);
  // Load the scan into a Json String
  int count = foundDevices.getCount();

  StaticJsonDocument<1000> devices;

  String device = "";

  for (int i = 0; i < count; i++) {
    BLEAdvertisedDevice d = foundDevices.getDevice(i);
    if (d.isAdvertisingService(CYCLINGPOWERSERVICE_UUID) || d.isAdvertisingService(HEARTSERVICE_UUID) || d.isAdvertisingService(FLYWHEEL_UART_SERVICE_UUID) ||
        d.isAdvertisingService(FITNESSMACHINESERVICE_UUID) || d.isAdvertisingService(ECHELON_DEVICE_UUID)) {
      device                     = "device " + String(i);
      devices[device]["address"] = d.getAddress().toString();

      if (d.haveName()) {
        devices[device]["name"] = d.getName();
      }

      if (d.haveServiceUUID()) {
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
  userConfig.setFoundDevices(output);
  pBLEScan = nullptr;  // free up memory
}

// This is the main server scan request process to use.
void SpinBLEClient::serverScan(bool connectRequest) {
  if (connectRequest) {
    this->scanRetries = MAX_SCAN_RETRIES;
  }
  this->doScan = true;
}

// Shuts down all BLE processes.
void SpinBLEClient::disconnect() {
  this->scanRetries           = 0;
  this->reconnectTries        = 0;
  intentionalDisconnect = true;
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Shutting Down all BLE services");
  if (NimBLEDevice::getInitialized()) {
    NimBLEDevice::deinit();
    vTaskDelay(100 / portTICK_RATE_MS);
  }
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
            BLEDevice::getClientByPeerAddress(oldBLEd.peerAddress)->disconnect();
            oldBLEd.reset();
            spinBLEClient.intentionalDisconnect = true;
            return;
          }
        }
      }
    }
  }
}

void SpinBLEClient::resetDevices() {
  SpinBLEAdvertisedDevice tBLEd;
  for (size_t i = 0; i < NUM_BLE_DEVICES; i++) {
    tBLEd = this->myBLEDevices[i];
    tBLEd.reset();
  }
}

void SpinBLEClient::postConnect(NimBLEClient *pClient) {
  for (size_t i = 0; i < NUM_BLE_DEVICES; i++) {
    if (pClient->getPeerAddress() == this->myBLEDevices[i].peerAddress) {
      if ((this->myBLEDevices[i].charUUID == CYCLINGPOWERMEASUREMENT_UUID) || (this->myBLEDevices[i].charUUID == FITNESSMACHINEINDOORBIKEDATA_UUID) ||
          (this->myBLEDevices[i].charUUID == FLYWHEEL_UART_RX_UUID) || (this->myBLEDevices[i].charUUID == ECHELON_DATA_UUID)) {
        this->connectedPM = true;
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Registered PM on Connect");
        if (this->myBLEDevices[i].charUUID == ECHELON_DATA_UUID) {
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
        }
        // spinBLEClient.removeDuplicates(pclient);
        BLEDevice::getServer()->updateConnParams(pClient->getConnId(), 400, 400, 0, 250);
        return;
      }
      if ((this->myBLEDevices[i].charUUID == HEARTCHARACTERISTIC_UUID)) {
        this->connectedHR = true;
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "Registered HRM on Connect");
        BLEDevice::getServer()->updateConnParams(pClient->getConnId(), 400, 400, 0, 250);
        return;
      } else {
        SS2K_LOG(BLE_CLIENT_LOG_TAG, "These did not match|%s|%s|", pClient->getPeerAddress().toString().c_str(), this->myBLEDevices[i].peerAddress.toString().c_str());
      }
    }
  }
}

bool SpinBLEAdvertisedDevice::enqueueData(uint8_t *data, size_t length) {
  NotifyData notifyData;
  // Serial.println("enqueue Called");
  if (!this->dataBufferQueue) {
    // Serial.println("Creating queue");
    this->dataBufferQueue = xQueueCreate(6, sizeof(NotifyData));
    if (!this->dataBufferQueue) {
      // Serial.println("Failed to create queue");
      return pdFALSE;
    }
  }

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
  logBufP += sprintf(logBufP, " HRM: (%s)", userSelectedHR ? "true" : "false");
  logBufP += sprintf(logBufP, " PM: (%s)", userSelectedPM ? "true" : "false");
  logBufP += sprintf(logBufP, " CSC: (%s)", userSelectedCSC ? "true" : "false");
  logBufP += sprintf(logBufP, " CT: (%s)", userSelectedCT ? "true" : "false");
  logBufP += sprintf(logBufP, " doConnect: (%s)", doConnect ? "true" : "false");
  strcat(logBufP, "|");
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "%s", String(logBuf));
}
