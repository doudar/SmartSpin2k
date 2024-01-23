/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

// custom characteristic codes
#define BLE_firmwareUpdateURL     0x01
#define BLE_incline               0x02
#define BLE_simulatedWatts        0x03
#define BLE_simulatedHr           0x04
#define BLE_simulatedCad          0x05
#define BLE_simulatedSpeed        0x06
#define BLE_deviceName            0x07
#define BLE_shiftStep             0x08
#define BLE_stepperPower          0x09
#define BLE_stealthChop           0x0A
#define BLE_inclineMultiplier     0x0B
#define BLE_powerCorrectionFactor 0x0C
#define BLE_simulateHr            0x0D
#define BLE_simulateWatts         0x0E
#define BLE_simulateCad           0x0F
#define BLE_FTMSMode              0x10
#define BLE_autoUpdate            0x11
#define BLE_ssid                  0x12
#define BLE_password              0x13
#define BLE_foundDevices          0x14
#define BLE_connectedPowerMeter   0x15
#define BLE_connectedHeartMonitor 0x16
#define BLE_shifterPosition       0x17
#define BLE_saveToLittleFS        0x18
#define BLE_targetPosition        0x19
#define BLE_externalControl       0x1A
#define BLE_syncMode              0x1B
#define BLE_reboot                0x1C
#define BLE_resetToDefaults       0x1D
#define BLE_stepperSpeed          0x1E