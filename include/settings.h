// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive
// 

#ifndef SETTING_H
#define SETTINGS_H

//const String FirmwareVer = {"0.0.11.11"};
#define FIRMWARE_VERSION "0.0.11.11"

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

//Hardware pin for rocker Switch 
#define RADIO_PIN 27

//Hardware pin for Shift Up
#define SHIFT_UP_PIN 19

//Hardware pin for Shift Down
#define SHIFT_DOWN_PIN 18

//Hardware pin for stepper Enable 
#define ENABLE_PIN 5

//Hardware pin for stepper step 
#define STEP_PIN 17

//Hardware pin for stepper dir
#define DIR_PIN 16

//Hardware pin for indicator LED *note* internal LED on esp32 Dev board is pin 2 
#define LED_PIN 2 



#endif