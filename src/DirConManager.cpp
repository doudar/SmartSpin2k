/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "DirConManager.h"
#include "SS2KLog.h"

#define DIRCON_LOG_TAG "DirConManager"

// Static member initialization
bool DirConManager::started = false;
String DirConManager::statusMessage = "";
WiFiClient DirConManager::dirConClients[DIRCON_MAX_CLIENTS];
// Static variable to track BLE service UUIDs
static String currentBleServiceUuids = "";

bool DirConManager::start() {
    if (!started) {
        setupMDNS();
        started = true;
        statusMessage = "DirCon service started";
        SS2K_LOG(DIRCON_LOG_TAG, "%s", statusMessage.c_str());
        return true;
    }
    return false;
}

void DirConManager::stop() {
    started = false;
    statusMessage = "DirCon service stopped";
    SS2K_LOG(DIRCON_LOG_TAG, "%s", statusMessage.c_str());
}

void DirConManager::update() {
    // Will be implemented in future phases
}

void DirConManager::handleData() {
    // Will be implemented in future phases
}

String DirConManager::getStatusMessage() {
    return statusMessage;
}

void DirConManager::setupMDNS() {
    // Get device MAC address
    String macAddress = WiFi.macAddress();
    // Create a unique serial number (using MAC address)
    String serialNumber = "SS2K-" + macAddress;
    serialNumber.replace(":", "");
    
    // Add DirCon service to MDNS
    SS2K_LOG(DIRCON_LOG_TAG, "Adding DirCon MDNS service: %s.%s on port %d",
             DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, DIRCON_TCP_PORT);
             
    if (MDNS.addService(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, DIRCON_TCP_PORT)) {
        SS2K_LOG(DIRCON_LOG_TAG, "Successfully added MDNS service");
    } else {
        SS2K_LOG(DIRCON_LOG_TAG, "Failed to add MDNS service");
    }
    
    // Add required text records for the DirCon protocol
    MDNS.addServiceTxt(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, "mac-address", macAddress.c_str());
    MDNS.addServiceTxt(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, "serial-number", serialNumber.c_str());
    
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
    std::string uuidStr = uuid.to16().toString();
    String shortUuidString = String(uuidStr.c_str());
    
    // Check if UUID is already in the list
    if (currentBleServiceUuids.indexOf(shortUuidString) >= 0) {
        // UUID already added
        return;
    }
    
    // Add UUID to comma-separated list
    if (currentBleServiceUuids.length() > 0) {
        currentBleServiceUuids += "," + shortUuidString;
    } else {
        currentBleServiceUuids = shortUuidString;
    }
    
    // Update the MDNS service TXT record with the updated BLE service UUIDs
    SS2K_LOG(DIRCON_LOG_TAG, "Adding BLE service UUID %s to DirCon MDNS", shortUuidString.c_str());
    MDNS.addServiceTxt(DIRCON_MDNS_SERVICE_NAME, DIRCON_MDNS_SERVICE_PROTOCOL, "ble-service-uuids", currentBleServiceUuids.c_str());
}