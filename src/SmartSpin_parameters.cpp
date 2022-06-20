/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "SS2KLog.h"
#include "SmartSpin_parameters.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

String RuntimeParameters::returnJSON() {
  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
  StaticJsonDocument<RUNTIMECONFIG_JSON_SIZE> doc;
  // Set the values in the document

  doc["targetIncline"]       = targetIncline;
  doc["currentIncline"]      = currentIncline;
  doc["targetWatts"]         = targetWatts;
  doc["simulatedWatts"]      = simulatedWatts.value;
  doc["simulatedHr"]         = simulatedHr;
  doc["simulatedCad"]        = simulatedCad;
  doc["simulatedSpeed"]      = simulatedSpeed;
  doc["simulateHr"]          = simulateHr;
  doc["simulateWatts"]       = simulateWatts;
  doc["simulateTargetWatts"] = simulateTargetWatts;
  doc["simulateCad"]         = simulateCad;
  doc["ERGMode"]             = ERGMode;
  doc["shifterPosition"]     = shifterPosition;
  doc["minStep"]             = minStep;
  doc["maxStep"]             = maxStep;

  String output;
  serializeJson(doc, output);
  return output;
}

// Default Values
void userParameters::setDefaults() {
  firmwareUpdateURL     = FW_UPDATEURL;
  deviceName            = DEVICE_NAME;
  shiftStep             = DEFAULT_SHIFT_STEP;
  stealthchop           = STEALTHCHOP;
  stepperPower          = DEFAULT_STEPPER_POWER;
  inclineMultiplier     = 3.0;
  powerCorrectionFactor = 1.0;
  ERGSensitivity        = ERG_SENSITIVITY;
  autoUpdate            = AUTO_FIRMWARE_UPDATE;
  ssid                  = DEVICE_NAME;
  password              = DEFAULT_PASSWORD;
  connectedPowerMeter   = CONNECTED_POWER_METER;
  connectedHeartMonitor = CONNECTED_HEART_MONITOR;
  maxWatts              = DEFAULT_MAX_WATTS;
  stepperDir            = true;
  shifterDir            = true;
  udpLogEnabled         = false;
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
  doc["firmwareVersion"]       = FIRMWARE_VERSION;
  doc["deviceName"]            = deviceName;
  doc["shiftStep"]             = shiftStep;
  doc["stepperPower"]          = stepperPower;
  doc["stealthchop"]           = stealthchop;
  doc["inclineMultiplier"]     = inclineMultiplier;
  doc["powerCorrectionFactor"] = powerCorrectionFactor;
  doc["ERGSensitivity"]        = ERGSensitivity;
  doc["autoUpdate"]            = autoUpdate;
  doc["ssid"]                  = ssid;
  doc["password"]              = password;
  doc["connectedPowerMeter"]   = connectedPowerMeter;
  doc["connectedHeartMonitor"] = connectedHeartMonitor;
  doc["foundDevices"]          = foundDevices;
  doc["maxWatts"]              = maxWatts;
  doc["shifterDir"]            = shifterDir;
  doc["stepperDir"]            = stepperDir;
  doc["udpLogEnabled"]         = udpLogEnabled;

  String output;
  serializeJson(doc, output);
  return output;
}

//-- Saves all parameters to LittleFS
void userParameters::saveToLittleFS() {
  // Delete existing file, otherwise the configuration is appended to the file
  LittleFS.remove(configFILENAME);

  // Open file for writing
  SS2K_LOG(CONFIG_LOG_TAG, "Writing File: %s", configFILENAME);
  File file = LittleFS.open(configFILENAME, FILE_WRITE);
  if (!file) {
    SS2K_LOG(CONFIG_LOG_TAG, "Failed to create file");
    return;
  }

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
  StaticJsonDocument<USERCONFIG_JSON_SIZE> doc;

  // Set the values in the document
  // commented items are not needed in save file

  doc["firmwareUpdateURL"]     = firmwareUpdateURL;
  doc["deviceName"]            = deviceName;
  doc["shiftStep"]             = shiftStep;
  doc["stepperPower"]          = stepperPower;
  doc["stealthchop"]           = stealthchop;
  doc["inclineMultiplier"]     = inclineMultiplier;
  doc["powerCorrectionFactor"] = powerCorrectionFactor;
  doc["ERGSensitivity"]        = ERGSensitivity;
  doc["autoUpdate"]            = autoUpdate;
  doc["ssid"]                  = ssid;
  doc["password"]              = password;
  doc["connectedPowerMeter"]   = connectedPowerMeter;
  doc["connectedHeartMonitor"] = connectedHeartMonitor;
  doc["foundDevices"]          = foundDevices;
  doc["maxWatts"]              = maxWatts;
  doc["shifterDir"]            = shifterDir;
  doc["stepperDir"]            = stepperDir;
  doc["udpLogEnabled"]         = udpLogEnabled;

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    SS2K_LOG(CONFIG_LOG_TAG, "Failed to write to file");
  }
  // Close the file
  file.close();
}

// Loads the JSON configuration from a file into a userParameters Object
void userParameters::loadFromLittleFS() {
  setDefaults();
  // Open file for reading
  SS2K_LOG(CONFIG_LOG_TAG, "Reading File: %s", configFILENAME);
  File file = LittleFS.open(configFILENAME);

  // load defaults if filename doesn't exist
  if (!file) {
    SS2K_LOG(CONFIG_LOG_TAG, "Couldn't find configuration file. using defaults");
    return;
  }
  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<USERCONFIG_JSON_SIZE> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    SS2K_LOG(CONFIG_LOG_TAG, "Failed to read file, using defaults");
    return;
  }

  // Copy values from the JsonDocument to the Config
  setFirmwareUpdateURL(doc["firmwareUpdateURL"]);
  setDeviceName(doc["deviceName"]);
  setShiftStep(doc["shiftStep"]);
  setStepperPower(doc["stepperPower"]);
  setStealthChop(doc["stealthchop"]);
  setInclineMultiplier(doc["inclineMultiplier"]);
  setAutoUpdate(doc["autoUpdate"]);
  setSsid(doc["ssid"]);
  setPassword(doc["password"]);
  setConnectedPowerMeter(doc["connectedPowerMeter"]);
  setConnectedHeartMonitor(doc["connectedHeartMonitor"]);
  setFoundDevices(doc["foundDevices"]);
  if (doc["ERGSensitivity"]) {  // If statements to upgrade old versions of config.txt that didn't include these
    setERGSensitivity(doc["ERGSensitivity"]);
  }
  if (doc["maxWatts"]) {
    setMaxWatts(doc["maxWatts"]);
  }
  if (!doc["stepperDir"].isNull()) {
    setStepperDir(doc["stepperDir"]);
  }
  if (!doc["shifterDir"].isNull()) {
    setShifterDir(doc["shifterDir"]);
  }
  if (!doc["udpLogEnabled"].isNull()) {
    setUdpLogEnabled(doc["udpLogEnabled"]);
  }
  if (doc["powerCorrectionFactor"]) {
    setPowerCorrectionFactor(doc["powerCorrectionFactor"]);
    if ((getPowerCorrectionFactor() < MIN_PCF) || (getPowerCorrectionFactor() > MAX_PCF)) {
      setPowerCorrectionFactor(1);
    }
  }

  SS2K_LOG(CONFIG_LOG_TAG, "Config File Loaded: %s", configFILENAME);
  file.close();
}

// Prints the content of a file to the Serial
void userParameters::printFile() {
  // Open file for reading
  SS2K_LOG(CONFIG_LOG_TAG, "Contents of file: %s", configFILENAME);
  File file = LittleFS.open(configFILENAME);
  if (!file) {
    SS2K_LOG(CONFIG_LOG_TAG, "Failed to read file");
    return;
  }

  // Close the file
  file.close();
}

/*****************************************USERPWC*****************************************/

void physicalWorkingCapacity::setDefaults() {
  session1HR  = 129;  // examples from https://www.cyclinganalytics.com/
  session1Pwr = 100;
  session2HR  = 154;
  session2Pwr = 150;
  hr2Pwr      = false;
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

//-- Saves all parameters to LittleFS
void physicalWorkingCapacity::saveToLittleFS() {
  // Delete existing file, otherwise the configuration is appended to the file
  LittleFS.remove(userPWCFILENAME);
  // Open file for writing
  SS2K_LOG(CONFIG_LOG_TAG, "Writing File: %s", userPWCFILENAME);
  File file = LittleFS.open(userPWCFILENAME, FILE_WRITE);
  if (!file) {
    SS2K_LOG(CONFIG_LOG_TAG, "Failed to create file");
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
    SS2K_LOG(CONFIG_LOG_TAG, "Failed to write to file");
  }
  // Close the file
  file.close();
}

// Loads the JSON configuration from a file
void physicalWorkingCapacity::loadFromLittleFS() {
  // Open file for reading
  SS2K_LOG(CONFIG_LOG_TAG, "Reading File: %s", userPWCFILENAME);
  File file = LittleFS.open(userPWCFILENAME);

  // load defaults if filename doesn't exist
  if (!file) {
    SS2K_LOG(CONFIG_LOG_TAG, "Couldn't find configuration file. Loading Defaults");
    setDefaults();
    return;
  }

  StaticJsonDocument<500> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    SS2K_LOG(CONFIG_LOG_TAG, "Failed to read file, using default configuration");
    setDefaults();
    return;
  }

  // Copy values from the JsonDocument to the Config
  session1HR  = doc["session1HR"];
  session1Pwr = doc["session1Pwr"];
  session2HR  = doc["session2HR"];
  session2Pwr = doc["session2Pwr"];
  hr2Pwr      = doc["hr2Pwr"];

  SS2K_LOG(CONFIG_LOG_TAG, "Config File Loaded: %s", userPWCFILENAME);
  file.close();
}

// Prints the content of a file to the Serial
void physicalWorkingCapacity::printFile() {
  // Open file for reading
  SS2K_LOG(CONFIG_LOG_TAG, "Contents of file: %s", userPWCFILENAME);
  File file = LittleFS.open(userPWCFILENAME);
  if (!file) {
    SS2K_LOG(CONFIG_LOG_TAG, "Failed to read file");
    return;
  }

  // Close the file
  file.close();
}
