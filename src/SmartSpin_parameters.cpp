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
JsonDocument doc;
  // Set the values in the document

  doc["watts"]            = this->watts.getValue();
  doc["targetWatts"]      = this->watts.getTarget();
  doc["simWatts"]         = this->watts.getSimulate();
  doc["hr"]               = this->hr.getValue();
  doc["simHr"]            = this->hr.getSimulate();
  doc["cad"]              = this->cad.getValue();
  doc["simCad"]           = this->cad.getSimulate();
  doc["resistance"]       = this->resistance.getValue();
  doc["targetResistance"] = this->resistance.getTarget();
  doc["homed"]            = this->homed.get();
  doc["targetIncline"]    = this->targetIncline.get();
  doc["speed"]            = this->simulatedSpeed.get();
  doc["simTargetWatts"]   = this->simTargetWatts.get();
  doc["FTMSMode"]         = this->FTMSMode.get();
  doc["shifterPosition"]  = this->shifterPosition.get();
  doc["minStep"]          = this->minStep.get();
  doc["maxStep"]          = this->maxStep.get();
  doc["minResistance"]    = this->minResistance.get();
  doc["maxResistance"]    = this->maxResistance.get();

  String output;
  serializeJson(doc, output);
  return output;
}

// Default Values
void userParameters::setDefaults() {
  firmwareUpdateURL.set(FW_UPDATEURL);
  deviceName.set(DEVICE_NAME);
  shiftStep.set(DEFAULT_SHIFT_STEP);
  stealthChop.set(STEALTHCHOP);
  stepperPower.set(DEFAULT_STEPPER_POWER);
  stepperSpeed.set(DEFAULT_STEPPER_SPEED);
  inclineMultiplier.set(INCLINE_MULTIPLIER);
  powerCorrectionFactor.set(1.0);
  ERGSensitivity.set(ERG_SENSITIVITY);
  autoUpdate.set(AUTO_FIRMWARE_UPDATE);
  ssid.set(DEVICE_NAME);
  password.set(DEFAULT_PASSWORD);
  connectedPowerMeter.set(CONNECTED_POWER_METER);
  connectedHeartMonitor.set(CONNECTED_HEART_MONITOR);
  connectedRemote.set(CONNECTED_REMOTE);
  foundDevices.set(" ");
  maxWatts.set(DEFAULT_MAX_WATTS);
  minWatts.set(DEFAULT_MIN_WATTS);
  stepperDir.set(true);
  shifterDir.set(true);
  udpLogEnabled.set(false);
  pTab4Pwr.set(false);
  hMin.set(INT32_MIN);
  hMax.set(INT32_MIN);
  homingSensitivity.set(DEFAULT_HOMING_SENSITIVITY);
}

//---------------------------------------------------------------------------------
//-- return all config as one a single JSON string
String userParameters::returnJSON() {
  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
 JsonDocument doc;
  // Set the values in the document

  doc["firmwareUpdateURL"]     = firmwareUpdateURL.get();
  doc["firmwareVersion"]       = FIRMWARE_VERSION;
  doc["deviceName"]            = deviceName.get();
  doc["shiftStep"]             = shiftStep.get();
  doc["stepperPower"]          = stepperPower.get();
  doc["stepperSpeed"]          = stepperSpeed.get();
  doc["stealthChop"]           = stealthChop.get();
  doc["inclineMultiplier"]     = inclineMultiplier.get();
  doc["powerCorrectionFactor"] = powerCorrectionFactor.get();
  doc["ERGSensitivity"]        = ERGSensitivity.get();
  doc["autoUpdate"]            = autoUpdate.get();
  doc["ssid"]                  = ssid.get();
  doc["password"]              = password.get();
  doc["connectedPowerMeter"]   = connectedPowerMeter.get();
  doc["connectedHeartMonitor"] = connectedHeartMonitor.get();
  doc["connectedRemote"]       = connectedRemote.get();
  doc["foundDevices"]          = foundDevices.get();
  doc["maxWatts"]              = maxWatts.get();
  doc["minWatts"]              = minWatts.get();
  doc["shifterDir"]            = shifterDir.get();
  doc["stepperDir"]            = stepperDir.get();
  doc["udpLogEnabled"]         = udpLogEnabled.get();
  doc["pTab4Pwr"]              = pTab4Pwr.get();
  doc["hMin"]                  = hMin.get();
  doc["hMax"]                  = hMax.get();
  doc["homingSensitivity"]     = homingSensitivity.get();

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
 JsonDocument doc;

  // Set the values in the document
  // commented items are not needed in save file

  doc["firmwareUpdateURL"]     = firmwareUpdateURL.get();
  doc["deviceName"]            = deviceName.get();
  doc["shiftStep"]             = shiftStep.get();
  doc["stepperPower"]          = stepperPower.get();
  doc["stepperSpeed"]          = stepperSpeed.get();
  doc["stealthChop"]           = stealthChop.get();
  doc["inclineMultiplier"]     = inclineMultiplier.get();
  doc["powerCorrectionFactor"] = powerCorrectionFactor.get();
  doc["ERGSensitivity"]        = ERGSensitivity.get();
  doc["autoUpdate"]            = autoUpdate.get();
  doc["ssid"]                  = ssid.get();
  doc["password"]              = password.get();
  doc["connectedPowerMeter"]   = connectedPowerMeter.get();
  doc["connectedHeartMonitor"] = connectedHeartMonitor.get();
  doc["connectedRemote"]       = connectedRemote.get();
  // doc["foundDevices"]          = foundDevices.get();
  doc["maxWatts"]      = maxWatts.get();
  doc["minWatts"]      = minWatts.get();
  doc["shifterDir"]    = shifterDir.get();
  doc["stepperDir"]    = stepperDir.get();
  doc["udpLogEnabled"] = udpLogEnabled.get();
  doc["pTab4Pwr"]      = pTab4Pwr.get();
  doc["hMin"]          = hMin.get();
  doc["hMax"]          = hMax.get();
  doc["homingSensitivity"]     = homingSensitivity.get();

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
    SS2K_LOG(CONFIG_LOG_TAG, "Couldn't find configuration file.");
    return;
  }
  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
JsonDocument doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    SS2K_LOG(CONFIG_LOG_TAG, "Failed to deserialize. Using defaults");
    return;
  }

  // Copy values from the JsonDocument to the Config
  setFirmwareUpdateURL(doc["firmwareUpdateURL"]);
  setDeviceName(doc["deviceName"]);
  setShiftStep(doc["shiftStep"]);
  setStepperPower(doc["stepperPower"]);
  setStealthChop(doc["stealthChop"]);
  setInclineMultiplier(doc["inclineMultiplier"]);
  setAutoUpdate(doc["autoUpdate"]);
  setSsid(doc["ssid"]);
  setPassword(doc["password"]);
  setConnectedPowerMeter(doc["connectedPowerMeter"]);
  setConnectedHeartMonitor(doc["connectedHeartMonitor"]);
  // setFoundDevices(doc["foundDevices"]);

  // If statements to upgrade old versions of config.txt that didn't include these
  if (doc["ERGSensitivity"]) {
    setERGSensitivity(doc["ERGSensitivity"]);
  }
  if (doc["maxWatts"]) {
    setMaxWatts(doc["maxWatts"]);
  }
  if (doc["stepperSpeed"]) {
    setStepperSpeed(doc["stepperSpeed"]);
  }
  if (doc["minWatts"]) {
    setMinWatts(doc["minWatts"]);
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
  if (!doc["pTab4Pwr"].isNull()) {
    setPTab4Pwr(doc["pTab4Pwr"]);
  }
  if (doc["powerCorrectionFactor"]) {
    setPowerCorrectionFactor(doc["powerCorrectionFactor"]);
    if ((getPowerCorrectionFactor() < MIN_PCF) || (getPowerCorrectionFactor() > MAX_PCF)) {
      setPowerCorrectionFactor(1);
    }
  }
  if (doc["connectedRemote"]) {
    setConnectedRemote(doc["connectedRemote"]);
  }
  if (!doc["hMin"].isNull()) {
    setHMin(doc["hMin"]);
  }
  if (!doc["hMax"].isNull()) {
    setHMax(doc["hMax"]);
  }
  if (!doc["homingSensitivity"].isNull()) {
    setHomingSensitivity(doc["homingSensitivity"]);
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

