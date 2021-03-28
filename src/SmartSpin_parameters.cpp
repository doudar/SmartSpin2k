/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "SmartSpin_parameters.h"

#include <ArduinoJson.h>
#include <SPIFFS.h>

// Default Values
void userParameters::setDefaults() {  // Move these to set the values as #define
                                      // in main.h
  firmwareUpdateURL     = FW_UPDATEURL;
  incline               = 0.0;
  simulatedWatts        = 00;
  simulatedHr           = 00;
  simulatedCad          = 00;
  deviceName            = DEVICE_NAME;
  shiftStep             = 600;
  stepperPower          = STEPPER_POWER;
  stealthchop           = STEALTHCHOP;
  inclineMultiplier     = 3.0;
  doublePower           = false;
  simulateHr            = true;
  ERGMode               = false;
  autoUpdate            = AUTO_FIRMWARE_UPDATE;
  ssid                  = DEVICE_NAME;
  password              = DEFAULT_PASSWORD;
  foundDevices          = "";
  connectedPowerMeter   = "any";
  connectedHeartMonitor = "any";
}

//---------------------------------------------------------------------------------
//-- return all config as one a single JSON string
String userParameters::returnJSON() {
  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
  StaticJsonDocument<USERCONFIG_JSON_SIZE> doc;
  // Set the values in the document

  doc["firmwareUpdateURL"]     = firmwareUpdateURL;
  doc["incline"]               = incline;
  doc["simulatedWatts"]        = simulatedWatts;
  doc["simulatedHr"]           = simulatedHr;
  doc["simulatedCad"]          = simulatedCad;
  doc["deviceName"]            = deviceName;
  doc["shiftStep"]             = shiftStep;
  doc["stepperPower"]          = stepperPower;
  doc["stealthchop"]           = stealthchop;
  doc["inclineMultiplier"]     = inclineMultiplier;
  doc["doublePower"]           = doublePower;
  doc["simulateHr"]            = simulateHr;
  doc["ERGMode"]               = ERGMode;
  doc["autoUpdate"]            = autoUpdate;
  doc["ssid"]                  = ssid;
  doc["password"]              = password;
  doc["foundDevices"]          = foundDevices;
  doc["connectedPowerMeter"]   = connectedPowerMeter;
  doc["connectedHeartMonitor"] = connectedHeartMonitor;
  String output;
  serializeJson(doc, output);
  return output;
}

//-- Saves all parameters to SPIFFS
void userParameters::saveToSPIFFS() {
  // Delete existing file, otherwise the configuration is appended to the file
  SPIFFS.remove(configFILENAME);

  // Open file for writing
  debugDirector("Writing File: " + String(configFILENAME));
  File file = SPIFFS.open(configFILENAME, FILE_WRITE);
  if (!file) {
    debugDirector(F("Failed to create file"));
    return;
  }

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
  StaticJsonDocument<USERCONFIG_JSON_SIZE> doc;

  // Set the values in the document

  doc["firmwareUpdateURL"] = firmwareUpdateURL;
  doc["incline"]           = incline;
  // doc["simulatedWatts"]       = simulatedWatts;
  // doc["simulatedHr"]          = simulatedHr;
  // doc["simulatedCad"]         = simulatedCad;
  doc["deviceName"]        = deviceName;
  doc["shiftStep"]         = shiftStep;
  doc["stepperPower"]      = stepperPower;
  doc["stealthchop"]       = stealthchop;
  doc["inclineMultiplier"] = inclineMultiplier;
  doc["doublePower"]       = doublePower;
  doc["simulateHr"]        = simulateHr;
  doc["ERGMode"]           = ERGMode;
  doc["autoUpdate"]        = autoUpdate;
  doc["ssid"]              = ssid;
  doc["password"]          = password;
  // doc["foundDevices"]         = foundDevices; //I don't see a need
  // currently in keeping this boot to boot
  doc["connectedPowerMeter"]   = connectedPowerMeter;
  doc["connectedHeartMonitor"] = connectedHeartMonitor;

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    debugDirector(F("Failed to write to file"));
  }
  // Close the file
  file.close();
}

// Loads the JSON configuration from a file into a userParameters Object
void userParameters::loadFromSPIFFS() {
  // Open file for reading
  debugDirector("Reading File: " + String(configFILENAME));
  File file = SPIFFS.open(configFILENAME);

  // load defaults if filename doesn't exist
  if (!file) {
    debugDirector("Couldn't find configuration file. Loading Defaults");
    setDefaults();
    return;
  }
  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<USERCONFIG_JSON_SIZE> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    debugDirector(F("Failed to read file, using default configuration"));
    setDefaults();
    return;
  }

  // Copy values from the JsonDocument to the Config
  setFirmwareUpdateURL(doc["firmwareUpdateURL"]);
  setIncline(doc["incline"]);
  // setSimulatedWatts     (doc["simulatedWatts"]);
  // setSimulatedHr        (doc["simulatedHr"]);
  // setSimulatedCad       (doc["simulatedCad"]);
  setDeviceName(doc["deviceName"]);
  setShiftStep(doc["shiftStep"]);
  setStepperPower(doc["stepperPower"]);
  setStealthChop(doc["stealthchop"]);
  setInclineMultiplier(doc["inclineMultiplier"]);
  setDoublePower(doc["doublePower"]);
  setSimulateHr(doc["simulateHr"]);
  setERGMode(doc["ERGMode"]);
  setAutoUpdate(doc["autoUpdate"]);
  setSsid(doc["ssid"]);
  setPassword(doc["password"]);
  // setfoundDevices       (doc["foundDevices"]);
  setConnectedPowerMeter(doc["connectedPowerMeter"]);
  setConnectedHeartMonitor(doc["connectedHeartMonitor"]);

  // Incase these important variables were not in the document, set them to
  // defaults.
  if (doc["firmwareUpdateURL"] == "null") {
    firmwareUpdateURL = FW_UPDATEURL;
  }
  if (doc["deviceName"] == "null") {
    deviceName = DEVICE_NAME;
  }
  if (doc["ssid"] == "null") {
    ssid = DEVICE_NAME;
  }
  if (doc["password"] == "null") {
    password = DEFAULT_PASSWORD;
  }
  if (doc["autoUpdate"] == "null") {
    autoUpdate = AUTO_FIRMWARE_UPDATE;
  }
  if (doc["stepperPower"] == "null") {
    stepperPower = STEPPER_POWER;
  }
  if (doc["stealthchop"] == "null") {
    stealthchop = STEALTHCHOP;
  }
  if (doc["connectedPowerMeter"] == "null") {
    connectedPowerMeter = CONNECTED_POWER_METER;
  }
  if (doc["connectedHeartMonitor"] == "null") {
    connectedHeartMonitor = CONNECTED_HEART_MONITOR;
  }

  debugDirector("Config File Loaded: " + String(configFILENAME));
  file.close();
}

// Prints the content of a file to the Serial
void userParameters::printFile() {
  // Open file for reading
  debugDirector("Contents of file: " + String(configFILENAME));
  File file = SPIFFS.open(configFILENAME);
  if (!file) {
    debugDirector(F("Failed to read file"));
    return;
  }

  // Extract each characters by one by one
  while (file.available()) {
    debugDirector(String(static_cast<char>(file.read())), false);
  }
  debugDirector(String(" "));
  // Close the file
  file.close();
}

/*****************************************USERPWC*****************************************/

void physicalWorkingCapacity::setDefaults() {
  session1HR  = 129;  // examples from https://www.cyclinganalytics.com/
  session1Pwr = 100;
  session2HR  = 154;
  session2Pwr = 150;
  hr2Pwr      = true;
}

//-- return all config as one a single JSON string
String physicalWorkingCapacity::returnJSON() {
  StaticJsonDocument<500> doc;

  doc["session1HR"]  = session1HR;
  doc["session1Pwr"] = session1Pwr;
  doc["session2HR"]  = session2HR;
  doc["session2Pwr"] = session2Pwr;
  doc["hr2Pwr"]      = hr2Pwr;

  String output;
  serializeJson(doc, output);
  return output;
}

//-- Saves all parameters to SPIFFS
void physicalWorkingCapacity::saveToSPIFFS() {
  // Delete existing file, otherwise the configuration is appended to the file
  SPIFFS.remove(userPWCFILENAME);

  // Open file for writing
  debugDirector("Writing File: " + String(userPWCFILENAME));
  File file = SPIFFS.open(userPWCFILENAME, FILE_WRITE);
  if (!file) {
    debugDirector(F("Failed to create file"));
    return;
  }

  StaticJsonDocument<500> doc;

  doc["session1HR"]  = session1HR;
  doc["session1Pwr"] = session1Pwr;
  doc["session2HR"]  = session2HR;
  doc["session2Pwr"] = session2Pwr;
  doc["hr2Pwr"]      = hr2Pwr;

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    debugDirector(F("Failed to write to file"));
  }
  // Close the file
  file.close();
}

// Loads the JSON configuration from a file
void physicalWorkingCapacity::loadFromSPIFFS() {
  // Open file for reading
  debugDirector("Reading File: " + String(userPWCFILENAME));
  File file = SPIFFS.open(userPWCFILENAME);

  // load defaults if filename doesn't exist
  if (!file) {
    debugDirector("Couldn't find configuration file. Loading Defaults");
    setDefaults();
    return;
  }

  StaticJsonDocument<500> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    debugDirector(F("Failed to read file, using default configuration"));
    setDefaults();
    return;
  }

  // Copy values from the JsonDocument to the Config
  session1HR  = doc["session1HR"];
  session1Pwr = doc["session1Pwr"];
  session2HR  = doc["session2HR"];
  session2Pwr = doc["session2Pwr"];
  hr2Pwr      = doc["hr2Pwr"];

  debugDirector("Config File Loaded: " + String(userPWCFILENAME));
  file.close();
}

// Prints the content of a file to the Serial
void physicalWorkingCapacity::printFile() {
  // Open file for reading
  debugDirector("Contents of file: " + String(userPWCFILENAME));
  File file = SPIFFS.open(userPWCFILENAME);
  if (!file) {
    debugDirector(F("Failed to read file"));
    return;
  }

  // Extract each characters by one by one
  while (file.available()) {
    debugDirector(String(static_cast<char>(file.read())), false);
  }
  debugDirector(String(" "));
  // Close the file
  file.close();
}
