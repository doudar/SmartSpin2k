// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive
// 

#pragma once

//Update firmware on boot?
#define AUTO_FIRMWARE_UPDATE true

//Default Bluetooth WiFi and MDNS Name
#define DEVICE_NAME "SmartSpin2K"

//Default WiFi Password
#define DEFAULT_PASSWORD "password"

//default URL To get Updates From.
//If changed you'll also need to get a root certificate from the new server
//and put it in /include/cert.h
#define FW_UPDATEURL "https://raw.githubusercontent.com/doudar/OTAUpdates/main/"

//File that contains Version info
#define FW_VERSIONFILE "version.txt"

//Path to the latest Firmware
#define FW_BINFILE "firmware.bin"

//Path to the latest filesystem
#define FW_SPIFFSFILE "spiffs.bin"

//name of local file to save configuration in SPIFFS
#define configFILENAME "/config.txt"

//name of local file to save Physical Working Capacity in Spiffs
#define userPWCFILENAME "/userPWC.txt"

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

//When quick reconnect fails ^^, we try to scan for the disconnected server. 
//Scans slow BLE & WiFi traffic so we don't want to do this forever. 
//Give up scanning for the lost connection after this many tries: 
#define MAX_SCAN_RETRIES 1

//loop speed for the SmartSpin2k BLE communications
#define BLE_NOTIFY_DELAY 999

//loop speed for the SmartSpin2k BLE Client reconnect 
#define BLE_CLIENT_DELAY 1000

//Number of devices that can be connected to the Client (myBLEDevices size)
#define NUM_BLE_DEVICES 4

//Number of NIMBLE Devices (Including Clients)
//#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 5

//loop speed for the Webserver
#define WEBSERVER_DELAY 30

//Name of default Power Meter. any connects to anything, none connects to nothing.
#define CONNECTED_POWER_METER "any"

//Name of default heart monitor. any connects to anything, none connects to nothing.
#define CONNECTED_HEART_MONITOR "any"

//number of main loops the shifters need to be held before a BLE scan is initiated. 
#define SHIFTERS_HOLD_FOR_SCAN 2

//stealthchop enabled by default
#define STEALTHCHOP true

//how long to try STA mode before falling back to AP mode
#define WIFI_CONNECT_TIMEOUT 10

//Max size of userconfig
#define USERCONFIG_JSON_SIZE 768

//Uncomment to enable sending Telegram debug messages back to the chat specified in telegram_token.h
#define USE_TELEGRAM

//Uncomment to enable stack size debugging info
//#define DEBUG_STACK

#ifdef USE_TELEGRAM
    //Max number of telegram messages to send per session
    #define MAX_TELEGRAM_MESSAGES 5
    //Filler definitions for if telegram_token.h is not included (because it has sensitive information). 
    //Do not change these as this file is tracked and therefore public. Enter your own Telegram info into telegram_token.h   
    #if __has_include("telegram_token.h")
        #include "telegram_token.h"
    #else
        #define TELEGRAM_TOKEN "1234567890:ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghi"
        #define TELEGRAM_CHAT_ID "1234567890"                  
    #endif
#endif
