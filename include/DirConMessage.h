/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef DIRCONMESSAGE_H
#define DIRCONMESSAGE_H

#include <Arduino.h>
#include <NimBLEDevice.h>

#define DIRCON_MESSAGE_HEADER_LENGTH 6

// DirCon protocol message types
#define DIRCON_CHAR_PROP_FLAG_READ 0x01
#define DIRCON_CHAR_PROP_FLAG_WRITE 0x02
#define DIRCON_CHAR_PROP_FLAG_NOTIFY 0x04
#define DPKT_CHAR_PROP_FLAG_INDICATE 0x08
#define DIRCON_MSGID_ERROR 0xFF
#define DIRCON_MSGID_DISCOVER_SERVICES 0x01
#define DIRCON_MSGID_DISCOVER_CHARACTERISTICS 0x02
#define DIRCON_MSGID_READ_CHARACTERISTIC 0x03
#define DIRCON_MSGID_WRITE_CHARACTERISTIC 0x04
#define DIRCON_MSGID_ENABLE_CHARACTERISTIC_NOTIFICATIONS 0x05
#define DIRCON_MSGID_UNSOLICITED_CHARACTERISTIC_NOTIFICATION 0x06
#define DIRCON_MSGID_UNKNOWN 0x07

// DirCon protocol response codes
#define DIRCON_RESPCODE_SUCCESS_REQUEST 0x00
#define DIRCON_RESPCODE_UNKNOWN_MESSAGE_TYPE 0x01
#define DIRCON_RESPCODE_UNEXPECTED_ERROR 0x02
#define DIRCON_RESPCODE_SERVICE_NOT_FOUND 0x03
#define DIRCON_RESPCODE_CHARACTERISTIC_NOT_FOUND 0x04
#define DIRCON_RESPCODE_CHARACTERISTIC_OPERATION_NOT_SUPPORTED 0x05
#define DIRCON_RESPCODE_CHARACTERISTIC_WRITE_FAILED 0x06
#define DIRCON_RESPCODE_UNKNOWN_PROTOCOL 0x07

class DirConMessage {
public:
    DirConMessage();
    
    uint8_t MessageVersion = 1;
    uint8_t Identifier = DIRCON_MSGID_ERROR;
    uint8_t SequenceNumber = 0;
    uint8_t ResponseCode = DIRCON_RESPCODE_SUCCESS_REQUEST;
    uint16_t Length = 0;
    NimBLEUUID UUID;
    std::vector<NimBLEUUID> AdditionalUUIDs;
    std::vector<uint8_t> AdditionalData;
    bool Request = false;
    
    // Encode message for sending to client
    std::vector<uint8_t>* encode(uint8_t sequenceNumber);
    
    // Parse received message data
    size_t parse(uint8_t* data, size_t len, uint8_t sequenceNumber);

    static void printVectorBytesToSerial(const std::vector<uint8_t>& data, bool isIncoming);

private:
    bool isRequest(int last_seq_number);
    std::vector<uint8_t> encodedMessage;
};

#endif // DIRCONMESSAGE_H