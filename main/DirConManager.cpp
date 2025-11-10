/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "DirConManager.h"
#include "SS2KLog.h"
#include <algorithm>
#include <BLE_Fitness_Machine_Service.h>

#define DIRCON_LOG_TAG "DirConManager"

// Static member initialization
bool DirConManager::started         = false;
String DirConManager::statusMessage = "";
WiFiClient DirConManager::dirConClients[DIRCON_MAX_CLIENTS];
WiFiServer* DirConManager::tcpServer = nullptr;
uint8_t DirConManager::receiveBuffer[DIRCON_MAX_CLIENTS][DIRCON_RECEIVE_BUFFER_SIZE];
size_t DirConManager::receiveBufferLength[DIRCON_MAX_CLIENTS] = {0};
uint8_t DirConManager::sendBuffer[DIRCON_SEND_BUFFER_SIZE];
uint8_t DirConManager::lastSequenceNumber[DIRCON_MAX_CLIENTS]                           = {0};
bool DirConManager::clientSubscriptions[DIRCON_MAX_CLIENTS][DIRCON_MAX_CHARACTERISTICS] = {false};

// Static buffer to store the list of UUIDs to avoid dynamic string allocations
static char uuidListBuffer[128] = "";
static size_t uuidListLength    = 0;

bool DirConManager::start() {
  if (!started) {
    // Initialize buffers
    for (int i = 0; i < DIRCON_MAX_CLIENTS; i++) {
      receiveBufferLength[i] = 0;
      lastSequenceNumber[i]  = 0;
      for (int j = 0; j < DIRCON_MAX_CHARACTERISTICS; j++) {
        clientSubscriptions[i][j] = false;
      }
    }

    // Setup MDNS service
    setupMDNS();

    // Create TCP server
    tcpServer = new WiFiServer(DIRCON_TCP_PORT);
    if (tcpServer == nullptr) {
      SS2K_LOG(DIRCON_LOG_TAG, "Failed to create TCP server");
      return false;
    }

    tcpServer->begin();

    started = true;
    updateStatusMessage();
    SS2K_LOG(DIRCON_LOG_TAG, "%s", statusMessage.c_str());
    return true;
  }
  return false;
}

void DirConManager::stop() {
  if (started) {
    // Stop TCP server and disconnect clients
    if (tcpServer != nullptr) {
      tcpServer->close();
      delete tcpServer;
      tcpServer = nullptr;
    }

    for (int i = 0; i < DIRCON_MAX_CLIENTS; i++) {
      if (dirConClients[i].connected()) {
        dirConClients[i].stop();
      }
    }

    started = false;
    updateStatusMessage();
    SS2K_LOG(DIRCON_LOG_TAG, "%s", statusMessage.c_str());
  }
}

void DirConManager::update() {
  // Return immediately unless DIRCON_MANAGER_DELAY has passed.
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < DIRCON_MANAGER_DELAY) {
    return;
  }
  lastUpdate = millis();

  if (!started) {
    return;
  }

  // Check for new clients
  checkForNewClients();

  // Handle data from connected clients
  handleClientData();
}

// returns true if we have clients connected
int DirConManager::connectedClients() {
  int connectedClients = 0;
  for (int i = 0; i < DIRCON_MAX_CLIENTS; i++) {
    if (dirConClients[i].connected()) {
      connectedClients++;
    }
  }
  return connectedClients;
}
void DirConManager::updateStatusMessage() {
  if (!started) {
    statusMessage = "DirCon service stopped";
    return;
  }
  statusMessage = "DirCon service running on port " + String(DIRCON_TCP_PORT) + " with " + String(connectedClients()) + " connected client(s)";
}

void DirConManager::setupMDNS() {
  // Static buffers for strings to avoid repeated allocations
  static char macAddress[18];    // MAC format: 11:22:33:44:55:66\0
  static char serialNumber[12];  // SS2K-112233445566\0

  // Get device MAC address using existing buffer
  strcpy(macAddress, WiFi.macAddress().c_str());
  // replace colons with dashes for mac address
  for (char* p = macAddress; *p; p++) {
    if (*p == ':') {
      *p = '-';
    }
  }

  // Create a unique serial number (using MAC address), and remove the dashes and change the letters to decimal numbers.
  snprintf(serialNumber, sizeof(serialNumber), "%02X%02X%02X%02X%02X%02X", macAddress[0], macAddress[1], macAddress[3], macAddress[4], macAddress[6], macAddress[7]);

  // Add DirCon service to MDNS
  SS2K_LOG(DIRCON_LOG_TAG, "Adding DirCon MDNS service: %s.%s on port %d", DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, DIRCON_TCP_PORT);

  if (MDNS.addService(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, DIRCON_TCP_PORT)) {
    SS2K_LOG(DIRCON_LOG_TAG, "Successfully added MDNS service");
  } else {
    SS2K_LOG(DIRCON_LOG_TAG, "Failed to add MDNS service");
  }

  // Add required text records for the DirCon protocol
  MDNS.addServiceTxt(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, "mac-address", (const char*)macAddress);
  MDNS.addServiceTxt(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, "serial-number", (const char*)serialNumber);

  // Add BLE service UUIDs that this device supports
  // Initially empty, will be updated when BLE is initialized
  MDNS.addServiceTxt(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, "ble-service-uuids", "");

  SS2K_LOG(DIRCON_LOG_TAG, "DirCon MDNS service setup complete");
}
void DirConManager::addBleServiceUuid(const NimBLEUUID& serviceUuid) {
  if (!started) {
    return;
  }

  // Create a non-const copy we can call to16() on
  NimBLEUUID uuid(serviceUuid);

  // Get the 16-bit UUID string representation
  std::string uuidStr   = uuid.to16().toString();
  const char* shortUuid = uuidStr.c_str();

  // Check if UUID is already in the list
  if (strstr(uuidListBuffer, shortUuid) != nullptr) {
    // UUID already added
    return;
  }

  // Calculate if we have enough space for the UUID plus a comma
  size_t shortUuidLen  = strlen(shortUuid);
  bool needComma       = (uuidListLength > 0);
  size_t requiredSpace = shortUuidLen + (needComma ? 1 : 0);

  // Ensure we have enough space
  if (uuidListLength + requiredSpace >= sizeof(uuidListBuffer) - 1) {
    SS2K_LOG(DIRCON_LOG_TAG, "Warning: Not enough space to add UUID %s", shortUuid);
    return;
  }

  // Add comma if needed
  if (needComma) {
    uuidListBuffer[uuidListLength++] = ',';
  }

  // Add the UUID to our static buffer
  strcpy(&uuidListBuffer[uuidListLength], shortUuid);
  uuidListLength += shortUuidLen;

  // Update the MDNS service TXT record with the updated BLE service UUIDs
  SS2K_LOG(DIRCON_LOG_TAG, "Adding BLE service UUID %s to DirCon MDNS", shortUuid);
  MDNS.addServiceTxt(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, "ble-service-uuids", (const char*)uuidListBuffer);
}

void DirConManager::checkForNewClients() {
  if (tcpServer == nullptr || !tcpServer->hasClient()) {
    return;
  }

  WiFiClient newClient = tcpServer->accept();
  if (!newClient) {
    return;
  }

  // Find a free slot for the new client
  bool clientAccepted = false;
  int clientIndex     = -1;

  for (int i = 0; i < DIRCON_MAX_CLIENTS; i++) {
    if (!dirConClients[i].connected()) {
      dirConClients[i] = newClient;
      clientAccepted   = true;
      clientIndex      = i;

      // Clear receive buffer and subscription state
      receiveBufferLength[i] = 0;
      lastSequenceNumber[i]  = 0;
      for (int j = 0; j < DIRCON_MAX_CHARACTERISTICS; j++) {
        clientSubscriptions[i][j] = false;
      }

      break;
    }
  }

  if (clientAccepted) {
    String clientIP = dirConClients[clientIndex].remoteIP().toString();
    SS2K_LOG(DIRCON_LOG_TAG, "New DirCon client connected from %s, assigned slot %d", clientIP.c_str(), clientIndex);
    updateStatusMessage();
  } else {
    SS2K_LOG(DIRCON_LOG_TAG, "Rejected DirCon client, no free slots available");
    newClient.stop();
  }
}

void DirConManager::handleClientData() {
  for (int i = 0; i < DIRCON_MAX_CLIENTS; i++) {
    if (!dirConClients[i].connected()) {
      continue;
    }

    // Check if client disconnected
    if (!dirConClients[i].connected()) {
      String clientIP = dirConClients[i].remoteIP().toString();
      SS2K_LOG(DIRCON_LOG_TAG, "DirCon client %s disconnected", clientIP.c_str());
      dirConClients[i].stop();
      removeAllSubscriptions(i);
      updateStatusMessage();
      continue;
    }

    // Check if data is available
    if (dirConClients[i].available()) {
      // Read available data into buffer
      while (dirConClients[i].available() && receiveBufferLength[i] < DIRCON_RECEIVE_BUFFER_SIZE) {
        receiveBuffer[i][receiveBufferLength[i]] = dirConClients[i].read();
        receiveBufferLength[i]++;
      }

      // Process messages in buffer
      size_t processedBytes = 0;
      while (processedBytes < receiveBufferLength[i]) {
        DirConMessage message;
        size_t parsedBytes = message.parse(receiveBuffer[i] + processedBytes, receiveBufferLength[i] - processedBytes, lastSequenceNumber[i]);

        if (parsedBytes == 0) {
          // Not enough data for a complete message or invalid message
          break;
        }

        // Process the message
        if (message.Identifier != DIRCON_MSGID_ERROR) {
          lastSequenceNumber[i] = message.SequenceNumber;
          processDirConMessage(&message, i);
        }

        processedBytes += parsedBytes;
      }

      // Remove processed bytes from buffer
      if (processedBytes > 0) {
        if (processedBytes < receiveBufferLength[i]) {
          // Move remaining bytes to beginning of buffer
          memmove(receiveBuffer[i], receiveBuffer[i] + processedBytes, receiveBufferLength[i] - processedBytes);
          receiveBufferLength[i] -= processedBytes;
        } else {
          // All bytes processed
          receiveBufferLength[i] = 0;
        }
      }
    }
  }
}

bool DirConManager::processDirConMessage(DirConMessage* message, size_t clientIndex) {
  if (!message->Request) {
    // We only process requests, not responses
    return false;
  }
  bool success = true;
  DirConMessage response;
  response.Request        = false;
  response.SequenceNumber = message->SequenceNumber;

  switch (message->Identifier) {
    case DIRCON_MSGID_DISCOVER_SERVICES: {
      // Handle service discovery
      response.Identifier   = DIRCON_MSGID_DISCOVER_SERVICES;
      response.ResponseCode = DIRCON_RESPCODE_SUCCESS_REQUEST;

      // Get all service UUIDs
      std::vector<NimBLEUUID> services = getAvailableServices();
      for (NimBLEUUID& service : services) {
        // Add each service UUID to the response
        response.AdditionalUUIDs.push_back(service);
      }

      // Log discovery request details
      SS2K_LOG(DIRCON_LOG_TAG, "Received service discovery request from client %d", clientIndex);
      SS2K_LOG(DIRCON_LOG_TAG, "Responding with %d service UUIDs", services.size());

      // Send the response
      sendResponse(&response, clientIndex);
      break;
    }

    case DIRCON_MSGID_DISCOVER_CHARACTERISTICS: {
      // Handle characteristic discovery for a service
      response.Identifier   = DIRCON_MSGID_DISCOVER_CHARACTERISTICS;
      response.ResponseCode = DIRCON_RESPCODE_SUCCESS_REQUEST;
      response.UUID         = message->UUID;

      // Get BLE service
      NimBLEService* service = NimBLEDevice::getServer()->getServiceByUUID(message->UUID);
      if (service == nullptr) {
        sendErrorResponse(DIRCON_MSGID_DISCOVER_CHARACTERISTICS, message->SequenceNumber, DIRCON_RESPCODE_SERVICE_NOT_FOUND, clientIndex);
        return false;
      }

      // Get all characteristics for the service
      std::vector<NimBLECharacteristic*> characteristics = service->getCharacteristics();
      for (NimBLECharacteristic* characteristic : characteristics) {
        if (characteristic != nullptr) {
          uint32_t properties = characteristic->getProperties();
          uint8_t dirconProps = getDirConProperties(properties);
          Serial.printf("Telling client %s is availiable", characteristic->getUUID().toString().c_str());
          response.AdditionalUUIDs.push_back(characteristic->getUUID());
          response.AdditionalData.push_back(dirconProps);
        }
      }

      sendResponse(&response, clientIndex);
      break;
    }

    case DIRCON_MSGID_READ_CHARACTERISTIC: {
      // Handle characteristic read
      response.Identifier   = DIRCON_MSGID_READ_CHARACTERISTIC;
      response.ResponseCode = DIRCON_RESPCODE_SUCCESS_REQUEST;
      response.UUID         = message->UUID;

      // Use the helper function to find the characteristic
      NimBLECharacteristic* characteristic = findCharacteristic(message->UUID);

      if (characteristic == nullptr) {
        sendErrorResponse(DIRCON_MSGID_READ_CHARACTERISTIC, message->SequenceNumber, DIRCON_RESPCODE_CHARACTERISTIC_NOT_FOUND, clientIndex);
        return false;
      }

      // Check if read is allowed based on properties
      if (!(characteristic->getProperties() & NIMBLE_PROPERTY::READ)) {
        sendErrorResponse(DIRCON_MSGID_READ_CHARACTERISTIC, message->SequenceNumber, DIRCON_RESPCODE_CHARACTERISTIC_OPERATION_NOT_SUPPORTED, clientIndex);
        return false;
      }

      // Read the value
      NimBLEAttValue value = characteristic->getValue();
      size_t length        = value.size();

      for (size_t i = 0; i < length; i++) {
        response.AdditionalData.push_back(value[i]);
      }

      sendResponse(&response, clientIndex);
      break;
    }

    case DIRCON_MSGID_WRITE_CHARACTERISTIC: {
      // Handle characteristic write
      response.Identifier   = DIRCON_MSGID_WRITE_CHARACTERISTIC;
      response.ResponseCode = DIRCON_RESPCODE_SUCCESS_REQUEST;
      response.UUID         = message->UUID;

      // Use the helper function to find the characteristic
      NimBLECharacteristic* characteristic = findCharacteristic(message->UUID);

      if (characteristic == nullptr) {
        sendErrorResponse(DIRCON_MSGID_WRITE_CHARACTERISTIC, message->SequenceNumber, DIRCON_RESPCODE_CHARACTERISTIC_NOT_FOUND, clientIndex);
        SS2K_LOG(DIRCON_LOG_TAG, "Write characteristic failed: characteristic %s not found", message->UUID.toString().c_str());
        return false;
      }

      // Check if write is allowed based on properties
      if (!(characteristic->getProperties() & NIMBLE_PROPERTY::WRITE)) {
        sendErrorResponse(DIRCON_MSGID_WRITE_CHARACTERISTIC, message->SequenceNumber, DIRCON_RESPCODE_CHARACTERISTIC_OPERATION_NOT_SUPPORTED, clientIndex);
        // log which characteristic failed
        SS2K_LOG(DIRCON_LOG_TAG, "Write operation not supported for characteristic %s", characteristic->getUUID().toString().c_str());
        return false;
      }

      // Write the value (setValue doesn't return a status in NimBLE)
      characteristic->setValue(message->AdditionalData.data(), message->AdditionalData.size());

      // handle FTMS control Point Writes
      if (characteristic->getUUID().equals(FITNESSMACHINECONTROLPOINT_UUID)) {
        spinBLEServer.writeCache.push(characteristic->getValue());
        fitnessMachineService.processFTMSWrite();
        response.AdditionalData = characteristic->getValue();
      }

      sendResponse(&response, clientIndex);
      break;
    }

    case DIRCON_MSGID_ENABLE_CHARACTERISTIC_NOTIFICATIONS: {
      // Handle notification subscription
      response.Identifier   = DIRCON_MSGID_ENABLE_CHARACTERISTIC_NOTIFICATIONS;
      response.ResponseCode = DIRCON_RESPCODE_SUCCESS_REQUEST;
      response.UUID         = message->UUID;

      // Use the helper function to find the characteristic
      NimBLECharacteristic* characteristic = findCharacteristic(message->UUID);

      if (characteristic == nullptr) {
        sendErrorResponse(DIRCON_MSGID_ENABLE_CHARACTERISTIC_NOTIFICATIONS, message->SequenceNumber, DIRCON_RESPCODE_CHARACTERISTIC_NOT_FOUND, clientIndex);
        SS2K_LOG(DIRCON_LOG_TAG, "Enable notifications failed: characteristic %s not found", message->UUID.toString().c_str());
        return false;
      }

      // Check if notifications are allowed based on properties
      if (!(characteristic->getProperties() & NIMBLE_PROPERTY::NOTIFY)) {
        sendErrorResponse(DIRCON_MSGID_ENABLE_CHARACTERISTIC_NOTIFICATIONS, message->SequenceNumber, DIRCON_RESPCODE_CHARACTERISTIC_OPERATION_NOT_SUPPORTED, clientIndex);
        SS2K_LOG(DIRCON_LOG_TAG, "Notifications not supported for characteristic %s", characteristic->getUUID().toString().c_str());
        return false;
      }

      // Get enable/disable flag
      bool enableNotifications = false;
      if (message->AdditionalData.size() > 0) {
        enableNotifications = message->AdditionalData[0] != 0;
      }

      // Update subscription
      if (enableNotifications) {
        addSubscription(clientIndex, message->UUID);
      } else {
        removeSubscription(clientIndex, message->UUID);
      }

      sendResponse(&response, clientIndex);
      break;
    }

    default:
      // Unknown message type
      sendErrorResponse(message->Identifier, message->SequenceNumber, DIRCON_RESPCODE_UNKNOWN_MESSAGE_TYPE, clientIndex);
      success = false;
      break;
  }

  return success;
}

void DirConManager::sendErrorResponse(uint8_t messageId, uint8_t sequenceNumber, uint8_t errorCode, size_t clientIndex) {
  DirConMessage errorResponse;
  errorResponse.Request        = false;
  errorResponse.Identifier     = messageId;
  errorResponse.SequenceNumber = sequenceNumber;
  errorResponse.ResponseCode   = errorCode;
  sendResponse(&errorResponse, clientIndex);
}

void DirConManager::sendResponse(DirConMessage* message, size_t clientIndex) {
  if (clientIndex >= DIRCON_MAX_CLIENTS || !dirConClients[clientIndex].connected()) {
    SS2K_LOG(DIRCON_LOG_TAG, "Cannot send response - client %d is not connected", clientIndex);
    return;
  }

  // Log the message type being sent
  SS2K_LOG(DIRCON_LOG_TAG, "Sending response message type 0x%02X to client %d", message->Identifier, clientIndex);

  // For discover services, log additional details
  if (message->Identifier == DIRCON_MSGID_DISCOVER_SERVICES) {
    SS2K_LOG(DIRCON_LOG_TAG, "Discover services response contains %d UUIDs", message->AdditionalUUIDs.size());
    for (size_t i = 0; i < message->AdditionalUUIDs.size(); i++) {
      SS2K_LOG(DIRCON_LOG_TAG, "Service %d: %s", i, message->AdditionalUUIDs[i].toString().c_str());
    }
  }

  std::vector<uint8_t>* encodedMessage = message->encode(lastSequenceNumber[clientIndex]);
  if (encodedMessage != nullptr && encodedMessage->size() > 0) {
    // SS2K_LOG(DIRCON_LOG_TAG, "Sending %d bytes to client %d", encodedMessage->size(), clientIndex);
    dirConClients[clientIndex].write(encodedMessage->data(), encodedMessage->size());
  } else {
    SS2K_LOG(DIRCON_LOG_TAG, "Error: No encoded message to send");
  }
}

void DirConManager::notifyCharacteristic(const NimBLEUUID& serviceUuid, const NimBLEUUID& characteristicUuid, uint8_t* data, size_t length) {
  if (!started || !connectedClients()) {
    return;
  }

  // We can skip the service lookup since we're only validating that the characteristic exists
  // and we'll directly broadcast to all clients anyway
  if (findCharacteristic(characteristicUuid) == nullptr) {
    return;
  }

  // Send notifications to subscribed clients
  broadcastNotification(characteristicUuid, data, length);
}

void DirConManager::broadcastNotification(const NimBLEUUID& characteristicUuid, uint8_t* data, size_t length) {
  // Create a single notification message that will be reused for all clients
  static DirConMessage notification;  // Static to avoid repeated heap allocations

  // Initialize the notification once
  notification.Request    = false;
  notification.Identifier = DIRCON_MSGID_UNSOLICITED_CHARACTERISTIC_NOTIFICATION;
  notification.UUID       = characteristicUuid;

  // Copy notification data (only done once)
  notification.AdditionalData.clear();
  notification.AdditionalData.reserve(length);  // Pre-allocate space to avoid reallocations
  for (size_t j = 0; j < length; j++) {
    notification.AdditionalData.push_back(data[j]);
  }

  // Encode the message once
  std::vector<uint8_t>* encodedMessage = notification.encode(0);
  if (encodedMessage == nullptr || encodedMessage->size() == 0) {
    return;  // Nothing to send
  }

  // Send to all connected clients
  for (int i = 0; i < DIRCON_MAX_CLIENTS; i++) {
    if (!dirConClients[i].connected() || !hasSubscription(i, characteristicUuid)) {
      continue;
    }
#ifdef DEBUG_DIRCON_MESSAGES
    bool sentDebug = false;
    // Print the outgoing raw message bytes to serial
    if (!sentDebug) DirConMessage::printVectorBytesToSerial(*encodedMessage, false);
    sentDebug = true;
#endif
    dirConClients[i].write(encodedMessage->data(), encodedMessage->size());
  }
}

// Static variable to hold the available services (initialized once)
static std::vector<NimBLEUUID> cachedServices;
static bool servicesInitialized = false;

std::vector<NimBLEUUID> DirConManager::getAvailableServices() {
  // Initialize the services list only once
  if (!servicesInitialized) {
    cachedServices.clear();

    // Add each service with descriptive name for better debugging
    NimBLEUUID cyclingPowerUuid = NimBLEUUID(CYCLINGPOWERSERVICE_UUID);
    cachedServices.push_back(cyclingPowerUuid);

    NimBLEUUID cscUuid = NimBLEUUID(CSCSERVICE_UUID);
    cachedServices.push_back(cscUuid);

    NimBLEUUID heartUuid = NimBLEUUID(HEARTSERVICE_UUID);
    cachedServices.push_back(heartUuid);

    NimBLEUUID ftmsUuid = NimBLEUUID(FITNESSMACHINESERVICE_UUID);
    cachedServices.push_back(ftmsUuid);

    // Log summary
    SS2K_LOG(DIRCON_LOG_TAG, "Initialized service discovery with %d services", cachedServices.size());
    servicesInitialized = true;
  }

  return cachedServices;
}

std::vector<NimBLECharacteristic*> DirConManager::getCharacteristics(const NimBLEUUID& serviceUuid) {
  std::vector<NimBLECharacteristic*> characteristics;

  // Get the service
  NimBLEService* service = NimBLEDevice::getServer()->getServiceByUUID(serviceUuid);
  if (service == nullptr) {
    return characteristics;
  }
  for (const NimBLECharacteristic* characteristic : service->getCharacteristics()) {
    if (characteristic != nullptr) {
      characteristics.push_back(const_cast<NimBLECharacteristic*>(characteristic));
    }
  }
  // Find service-specific characteristics based on known UUIDs
  /*if (serviceUuid.equals(CYCLINGPOWERSERVICE_UUID)) {
    characteristics.push_back(service->getCharacteristic(CYCLINGPOWERMEASUREMENT_UUID));
    characteristics.push_back(service->getCharacteristic(CYCLINGPOWERFEATURE_UUID));
    characteristics.push_back(service->getCharacteristic(SENSORLOCATION_UUID));
  } else if (serviceUuid.equals(CSCSERVICE_UUID)) {
    characteristics.push_back(service->getCharacteristic(CSCMEASUREMENT_UUID));
  } else if (serviceUuid.equals(HEARTSERVICE_UUID)) {
    characteristics.push_back(service->getCharacteristic(HEARTCHARACTERISTIC_UUID));
  } else if (serviceUuid.equals(FITNESSMACHINESERVICE_UUID)) {
    characteristics.push_back(service->getCharacteristic(FITNESSMACHINEINDOORBIKEDATA_UUID));
    characteristics.push_back(service->getCharacteristic(FITNESSMACHINEFEATURE_UUID));
    characteristics.push_back(service->getCharacteristic(FITNESSMACHINECONTROLPOINT_UUID));
    characteristics.push_back(service->getCharacteristic(FITNESSMACHINESTATUS_UUID));
  } else if (serviceUuid.equals(DEVICE_INFORMATION_SERVICE_UUID)) {
    // Add device info characteristics if needed
  } else if (serviceUuid.equals(WATTBIKE_SERVICE_UUID)) {
    // Add wattbike service characteristics
  }
*/
  // Filter out null characteristics
  auto it = std::remove_if(characteristics.begin(), characteristics.end(), [](NimBLECharacteristic* c) { return c == nullptr; });
  characteristics.erase(it, characteristics.end());

  return characteristics;
}

NimBLECharacteristic* DirConManager::findCharacteristic(const NimBLEUUID& characteristicUuid) {
  // Get cached services (doesn't allocate new memory)
  const std::vector<NimBLEUUID>& services = getAvailableServices();

  // Search through each service for the characteristic
  for (const NimBLEUUID& serviceUuid : services) {
    NimBLEService* service = NimBLEDevice::getServer()->getServiceByUUID(serviceUuid);
    if (service != nullptr) {
      NimBLECharacteristic* characteristic = service->getCharacteristic(characteristicUuid);
      if (characteristic != nullptr) {
        return characteristic;
      }
    }
  }

  return nullptr;
}

uint8_t DirConManager::getDirConProperties(uint32_t characteristicProperties) {
  uint8_t properties = 0;

  if (characteristicProperties & NIMBLE_PROPERTY::READ) {
    properties |= DIRCON_CHAR_PROP_FLAG_READ;
  }

  if (characteristicProperties & NIMBLE_PROPERTY::WRITE) {
    properties |= DIRCON_CHAR_PROP_FLAG_WRITE;
  }

  if (characteristicProperties & NIMBLE_PROPERTY::NOTIFY) {
    properties |= DIRCON_CHAR_PROP_FLAG_NOTIFY;
  }

  return properties;
}

size_t DirConManager::charSubscriptionIndex(const NimBLEUUID& characteristicUuid) {
  // Simple hash function to get an index for the characteristic
  // In a real implementation, this would be a proper data structure
  std::string uuidStr = characteristicUuid.toString();
  uint32_t hash       = 0;

  for (char c : uuidStr) {
    hash = ((hash << 5) + hash) + c;
  }

  return hash % DIRCON_MAX_CHARACTERISTICS;
}

void DirConManager::addSubscription(size_t clientIndex, const NimBLEUUID& characteristicUuid) {
  size_t index                            = charSubscriptionIndex(characteristicUuid);
  clientSubscriptions[clientIndex][index] = true;
  SS2K_LOG(DIRCON_LOG_TAG, "Client %d subscribed to characteristic %s", clientIndex, characteristicUuid.toString().c_str());
}

void DirConManager::removeSubscription(size_t clientIndex, const NimBLEUUID& characteristicUuid) {
  size_t index                            = charSubscriptionIndex(characteristicUuid);
  clientSubscriptions[clientIndex][index] = false;
  SS2K_LOG(DIRCON_LOG_TAG, "Client %d unsubscribed from characteristic %s", clientIndex, characteristicUuid.toString().c_str());
}

void DirConManager::removeAllSubscriptions(size_t clientIndex) {
  for (int i = 0; i < DIRCON_MAX_CHARACTERISTICS; i++) {
    clientSubscriptions[clientIndex][i] = false;
  }
  SS2K_LOG(DIRCON_LOG_TAG, "Removed all subscriptions for client %d", clientIndex);
}

bool DirConManager::hasSubscription(size_t clientIndex, const NimBLEUUID& characteristicUuid) {
  size_t index = charSubscriptionIndex(characteristicUuid);
  return clientSubscriptions[clientIndex][index];
}
