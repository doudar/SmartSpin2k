/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "LittleFS_Upgrade.h"
#include "Main.h"
#include <LittleFS.h>
#include <arduinojson.h>

void FSUpgrader::upgradeFS() {
  SS2K_LOG(FS_UPGRADER_LOG_TAG, "Stopping LittleFS");
  LittleFS.end();
  if (!SPIFFS.begin(false)) {
    SS2K_LOG(FS_UPGRADER_LOG_TAG, "SPIFFS Not Found - Could not upgrade FS");
    SPIFFS.end();
    vTaskDelay(100 / portTICK_PERIOD_MS);
    LittleFS.begin(true);
    return;
  };
  // attempt to recover data from old SPIFFS Config
  this->loadFromSPIFFS();
  // Cleanup
  SPIFFS.end();
  vTaskDelay(100 / portTICK_PERIOD_MS);
  SS2K_LOG(FS_UPGRADER_LOG_TAG, "Upgrade Complete. Starting LittleFS");
  if (!LittleFS.begin(true)) {
    SS2K_LOG(FS_UPGRADER_LOG_TAG, "Starting LittleFS failed. Retrying");
    vTaskDelay(100 / portTICK_PERIOD_MS);
    LittleFS.begin(true);
  }
  LittleFS.format();
  userConfig.saveToLittleFS();
}

// Loads the JSON configuration from a file into a userParameters Object
void FSUpgrader::loadFromSPIFFS() {
  userConfig.setDefaults();
  // Open file for reading
  SS2K_LOG(FS_UPGRADER_LOG_TAG, "Upgrade starting. Reading File: %s", configFILENAME);
  this->file = SPIFFS.open(configFILENAME);

  // load defaults if filename doesn't exist
  if (!this->file) {
    SS2K_LOG(FS_UPGRADER_LOG_TAG, "Couldn't upgrade filesystem. Using defaults");
    return;
  }
  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<USERCONFIG_JSON_SIZE> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    SS2K_LOG(FS_UPGRADER_LOG_TAG, "Failed to deserialize. Using defaults");
    return;
  }

  // Copy values from the JsonDocument to the Config
  userConfig.setFirmwareUpdateURL(doc["firmwareUpdateURL"]);
  userConfig.setDeviceName(doc["deviceName"]);
  userConfig.setShiftStep(doc["shiftStep"]);
  userConfig.setStepperPower(doc["stepperPower"]);
  userConfig.setStealthChop(doc["stealthchop"]);
  userConfig.setInclineMultiplier(doc["inclineMultiplier"]);
  userConfig.setAutoUpdate(doc["autoUpdate"]);
  userConfig.setSsid(doc["ssid"]);
  userConfig.setPassword(doc["password"]);
  userConfig.setConnectedPowerMeter(doc["connectedPowerMeter"]);
  userConfig.setConnectedHeartMonitor(doc["connectedHeartMonitor"]);
  userConfig.setFoundDevices(doc["foundDevices"]);
  if (doc["ERGSensitivity"]) {  // If statements to upgrade old versions of config.txt that didn't include these
    userConfig.setERGSensitivity(doc["ERGSensitivity"]);
  }
  if (doc["maxWatts"]) {
    userConfig.setMaxWatts(doc["maxWatts"]);
  }
  if (!doc["stepperDir"].isNull()) {
    userConfig.setStepperDir(doc["stepperDir"]);
  }
  if (!doc["shifterDir"].isNull()) {
    userConfig.setShifterDir(doc["shifterDir"]);
  }
  if (!doc["udpLogEnabled"].isNull()) {
    userConfig.setUdpLogEnabled(doc["udpLogEnabled"]);
  }
  if (doc["powerCorrectionFactor"]) {
    userConfig.setPowerCorrectionFactor(doc["powerCorrectionFactor"]);
    if ((userConfig.getPowerCorrectionFactor() < MIN_PCF) || (userConfig.getPowerCorrectionFactor() > MAX_PCF)) {
      userConfig.setPowerCorrectionFactor(1);
    }
  }

  SS2K_LOG(FS_UPGRADER_LOG_TAG, "Config File Loaded: %s", configFILENAME);
  this->file.close();
}
