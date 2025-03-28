/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <NimBLEDevice.h>
#include "BLE_Common.h"

// See https://github.com/JanDeVisser/stages-monitor/tree/master/sb20display
#define SB20_SERVICE_UUID        "0c46beaf-9c22-48ff-ae0e-c6eae1a2f4e5"
#define SB20_CHARACTERISTIC_UUID "0c46beb0-9c22-48ff-ae0e-c6eae1a2f4e5"

struct SB20Data {
    uint8_t gear;       // Calculated Gear (1-22)
    uint16_t cadence;    // Cadence in RPM
    uint16_t power;      // Power in Watts
    uint16_t heartrate;   // Heart Rate in BPM
    uint8_t battery;    // always 4
};

class BLE_SB20_Service {
public:
    BLE_SB20_Service();

    void begin();
    void setData(SB20Data data);
    void notify();

private:
    BLEService *pService;
    BLECharacteristic *pCharacteristic;
    SB20Data currentData;
    bool deviceConnected;
    
};
