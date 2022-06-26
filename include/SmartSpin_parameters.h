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
 public:
  int value;
  long timestamp;

  Measurement(int value) {
    this->value     = value;
    this->timestamp = millis();
  }
};

class RuntimeParameters {
 private:
  float targetIncline        = 0.0;
  float currentIncline       = 0.0;
  int targetWatts            = 0;
  Measurement simulatedWatts = Measurement(0);
  int simulatedHr            = 0;
  int simulatedCad           = 0;
  float simulatedSpeed       = 0.0;
  bool simulateHr            = false;
  bool simulateWatts         = false;
  bool simulateTargetWatts   = false;
  bool simulateCad           = false;
  bool ERGMode               = false;
  int shifterPosition        = 0;
  int minStep                = -200000000;
  int maxStep                = 200000000;

 public:
  void setTargetIncline(float inc) { targetIncline = inc; }
  float getTargetIncline() { return targetIncline; }

  void setCurrentIncline(float inc) { currentIncline = inc; }
  float getCurrentIncline() { return currentIncline; }

  void setTargetWatts(int w) { targetWatts = w; }
  int getTargetWatts() { return targetWatts; }

  void setSimulatedWatts(int w) { simulatedWatts = Measurement(w); }
  Measurement getSimulatedWatts() { return simulatedWatts; }

  void setSimulatedHr(int hr) { simulatedHr = hr; }
  int getSimulatedHr() { return simulatedHr; }

  void setSimulatedCad(float cad) { simulatedCad = cad; }
  int getSimulatedCad() { return simulatedCad; }

  void setSimulatedSpeed(float spd) { simulatedSpeed = spd; }
  float getSimulatedSpeed() { return simulatedSpeed; }

  void setSimulateHr(bool shr) { simulateHr = shr; }
  bool getSimulateHr() { return simulateHr; }

  void setSimulateWatts(bool swt) { simulateWatts = swt; }
  bool getSimulateWatts() { return simulateWatts; }

  void setSimulateTargetWatts(bool swt) { simulateTargetWatts = swt; }
  bool getSimulateTargetWatts() { return simulateTargetWatts; }

  void setSimulateCad(bool scd) { simulateCad = scd; }
  bool getSimulateCad() { return simulateCad; }

  void setERGMode(bool erg) { ERGMode = erg; }
  bool getERGMode() { return ERGMode; }

  void setShifterPosition(int sp) { shifterPosition = sp; }
  int getShifterPosition() { return shifterPosition; }

  void setMinStep(int ms) { minStep = ms; }
  int getMinStep() { return minStep; }

  void setMaxStep(int ms) { maxStep = ms; }
  int getMaxStep() { return maxStep; }

  String returnJSON();
};

class userParameters {
 private:
  String firmwareUpdateURL;
  String deviceName;
  int shiftStep;
  bool stealthchop;
  float inclineMultiplier;
  float powerCorrectionFactor;
  float ERGSensitivity;
  bool autoUpdate;
  int stepperPower;
  int maxWatts;
  bool stepperDir;
  bool shifterDir;
  bool udpLogEnabled = false;
  String ssid;
  String password;
  String connectedPowerMeter   = "any";
  String connectedHeartMonitor = "any";
  String foundDevices          = " ";

 public:
  const char* getFirmwareUpdateURL() { return firmwareUpdateURL.c_str(); }
  const char* getDeviceName() { return deviceName.c_str(); }
  int getShiftStep() { return shiftStep; }
  bool getStealthchop() { return stealthchop; }
  float getInclineMultiplier() { return inclineMultiplier; }
  float getPowerCorrectionFactor() { return powerCorrectionFactor; }
  float getERGSensitivity() { return ERGSensitivity; }
  bool getAutoUpdate() { return autoUpdate; }
  const char* getSsid() { return ssid.c_str(); }
  const char* getPassword() { return password.c_str(); }
  const char* getconnectedPowerMeter() { return connectedPowerMeter.c_str(); }
  const char* getconnectedHeartMonitor() { return connectedHeartMonitor.c_str(); }
  int getStepperPower() { return stepperPower; }
  int getMaxWatts() { return maxWatts; }
  bool getStepperDir() { return stepperDir; }
  bool getShifterDir() { return shifterDir; }
  bool getUdpLogEnabled() { return udpLogEnabled; }

  void setDefaults();
  void setFirmwareUpdateURL(String fURL) { firmwareUpdateURL = fURL; }
  void setDeviceName(String dvcn) { deviceName = dvcn; }
  void setShiftStep(int ss) { shiftStep = ss; }
  void setStealthChop(bool sc) { stealthchop = sc; }
  void setInclineMultiplier(float im) { inclineMultiplier = im; }
  void setPowerCorrectionFactor(float pm) { powerCorrectionFactor = pm; }
  void setERGSensitivity(float ergS) { ERGSensitivity = ergS; }
  void setAutoUpdate(bool atupd) { autoUpdate = atupd; }
  void setSsid(String sid) { ssid = sid; }
  void setPassword(String pwd) { password = pwd; }
  void setConnectedPowerMeter(String cpm) { connectedPowerMeter = cpm; }
  void setConnectedHeartMonitor(String cHr) { connectedHeartMonitor = cHr; }
  void setStepperPower(int sp) { stepperPower = sp; }
  void setMaxWatts(int maxW) { maxWatts = maxW; }
  void setStepperDir(bool sd) { stepperDir = sd; }
  void setShifterDir(bool shd) { shifterDir = shd; }
  void setUdpLogEnabled(bool enabled) { udpLogEnabled = enabled; }
  void setFoundDevices(String fdev) { foundDevices = fdev; }
  const char* getFoundDevices() { return foundDevices.c_str(); }

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
