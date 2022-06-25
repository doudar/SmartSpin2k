/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "LittleFS_Upgrade.h"
#include "Main.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <SPIFFS.h>

void FSUpgrader::UpgradeFS() {
  if (!SPIFFS.begin()) {
    SS2K_LOG(FS_UPGRADER_LOG_TAG, "SPIFFS Not Found - Could not upgrade FS");
    SPIFFS.end();
    LittleFS.begin();
    LittleFS.format();
    LittleFS.end();
    return;
  };
  file = SPIFFS.open(configFILENAME, FILE_WRITE);
}

// Loads the JSON configuration from a file into a userParameters Object
void FSUpgrader::loadFromSPIFFS() {
  userConfig.setDefaults();
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
