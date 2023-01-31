/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#ifndef UNIT_TEST
#include <Arduino.h>
#else
#include <ArduinoFake.h>
#endif

#define CONFIG_LOG_TAG "Config"

class Measurement {
 private:
  bool simulate;
  int value;
  int target;
  unsigned long timestamp;

 public:
  void setSimulate(bool sim) {
    simulate        = sim;
    this->timestamp = millis();
  }
  bool getSimulate() { return simulate; }

  void setValue(int val) {
    value           = val;
    this->timestamp = millis();
  }
  int getValue() { return value; }

  void setTarget(int tar) {
    target          = tar;
    this->timestamp = millis();
  }
  int getTarget() { return target; }

  long getTimestamp() { return timestamp; }

  Measurement() {
    this->simulate  = false;
    this->value     = 0;
    this->target    = 0;
    this->timestamp = millis();
  }
};

class RuntimeParameters {
 private:
  float targetIncline  = 0.0;
  float currentIncline = 0.0;
  float simulatedSpeed = 0.0;
  uint8_t FTMSMode     = 0x00;
  int shifterPosition  = 0;
  int minStep          = -DEFAULT_STEPPER_TRAVEL;
  int maxStep          = DEFAULT_STEPPER_TRAVEL;
  int minResistance    = -DEFAULT_RESISTANCE_RANGE;
  int maxResistance    = DEFAULT_RESISTANCE_RANGE;
  bool simTargetWatts  = false;

 public:
  Measurement watts;
  Measurement hr;
  Measurement cad;
  Measurement resistance;

  void setTargetIncline(float inc) { targetIncline = inc; }
  float getTargetIncline() { return targetIncline; }

  void setCurrentIncline(float inc) { currentIncline = inc; }
  float getCurrentIncline() { return currentIncline; }

  void setSimulatedSpeed(float spd) { simulatedSpeed = spd; }
  float getSimulatedSpeed() { return simulatedSpeed; }

  void setFTMSMode(uint8_t mde) { FTMSMode = mde; }
  uint8_t getFTMSMode() { return FTMSMode; }

  void setShifterPosition(int sp) { shifterPosition = sp; }
  int getShifterPosition() { return shifterPosition; }

  void setMinStep(int ms) { minStep = ms; }
  int getMinStep() { return minStep; }

  void setMaxStep(int ms) { maxStep = ms; }
  int getMaxStep() { return maxStep; }

  void setSimTargetWatts(int tgt) { simTargetWatts = tgt; }
  bool getSimTargetWatts() { return simTargetWatts; }

  void setMinResistance(int min) { minResistance = min; }
  int getMinResistance() { return minResistance; }

  void setMaxResistance(int max) { maxResistance = max; }
  int getMaxResistance() { return maxResistance; }

  String returnJSON();
};

class userParameters {
 private:
  String firmwareUpdateURL;
  String deviceName;
  int shiftStep;
  bool stealthChop;
  float inclineMultiplier;
  float powerCorrectionFactor;
  float ERGSensitivity;
  bool autoUpdate;
  int stepperPower;
  int maxWatts;
  int minWatts;
  bool stepperDir;
  bool shifterDir;
  bool udpLogEnabled = false;
  String ssid;
  String password;
  String connectedPowerMeter   = "any";
  String connectedHeartMonitor = "any";
  String foundDevices          = " ";

 public:
  void setFirmwareUpdateURL(String fURL) { firmwareUpdateURL = fURL; }
  const char* getFirmwareUpdateURL() { return firmwareUpdateURL.c_str(); }

  void setDeviceName(String dvn) { deviceName = dvn; }
  const char* getDeviceName() { return deviceName.c_str(); }

  void setShiftStep(int ss) { shiftStep = ss; }
  int getShiftStep() { return shiftStep; }

  void setStealthChop(bool sc) { stealthChop = sc; }
  bool getStealthChop() { return stealthChop; }

  void setInclineMultiplier(float im) { inclineMultiplier = im; }
  float getInclineMultiplier() { return inclineMultiplier; }

  void setPowerCorrectionFactor(float pcf) { powerCorrectionFactor = pcf; }
  float getPowerCorrectionFactor() { return powerCorrectionFactor; }

  float getERGSensitivity() { return ERGSensitivity; }
  void setERGSensitivity(float ergS) { ERGSensitivity = ergS; }

  void setAutoUpdate(bool atd) { autoUpdate = atd; }
  bool getAutoUpdate() { return autoUpdate; }

  void setSsid(String sid) { ssid = sid; }
  const char* getSsid() { return ssid.c_str(); }

  void setPassword(String pwd) { password = pwd; }
  const char* getPassword() { return password.c_str(); }

  void setConnectedPowerMeter(String cpm) { connectedPowerMeter = cpm; }
  const char* getConnectedPowerMeter() { return connectedPowerMeter.c_str(); }

  void setConnectedHeartMonitor(String cHr) { connectedHeartMonitor = cHr; }
  const char* getConnectedHeartMonitor() { return connectedHeartMonitor.c_str(); }

  void setStepperPower(int sp) { stepperPower = sp; }
  int getStepperPower() { return stepperPower; }

  void setMaxWatts(int maxW) { maxWatts = maxW; }
  int getMaxWatts() { return maxWatts; }

  void setMinWatts(int minW) { minWatts = minW; }
  int getMinWatts() { return minWatts; }

  void setStepperDir(bool sd) { stepperDir = sd; }
  bool getStepperDir() { return stepperDir; }

  void setShifterDir(bool shd) { shifterDir = shd; }
  bool getShifterDir() { return shifterDir; }

  void setUdpLogEnabled(bool enabled) { udpLogEnabled = enabled; }
  bool getUdpLogEnabled() { return udpLogEnabled; }

  void setFoundDevices(String fdv) { foundDevices = fdv; }
  const char* getFoundDevices() { return foundDevices.c_str(); }

  void setDefaults();
  String returnJSON();
  void saveToLittleFS();
  void loadFromLittleFS();
  void printFile();
};

class physicalWorkingCapacity {
 public:
  int session1HR;
  int session1Pwr;
  int session2HR;
  int session2Pwr;
  bool hr2Pwr;

  void setDefaults();
  String returnJSON();
  void saveToLittleFS();
  void loadFromLittleFS();
  void printFile();
};
