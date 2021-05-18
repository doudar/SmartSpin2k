/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <NimBLEUUID.h>

// Heart Service
#define HEARTSERVICE_UUID        NimBLEUUID((uint16_t)0x180D)
#define HEARTCHARACTERISTIC_UUID NimBLEUUID((uint16_t)0x2A37)

// Cycling Power Service
#define CSCSERVICE_UUID              NimBLEUUID((uint16_t)0x1816)
#define CSCMEASUREMENT_UUID          NimBLEUUID((uint16_t)0x2A5B)
#define CYCLINGPOWERSERVICE_UUID     NimBLEUUID((uint16_t)0x1818)
#define CYCLINGPOWERMEASUREMENT_UUID NimBLEUUID((uint16_t)0x2A63)
#define CYCLINGPOWERFEATURE_UUID     NimBLEUUID((uint16_t)0x2A65)
#define SENSORLOCATION_UUID          NimBLEUUID((uint16_t)0x2A5D)

// Fitness Machine Service
#define FITNESSMACHINESERVICE_UUID              NimBLEUUID((uint16_t)0x1826)
#define FITNESSMACHINEFEATURE_UUID              NimBLEUUID((uint16_t)0x2ACC)
#define FITNESSMACHINECONTROLPOINT_UUID         NimBLEUUID((uint16_t)0x2AD9)
#define FITNESSMACHINESTATUS_UUID               NimBLEUUID((uint16_t)0x2ADA)
#define FITNESSMACHINEINDOORBIKEDATA_UUID       NimBLEUUID((uint16_t)0x2AD2)
#define FITNESSMACHINERESISTANCELEVELRANGE_UUID NimBLEUUID((uint16_t)0x2AD6)
#define FITNESSMACHINEPOWERRANGE_UUID           NimBLEUUID((uint16_t)0x2AD8)

// GATT service/characteristic UUIDs for Flywheel Bike from ptx2/gymnasticon/
#define FLYWHEEL_UART_SERVICE_UUID NimBLEUUID("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
#define FLYWHEEL_UART_RX_UUID      NimBLEUUID("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
#define FLYWHEEL_UART_TX_UUID      NimBLEUUID("6e400003-b5a3-f393-e0a9-e50e24dcca9e")
#define FLYWHEEL_BLE_NAME          "Flywheel 1"

// The Echelon Services
#define ECHELON_DEVICE_UUID  NimBLEUUID("0bf669f0-45f2-11e7-9598-0800200c9a66")
#define ECHELON_SERVICE_UUID NimBLEUUID("0bf669f1-45f2-11e7-9598-0800200c9a66")
#define ECHELON_WRITE_UUID   NimBLEUUID("0bf669f2-45f2-11e7-9598-0800200c9a66")
#define ECHELON_DATA_UUID    NimBLEUUID("0bf669f4-45f2-11e7-9598-0800200c9a66")
