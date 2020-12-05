// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive
// 

#ifndef SETTING_H
#define SETTINGS_H

//Current program version info. Used for auto updates
#define FIRMWARE_VERSION "0.0.12.5"

//Update firmware on boot?
#define AUTO_FIRMWARE_UPDATE true

//Default Bluetooth WiFi and MDNS Name
#define DEVICE_NAME "SmartSpin2K"

//Default WiFi Password
#define DEFAULT_PASSWORD "password"

//default URL To get Updates From
#define FW_UPDATEURL "https://raw.githubusercontent.com/doudar/OTAUpdates/main/"

//File that contains Version info
#define FW_VERSIONFILE "version.txt"

//Path to the latest Firmware
#define FW_BINFILE "firmware.bin"

//Path to the latest filesystem
#define FW_SPIFFSFILE "spiffs.bin"

//name of local file to save configuration in SPIFFS
#define configFILENAME "/config.txt"

//Default Stepper Power
#define STEPPER_POWER 1000

//Hardware pin for rocker Switch 
#define RADIO_PIN 27

//Hardware pin for Shift Up
#define SHIFT_UP_PIN 19

//Hardware pin for Shift Down
#define SHIFT_DOWN_PIN 18

//Hardware pin for stepper Enable 
#define ENABLE_PIN 13

//Hardware pin for stepper step 
#define STEP_PIN 25

//Hardware pin for stepper dir
#define DIR_PIN 33

// TMC2208/TMC2224 SoftwareSerial receive pin
#define STEPPERSERIAL_RX 14 

// TMC2208/TMC2224 SoftwareSerial transmit pin
#define STEPPERSERIAL_TX 12 

// TMC2208/TMC2224 HardwareSerial port
#define SERIAL_PORT stepperSerial 

// Match to your driver
// SilentStepStick series use 0.11
// UltiMachine Einsy and Archim2 boards use 0.2
// Panucatt BSD2660 uses 0.1
// Watterott TMC5160 uses 0.075
#define R_SENSE 0.11f 

//Hardware pin for indicator LED *note* internal LED on esp32 Dev board is pin 2 
#define LED_PIN 2 

//Max tries that BLE client will perform on reconnect
#define MAX_RECONNECT_TRIES 10

//loop speed for the SmartSpin2k BLE Server
#define BLE_NOTIFY_DELAY 500

//loop speed for the SmartSpin2k BLE Client reconnect 
#define BLE_CLIENT_DELAY 1000




#endif