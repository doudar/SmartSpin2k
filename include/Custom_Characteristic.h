/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

// custom characteristic codes
#define BLE_firmwareUpdateURL     0x01 // URL used to update firmware
#define BLE_incline               0x02 // target incline like
#define BLE_simulatedWatts        0x03 // current watts
#define BLE_simulatedHr           0x04 // current hr
#define BLE_simulatedCad          0x05 // currend cad
#define BLE_simulatedSpeed        0x06 // current speed
#define BLE_deviceName            0x07 // Name of SmartSpin2k (Changes BLE name and URL for local access)
#define BLE_shiftStep             0x08 // Amount of stepper steps for a shift
#define BLE_stepperPower          0x09 // Stepper power in ma. Capped at 2000
#define BLE_stealthChop           0x0A // Stepper StealthChop (Makes it quieter with a little torque sacraficed)
#define BLE_inclineMultiplier     0x0B // Incline * this = steps to move. 3.0 is a good starting vlue for most bikes.
#define BLE_powerCorrectionFactor 0x0C // Correction factor for FTMS and CPS connected devices. .1-2.0
#define BLE_simulateHr            0x0D // If set to 1, override connected HR and use simulated above.
#define BLE_simulateWatts         0x0E // "" for Power Meter
#define BLE_simulateCad           0x0F // "" for Cad
#define BLE_FTMSMode              0x10 // get or set FTMS mode using values such as FitnessMachineControlPointProcedure::SetTargetPower
#define BLE_autoUpdate            0x11 // Attempt to update firmware on reboot?
#define BLE_ssid                  0x12 // WiFi SSID. If it's not a network in range, fallback to AP mode made with devicename and "password"
#define BLE_password              0x13 // WiFi Password for known network. Fallback to "password" if network above not in range
#define BLE_foundDevices          0x14 // BLE fitness devices in range
#define BLE_connectedPowerMeter   0x15 // Desired Power Meter
#define BLE_connectedHeartMonitor 0x16 // Desired HR Monitor
#define BLE_shifterPosition       0x17 // Current Shifter Position
#define BLE_saveToLittleFS        0x18 // Write to make settings Permanent
#define BLE_targetPosition        0x19 // The target position of the stepper motor
#define BLE_externalControl       0x1A // Stop all internal control of stepper motor - let external device control motor
#define BLE_syncMode              0x1B // Stop stepper motor and set the current position to the target position 
#define BLE_reboot                0x1C // Reboots the SmartSpin2k
#define BLE_resetToDefaults       0x1D // Resets the SmartSpin2k to defaults - Settings only, not the filesystem
#define BLE_stepperSpeed          0x1E // Stepper motor speed. Use cautiously. numbers above 3000 seem to cause a crash currently
#define BLE_ERGSensitivity        0x1F // Agressiveness of the control loop for ERG mode
#define BLE_shiftDir              0x20 // Flip flops the hardwired shifter direction
#define BLE_minBrakeWatts         0x21 // Minimum watts @ 90rpm . Used to set software end stop so motor doesn't crash and lose steps.
#define BLE_maxBrakeWatts         0x22 // "" Maximum watts for the other direction
#define BLE_restartBLE            0x23 // Closes all connections to the BLE client - used to connect new BLE devices the user selects.
#define BLE_scanBLE               0x24 // Scan for new BLE devices
#define BLE_firmwareVer           0x25 // String of the current firmware version
