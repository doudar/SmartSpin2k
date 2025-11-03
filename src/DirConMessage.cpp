/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "DirConMessage.h"
#include "SS2KLog.h"
#include "settings.h"
#include "BLE_Common.h"

#define DIRCON_LOG_TAG "DirConMessage"

// Helper functions to print raw bytes to serial monitor
#ifdef DEBUG_DIRCON_MESSAGES
void printRawBytesToSerial(const uint8_t* data, size_t length, bool isIncoming) {
  String direction = isIncoming ? "RECEIVED" : "SENDING";
  Serial.print("[DIRCON ");
  Serial.print(direction);
  Serial.print("] Raw bytes[");
  Serial.print(length);
  Serial.print("]: ");

  for (size_t i = 0; i < length; i++) {
    char hexByte[4];
    snprintf(hexByte, sizeof(hexByte), "%02X ", data[i]);
    Serial.print(hexByte);
  }

  Serial.println();
}

void DirConMessage::printVectorBytesToSerial(const std::vector<uint8_t>& data, bool isIncoming) {
  if (data.size() > 0) {
    printRawBytesToSerial(data.data(), data.size(), isIncoming);
  }
}
#endif

// Helper functions for UUID conversion - matching the expected DirCon protocol format
void uuidToBytes(NimBLEUUID& uuid, std::vector<uint8_t>& message) {
  // Log the UUID being processed for debugging
  //SS2K_LOG(DIRCON_LOG_TAG, "Processing UUID: %s", uuid.toString().c_str());

  uint8_t *uuidBytes = (uint8_t*)uuid.to128().getBase();

  // Add the bytes to the message
  for(size_t i = 16; i > 0; i--) {
    message.push_back(uuidBytes[i]);
  }
}

NimBLEUUID bytesToUuid(uint8_t* data, size_t offset) {
  uint8_t* ptr = data + offset;

  NimBLEUUID uUidOut(ptr, 16);
  NimBLEUUID reversed = uUidOut.reverseByteOrder();

  //SS2K_LOG(DIRCON_LOG_TAG, "Derived UUID: %s", reversed.toString().c_str());

  return reversed;
}

DirConMessage::DirConMessage() {}

std::vector<uint8_t>* DirConMessage::encode(uint8_t sequenceNumber) {
  if (this->Identifier != DIRCON_MSGID_ERROR) {
    this->encodedMessage.clear();
    this->MessageVersion = 1;

    // Handle sequence number logic
    if (this->Request) {
      if (this->SequenceNumber < 255) {
        this->SequenceNumber++;
      } else {
        this->SequenceNumber = 0;
      }
    } else if (this->Identifier == DIRCON_MSGID_UNSOLICITED_CHARACTERISTIC_NOTIFICATION) {
      this->SequenceNumber = 0;
    } else {
      this->SequenceNumber = sequenceNumber;
    }

    // Add message header
    this->encodedMessage.push_back(this->MessageVersion);
    this->encodedMessage.push_back(this->Identifier);
    this->encodedMessage.push_back(this->SequenceNumber);
    this->encodedMessage.push_back(this->ResponseCode);

    // Handle error responses
    if (!this->Request && this->ResponseCode != DIRCON_RESPCODE_SUCCESS_REQUEST) {
      this->Length = 0;
      this->encodedMessage.push_back((uint8_t)(this->Length >> 8));
      this->encodedMessage.push_back((uint8_t)(this->Length));
    }
    // Handle discover services request/response
    else if (this->Identifier == DIRCON_MSGID_DISCOVER_SERVICES) {
      if (this->Request) {
        this->Length = 0;
        this->encodedMessage.push_back((uint8_t)(this->Length >> 8));
        this->encodedMessage.push_back((uint8_t)(this->Length));
      } else {
        // Calculate length - each UUID is 16 bytes
        this->Length = this->AdditionalUUIDs.size() * 16;
        this->encodedMessage.push_back((uint8_t)(this->Length >> 8));
        this->encodedMessage.push_back((uint8_t)(this->Length));

        // Debug log to show number of UUIDs being added
        SS2K_LOG(DIRCON_LOG_TAG, "Adding %d service UUIDs to discovery response", this->AdditionalUUIDs.size());

        // Add each UUID to the encoded message
        for (size_t counter = 0; counter < this->AdditionalUUIDs.size(); counter++) {
          NimBLEUUID& uuidToAdd = this->AdditionalUUIDs[counter];
          SS2K_LOG(DIRCON_LOG_TAG, "Adding service %d UUID: %s", counter, uuidToAdd.toString().c_str());

          // Add the UUID bytes to the message
          uuidToBytes(uuidToAdd, this->encodedMessage);
        }
      }
    }
    // Handle discover characteristics response
    else if (this->Identifier == DIRCON_MSGID_DISCOVER_CHARACTERISTICS && !this->Request) {
      this->Length = 16 + this->AdditionalUUIDs.size() * 17;
      this->encodedMessage.push_back((uint8_t)(this->Length >> 8));
      this->encodedMessage.push_back((uint8_t)(this->Length));
      uuidToBytes(this->UUID, this->encodedMessage);

      size_t dataIndex = 0;
      for (size_t counter = 0; counter < this->AdditionalUUIDs.size(); counter++) {
        uuidToBytes(this->AdditionalUUIDs[counter], this->encodedMessage);
        this->encodedMessage.push_back(this->AdditionalData[dataIndex]);
        dataIndex++;
      }
    }
    // Handle read characteristic request or discover characteristics request or notification response
    else if (((this->Identifier == DIRCON_MSGID_READ_CHARACTERISTIC || this->Identifier == DIRCON_MSGID_DISCOVER_CHARACTERISTICS) && this->Request) ||
             (this->Identifier == DIRCON_MSGID_ENABLE_CHARACTERISTIC_NOTIFICATIONS && !this->Request)) {
      this->Length = 16;
      this->encodedMessage.push_back((uint8_t)(this->Length >> 8));
      this->encodedMessage.push_back((uint8_t)(this->Length));
      uuidToBytes(this->UUID, this->encodedMessage);
    }
    // Handle write characteristic, unsolicited notification, read response, or notification enabling
    else if (this->Identifier == DIRCON_MSGID_WRITE_CHARACTERISTIC || this->Identifier == DIRCON_MSGID_UNSOLICITED_CHARACTERISTIC_NOTIFICATION ||
             (this->Identifier == DIRCON_MSGID_READ_CHARACTERISTIC && !this->Request) || (this->Identifier == DIRCON_MSGID_ENABLE_CHARACTERISTIC_NOTIFICATIONS && this->Request)) {
      this->Length = 16 + this->AdditionalData.size();
      this->encodedMessage.push_back((uint8_t)(this->Length >> 8));
      this->encodedMessage.push_back((uint8_t)(this->Length));
      uuidToBytes(this->UUID, this->encodedMessage);

      for (size_t counter = 0; counter < this->AdditionalData.size(); counter++) {
        this->encodedMessage.push_back(this->AdditionalData[counter]);
      }
    }
  }
  return &(this->encodedMessage);
}

size_t DirConMessage::parse(uint8_t* data, size_t len, uint8_t sequenceNumber) {
  if (len < DIRCON_MESSAGE_HEADER_LENGTH) {
    SS2K_LOG(DIRCON_LOG_TAG, "Error parsing DirCon message: Header length %d < %d", len, DIRCON_MESSAGE_HEADER_LENGTH);
    this->Identifier = DIRCON_MSGID_ERROR;
    return 0;
  }

  #ifdef DEBUG_DIRCON_MESSAGES
  printVectorBytesToSerial(std::vector<uint8_t>(data, data + len), true);
#endif

  // Parse header
  this->MessageVersion = data[0];
  this->Identifier     = data[1];
  this->SequenceNumber = data[2];
  this->ResponseCode   = data[3];
  this->Length         = (data[4] << 8) | data[5];
  this->Request        = false;
  this->UUID           = NimBLEUUID();
  this->AdditionalData.clear();
  this->AdditionalUUIDs.clear();

  if ((len - DIRCON_MESSAGE_HEADER_LENGTH) < this->Length) {
    SS2K_LOG(DIRCON_LOG_TAG, "Error parsing DirCon message: Content length %d < %d", (len - DIRCON_MESSAGE_HEADER_LENGTH), this->Length);
    this->Identifier = DIRCON_MSGID_ERROR;
    return 0;
  }

  size_t parsedBytes = 6;
  switch (this->Identifier) {
    case DIRCON_MSGID_DISCOVER_SERVICES:
    SS2K_LOG(DIRCON_LOG_TAG, "Discover services response contains %d UUIDs", this->Length / 16);
      if (!this->Length) {
        this->Request = this->isRequest(sequenceNumber);
      } else if ((this->Length % 16) == 0) {
        this->AdditionalUUIDs.clear();
        size_t index = 0;
        while (this->Length >= index + 16) {
          // Parse UUID with consistent byte order
          NimBLEUUID uuid = bytesToUuid(data + DIRCON_MESSAGE_HEADER_LENGTH, index);
          this->AdditionalUUIDs.push_back(uuid);
          index += 16;
          parsedBytes += 16;
        }
      } else {
        SS2K_LOG(DIRCON_LOG_TAG, "Error parsing DirCon message: Length %d isn't a multiple of 16", this->Length);
        this->Identifier = DIRCON_MSGID_ERROR;
        return 0;
      }
      break;

    case DIRCON_MSGID_DISCOVER_CHARACTERISTICS:
      if (this->Length >= 16) {
        // Parse the UUID in the same reversed byte format
        this->UUID = bytesToUuid(data + DIRCON_MESSAGE_HEADER_LENGTH, 0);
        parsedBytes += 16;
        if (this->Length == 16) {
          this->Request = this->isRequest(sequenceNumber);
        } else if ((this->Length - 16) % 17 == 0) {
          this->AdditionalUUIDs.clear();
          this->AdditionalData.clear();
          size_t index = 16;
          while (this->Length >= index + 17) {
            // Ensure consistent byte order for characteristic UUIDs
            NimBLEService* pService                                   = spinBLEServer.pServer->getServiceByUUID(this->UUID);
            const std::vector<NimBLECharacteristic*> pCharacteristics = pService->getCharacteristics();
            for (NimBLECharacteristic* pCharacteristic : pCharacteristics) {
              this->AdditionalUUIDs.push_back(pCharacteristic->getUUID());
              index += pCharacteristic->getUUID().toString().length();
              parsedBytes += pCharacteristic->getUUID().toString().length();
            }
          }
        }
      } else {
        SS2K_LOG(DIRCON_LOG_TAG, "Error parsing additional UUIDs and data: Length %d isn't a multiple of 17", (this->Length - 16));
        this->Identifier = DIRCON_MSGID_ERROR;
        return 0;
      }

      break;

    case DIRCON_MSGID_READ_CHARACTERISTIC:
      if (this->Length >= 16) {
        // Update READ_CHARACTERISTIC to use consistent byte ordering
        this->UUID = bytesToUuid(data + DIRCON_MESSAGE_HEADER_LENGTH, 0);
        parsedBytes += 16;
        if (this->Length == 16) {
          this->Request = this->isRequest(sequenceNumber);
        } else {
          this->AdditionalData.clear();
          for (size_t dataIndex = 0; dataIndex < (this->Length - 16); dataIndex++) {
            this->AdditionalData.push_back((uint8_t)data[DIRCON_MESSAGE_HEADER_LENGTH + dataIndex + 16]);
            parsedBytes += 1;
          }
        }
      } else {
        SS2K_LOG(DIRCON_LOG_TAG, "Error parsing DirCon message: Length %d < 16", this->Length);
        this->Identifier = DIRCON_MSGID_ERROR;
        return 0;
      }
      break;

    case DIRCON_MSGID_WRITE_CHARACTERISTIC:
      if (this->Length > 16) {
        // Update WRITE_CHARACTERISTIC UUID parsing
        this->UUID = bytesToUuid(data + DIRCON_MESSAGE_HEADER_LENGTH, 0);
        parsedBytes += 16;
        this->Request = this->isRequest(sequenceNumber);
        this->AdditionalData.clear();
        for (size_t dataIndex = 0; dataIndex < (this->Length - 16); dataIndex++) {
          this->AdditionalData.push_back((uint8_t)data[DIRCON_MESSAGE_HEADER_LENGTH + dataIndex + 16]);
          parsedBytes += 1;
        }
      } else {
        SS2K_LOG(DIRCON_LOG_TAG, "Error parsing DirCon message: Length %d < 16", this->Length);
        this->Identifier = DIRCON_MSGID_ERROR;
        return 0;
      }
      break;

    case DIRCON_MSGID_ENABLE_CHARACTERISTIC_NOTIFICATIONS:
      if (this->Length >= 16) {
        // Parse UUID
        this->UUID = bytesToUuid(data + DIRCON_MESSAGE_HEADER_LENGTH, 0);
        parsedBytes += 16;

        // Payload (if any) follows the UUID; typical CCCD is 1-2 bytes
        size_t payloadLen = this->Length - 16;
        this->AdditionalData.clear();
        if (payloadLen > 0) {
          this->Request = true;
          for (size_t i = 0; i < payloadLen; ++i) {
            this->AdditionalData.push_back((uint8_t)data[DIRCON_MESSAGE_HEADER_LENGTH + 16 + i]);
          }
          parsedBytes += payloadLen;
        } else {
          // No payload implies an ack/response
          this->Request = this->isRequest(sequenceNumber);
        }
      } else {
        SS2K_LOG(DIRCON_LOG_TAG, "Error parsing DirCon message: Length %d < 16 for enable notifications", this->Length);
        this->Identifier = DIRCON_MSGID_ERROR;
        return 0;
      }
      break;

    case DIRCON_MSGID_UNSOLICITED_CHARACTERISTIC_NOTIFICATION:
      if (this->Length > 16) {
        // Update UNSOLICITED_CHARACTERISTIC_NOTIFICATION UUID parsing
        this->UUID = bytesToUuid(data + DIRCON_MESSAGE_HEADER_LENGTH, 0);
        parsedBytes += 16;
        this->AdditionalData.clear();
        for (size_t dataIndex = 0; dataIndex < (this->Length - 16); dataIndex++) {
          this->AdditionalData.push_back((uint8_t)data[DIRCON_MESSAGE_HEADER_LENGTH + dataIndex + 16]);
          parsedBytes += 1;
        }
      } else {
        SS2K_LOG(DIRCON_LOG_TAG, "Error parsing DirCon message: Length %d < 16", this->Length);
        this->Identifier = DIRCON_MSGID_ERROR;
        return 0;
      }
      break;

    default:
      SS2K_LOG(DIRCON_LOG_TAG, "Error parsing DirCon message: Unknown identifier %d", this->Identifier);
      this->Identifier = DIRCON_MSGID_ERROR;
      return 0;
      break;
  }

  return parsedBytes;
}

bool DirConMessage::isRequest(int sequenceNumber) {
  return this->ResponseCode == DIRCON_RESPCODE_SUCCESS_REQUEST && (sequenceNumber <= 0 || sequenceNumber != this->SequenceNumber);
}