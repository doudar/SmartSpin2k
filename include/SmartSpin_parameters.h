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

#include "settings.h"

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
  double targetIncline = 0.0;
  float simulatedSpeed = 0.0;
  uint8_t FTMSMode     = 0x00;
  int shifterPosition  = 0;
  bool homed           = false;
  int32_t minStep      = -DEFAULT_STEPPER_TRAVEL;
  int32_t maxStep      = DEFAULT_STEPPER_TRAVEL;
  int minResistance    = -DEFAULT_RESISTANCE_RANGE;
  int maxResistance    = DEFAULT_RESISTANCE_RANGE;
  bool simTargetWatts  = false;

 public:
  Measurement watts;
  Measurement pm_batt;
  Measurement hr;
  Measurement hr_batt;
  Measurement cad;
  Measurement resistance;

  void setTargetIncline(float inc) { targetIncline = inc; }
  float getTargetIncline() { return targetIncline; }

  void setSimulatedSpeed(float spd) { simulatedSpeed = spd; }
  float getSimulatedSpeed() { return simulatedSpeed; }

  void setFTMSMode(uint8_t mde) { FTMSMode = mde; }
  uint8_t getFTMSMode() { return FTMSMode; }

  void setShifterPosition(int sp) { shifterPosition = sp; }
  int getShifterPosition() { return shifterPosition; }

  void setHomed(bool hmd) { homed = hmd; }
  int getHomed() { return homed; }

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
  int stepperSpeed;
  bool stepperDir;
  bool shifterDir;
  bool pTab4Pwr              = false;
  bool udpLogEnabled         = false;
  int32_t hMin               = INT32_MIN;
  int32_t hMax               = INT32_MIN;
  bool FTMSControlPointWrite = false;
  int homingSensitivity      = DEFAULT_HOMING_SENSITIVITY;  // Use default from settings.h
  String ssid;
  String password;
  String connectedPowerMeter   = CONNECTED_POWER_METER;
  String connectedHeartMonitor = CONNECTED_HEART_MONITOR;
  String connectedRemote       = CONNECTED_REMOTE;
  String foundDevices          = "";

 public:
  void setFirmwareUpdateURL(String fURL) { firmwareUpdateURL = fURL; }
  const char* getFirmwareUpdateURL() { return firmwareUpdateURL.c_str(); }

  void setDeviceName(String dvn) { deviceName = dvn; }
  const char* getDeviceName() { return deviceName.c_str(); }

  void setShiftStep(int ss) { shiftStep = ss; }
  int getShiftStep() { return shiftStep; }

  void setStealthChop(bool sc) { stealthChop = sc; }
  bool getStealthChop() { return stealthChop; }

  void setFTMSControlPointWrite(bool cpw) { FTMSControlPointWrite = cpw; }
  bool getFTMSControlPointWrite() { return FTMSControlPointWrite; }

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

  void setConnectedRemote(String cRemote) { connectedRemote = cRemote; }
  const char* getConnectedRemote() { return connectedRemote.c_str(); }

  void setStepperPower(int sp) { stepperPower = sp; }
  int getStepperPower() { return stepperPower; }

  void setStepperSpeed(int sp) { stepperSpeed = sp; }
  int getStepperSpeed() { return stepperSpeed; }

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

  void setPTab4Pwr(bool pTab) { pTab4Pwr = pTab; }
  bool getPTab4Pwr() { return pTab4Pwr; }

  void setFoundDevices(String fdv) { foundDevices = fdv; }
  const char* getFoundDevices() { return foundDevices.c_str(); }

  void setHMin(int32_t min) { hMin = min; }
  int32_t getHMin() { return hMin; }

  void setHMax(int32_t max) { hMax = max; }
  int32_t getHMax() { return hMax; }

  void setHomingSensitivity(int sensitivity) { homingSensitivity = sensitivity; }
  int getHomingSensitivity() { return homingSensitivity; }

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
