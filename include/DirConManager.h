/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef DIRCONMANAGER_H
#define DIRCONMANAGER_H

#include "Main.h"
#include "BLE_Common.h"
#include <WiFi.h>
#include <ESPmDNS.h>

// DirCon protocol definitions
#define DIRCON_MDNS_SERVICE_NAME "_wahoo-fitness-tnp"
#define DIRCON_MDNS_SERVICE_PROTOCOL "tcp"
#define DIRCON_TCP_PORT 8080
#define DIRCON_MAX_CLIENTS 3

class DirConManager {
public:
    static bool start();
    static void stop();
    static void update();
    static void handleData();
    static String getStatusMessage();
    
    // Add a BLE service UUID to DirCon MDNS service
    static void addBleServiceUuid(const NimBLEUUID& serviceUuid);

private:
    static bool started;
    static String statusMessage;
    static WiFiClient dirConClients[DIRCON_MAX_CLIENTS];
    static void setupMDNS();
};

#endif // DIRCONMANAGER_H