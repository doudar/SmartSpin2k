/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <NimBLEDevice.h>
#include "BLE_Common.h"

// Codes
const uint8_t cc_read    = 0x01;  // value to request read operation
const uint8_t cc_write   = 0x02;  // Value to request write operation
const uint8_t cc_error   = 0xff;  // value server error/unable
const uint8_t cc_success = 0x80;  // value for success

// custom characteristic codes
const uint8_t BLE_firmwareUpdateURL     = 0x01;  // URL used to update firmware
const uint8_t BLE_incline               = 0x02;  // target incline like
const uint8_t BLE_simulatedWatts        = 0x03;  // current watts
const uint8_t BLE_simulatedHr           = 0x04;  // current hr
const uint8_t BLE_simulatedCad          = 0x05;  // current cad
const uint8_t BLE_simulatedSpeed        = 0x06;  // current speed
const uint8_t BLE_deviceName            = 0x07;  // Name of SmartSpin2k (Changes BLE name and URL for local access)
const uint8_t BLE_shiftStep             = 0x08;  // Amount of stepper steps for a shift
const uint8_t BLE_stepperPower          = 0x09;  // Stepper power in ma. Capped at 2000
const uint8_t BLE_stealthChop           = 0x0A;  // Stepper StealthChop (Makes it quieter with a little torque sacrificed)
const uint8_t BLE_inclineMultiplier     = 0x0B;  // Incline * this = steps to move. 3.0 is a good starting value for most bikes.
const uint8_t BLE_powerCorrectionFactor = 0x0C;  // Correction factor for FTMS and CPS connected devices. .1-2.0
const uint8_t BLE_simulateHr            = 0x0D;  // If set to 1, override connected HR and use simulated above.
const uint8_t BLE_simulateWatts         = 0x0E;  // Are we sending watts
const uint8_t BLE_simulateCad           = 0x0F;  // Are we sending cad
const uint8_t BLE_FTMSMode              = 0x10;  // get or set FTMS mode using values such as FitnessMachineControlPointProcedure::SetTargetPower
const uint8_t BLE_autoUpdate            = 0x11;  // Attempt to update firmware on reboot?
const uint8_t BLE_ssid                  = 0x12;  // WiFi SSID. If it's not a network in range, fallback to AP mode made with devicename and "password"
const uint8_t BLE_password              = 0x13;  // WiFi Password for known network. Fallback to "password" if network above not in range
const uint8_t BLE_foundDevices          = 0x14;  // BLE fitness devices in range
const uint8_t BLE_connectedPowerMeter   = 0x15;  // Desired Power Meter
const uint8_t BLE_connectedHeartMonitor = 0x16;  // Desired HR Monitor
const uint8_t BLE_shifterPosition       = 0x17;  // Current Shifter Position
const uint8_t BLE_saveToLittleFS        = 0x18;  // Write to make settings Permanent
const uint8_t BLE_targetPosition        = 0x19;  // The target position of the stepper motor
const uint8_t BLE_externalControl       = 0x1A;  // Stop all internal control of stepper motor - let external device control motor
const uint8_t BLE_syncMode              = 0x1B;  // Stop stepper motor and set the current position to the target position
const uint8_t BLE_reboot                = 0x1C;  // Reboots the SmartSpin2k
const uint8_t BLE_resetToDefaults       = 0x1D;  // Resets the SmartSpin2k to defaults - Settings only, not the filesystem
const uint8_t BLE_stepperSpeed          = 0x1E;  // Stepper motor speed. Use cautiously. numbers above 3500 increase crashiness
const uint8_t BLE_ERGSensitivity        = 0x1F;  // Aggressiveness of the control loop for ERG mode
const uint8_t BLE_shiftDir              = 0x20;  // Flip flops the hardwired shifter direction
const uint8_t BLE_minBrakeWatts         = 0x21;  // Minimum watts @ 90rpm . Used to set software end stop so motor doesn't crash and lose steps.
const uint8_t BLE_maxBrakeWatts         = 0x22;  // "" Maximum watts for the other direction
const uint8_t BLE_restartBLE            = 0x23;  // Closes all connections to the BLE client - used to connect new BLE devices the user selects.
const uint8_t BLE_scanBLE               = 0x24;  // Scan for new BLE devices
const uint8_t BLE_firmwareVer           = 0x25;  // String of the current firmware version
const uint8_t BLE_resetPowerTable       = 0x26;  // Delete all power table information.
const uint8_t BLE_powerTableData        = 0x27;  // sets or requests power table data
const uint8_t BLE_simulatedTargetWatts  = 0x28;  // current target watts
const uint8_t BLE_simulateTargetWatts   = 0x29;  // are we sending target watts
const uint8_t BLE_hMin                  = 0x2A;  // Minimum homing value
const uint8_t BLE_hMax                  = 0x2B;  // Maximum homing value
const uint8_t BLE_homingSensitivity     = 0x2C;  // Homing sensitivity value
const uint8_t BLE_pTab4Pwr              = 0x2D;  // Use power values for power table

class BLE_ss2kCustomCharacteristic {
 public:
  void setupService(NimBLEServer *pServer);
  void update();
  // Used internally for notify and onWrite Callback.
  static void process(std::string rxValue);
  // Custom Characteristic value that needs to be notified
  static void notify(char _item, int tableRow = -1);
  // Notify any changed value in userConfig
  static void parseNemit();

 private:
  NimBLEService *pSmartSpin2kService;
  NimBLECharacteristic *smartSpin2kCharacteristic;
  uint8_t ss2kCustomCharacteristicValue[3] = {0x00, 0x00, 0x00};
};

class ss2kCustomCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;
  void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override;
  void onStatus(NimBLECharacteristic* pCharacteristic, int code) override;
};
