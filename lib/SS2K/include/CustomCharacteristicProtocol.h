/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <stdint.h>

// SmartSpin2k custom-characteristic operations and result codes.
static const uint8_t cc_read    = 0x01;
static const uint8_t cc_write   = 0x02;
static const uint8_t cc_success = 0x80;
static const uint8_t cc_error   = 0xFF;

// Custom-characteristic variable identifiers. Keep these contiguous so tests can
// verify that every protocol value has an explicitly documented wire format.
static const uint8_t BLE_firmwareUpdateURL     = 0x01;
static const uint8_t BLE_incline               = 0x02;
static const uint8_t BLE_simulatedWatts        = 0x03;
static const uint8_t BLE_simulatedHr           = 0x04;
static const uint8_t BLE_simulatedCad          = 0x05;
static const uint8_t BLE_simulatedSpeed        = 0x06;
static const uint8_t BLE_deviceName            = 0x07;
static const uint8_t BLE_shiftStep             = 0x08;
static const uint8_t BLE_stepperPower          = 0x09;
static const uint8_t BLE_stealthChop           = 0x0A;
static const uint8_t BLE_inclineMultiplier     = 0x0B;
static const uint8_t BLE_powerCorrectionFactor = 0x0C;
static const uint8_t BLE_simulateHr            = 0x0D;
static const uint8_t BLE_simulateWatts         = 0x0E;
static const uint8_t BLE_simulateCad           = 0x0F;
static const uint8_t BLE_FTMSMode              = 0x10;
static const uint8_t BLE_autoUpdate            = 0x11;
static const uint8_t BLE_ssid                  = 0x12;
static const uint8_t BLE_password              = 0x13;
static const uint8_t BLE_foundDevices          = 0x14;
static const uint8_t BLE_connectedPowerMeter   = 0x15;
static const uint8_t BLE_connectedHeartMonitor = 0x16;
static const uint8_t BLE_shifterPosition       = 0x17;
static const uint8_t BLE_saveToLittleFS        = 0x18;
static const uint8_t BLE_targetPosition        = 0x19;
static const uint8_t BLE_externalControl       = 0x1A;
static const uint8_t BLE_syncMode              = 0x1B;
static const uint8_t BLE_reboot                = 0x1C;
static const uint8_t BLE_resetToDefaults       = 0x1D;
static const uint8_t BLE_stepperSpeed          = 0x1E;
static const uint8_t BLE_ERGSensitivity        = 0x1F;
static const uint8_t BLE_shiftDir              = 0x20;
static const uint8_t BLE_minBrakeWatts         = 0x21;
static const uint8_t BLE_maxBrakeWatts         = 0x22;
static const uint8_t BLE_restartBLE            = 0x23;
static const uint8_t BLE_scanBLE               = 0x24;
static const uint8_t BLE_firmwareVer            = 0x25;
static const uint8_t BLE_resetPowerTable       = 0x26;
static const uint8_t BLE_powerTableData        = 0x27;
static const uint8_t BLE_simulatedTargetWatts  = 0x28;
static const uint8_t BLE_simulateTargetWatts   = 0x29;
static const uint8_t BLE_hMin                  = 0x2A;
static const uint8_t BLE_hMax                  = 0x2B;
static const uint8_t BLE_homingSensitivity     = 0x2C;
static const uint8_t BLE_pTab4Pwr              = 0x2D;
static const uint8_t BLE_UDPLogging            = 0x2E;
static const uint8_t BLE_hardwareVersion       = 0x2F;
static const uint8_t BLE_BLELogging            = 0x30;
static const uint8_t BLE_allSettings           = 0x31;

enum CustomCharacteristicValueFormat : uint8_t {
  CustomAction,
  CustomBoolean,
  CustomUnsigned16,
  CustomSigned16,
  CustomSigned32,
  CustomString,
  CustomPowerTableRow,
  CustomSettingsSnapshot,
  CustomBooleanWriteStringRead,
  CustomUnknown
};

inline CustomCharacteristicValueFormat customCharacteristicValueFormat(uint8_t id) {
  switch (id) {
    case BLE_firmwareUpdateURL:
    case BLE_deviceName:
    case BLE_ssid:
    case BLE_password:
    case BLE_foundDevices:
    case BLE_connectedPowerMeter:
    case BLE_connectedHeartMonitor:
    case BLE_firmwareVer:
    case BLE_hardwareVersion: return CustomString;

    case BLE_incline:
    case BLE_inclineMultiplier:
    case BLE_shifterPosition: return CustomSigned16;

    case BLE_simulatedWatts:
    case BLE_simulatedHr:
    case BLE_simulatedCad:
    case BLE_simulatedSpeed:
    case BLE_shiftStep:
    case BLE_stepperPower:
    case BLE_powerCorrectionFactor:
    case BLE_FTMSMode:
    case BLE_stepperSpeed:
    case BLE_ERGSensitivity:
    case BLE_minBrakeWatts:
    case BLE_maxBrakeWatts:
    case BLE_simulatedTargetWatts:
    case BLE_homingSensitivity: return CustomUnsigned16;

    case BLE_stealthChop:
    case BLE_simulateHr:
    case BLE_simulateWatts:
    case BLE_simulateCad:
    case BLE_autoUpdate:
    case BLE_externalControl:
    case BLE_syncMode:
    case BLE_shiftDir:
    case BLE_simulateTargetWatts:
    case BLE_pTab4Pwr:
    case BLE_UDPLogging: return CustomBoolean;

    case BLE_targetPosition:
    case BLE_hMin:
    case BLE_hMax: return CustomSigned32;

    case BLE_saveToLittleFS:
    case BLE_reboot:
    case BLE_resetToDefaults:
    case BLE_restartBLE:
    case BLE_scanBLE:
    case BLE_resetPowerTable: return CustomAction;

    case BLE_powerTableData: return CustomPowerTableRow;
    case BLE_BLELogging: return CustomBooleanWriteStringRead;
    case BLE_allSettings: return CustomSettingsSnapshot;
    default: return CustomUnknown;
  }
}

