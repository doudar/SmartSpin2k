/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <Arduino.h>

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
  float incline              = 0.0;
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
  String foundDevices        = " ";
  bool stepperRunning        = false;

 public:
  float getIncline() { return incline; }
  int getTargetWatts() { return targetWatts; }
  Measurement getSimulatedWatts() { return simulatedWatts; }
  int getSimulatedHr() { return simulatedHr; }
  int getSimulatedCad() { return simulatedCad; }
  float getSimulatedSpeed() { return simulatedSpeed; }
  bool getSimulateHr() { return simulateHr; }
  bool getSimulateWatts() { return simulateWatts; }
  bool getSimulateTargetWatts() { return simulateTargetWatts; }
  bool getSimulateCad() { return simulateCad; }
  bool getERGMode() { return ERGMode; }
  int getShifterPosition() { return shifterPosition; }
  bool getStepperRunning() { return stepperRunning; }
  const char* getFoundDevices() { return foundDevices.c_str(); }

  void setIncline(float inc) { incline = inc; }
  void setTargetWatts(int w) { targetWatts = w; }
  void setSimulatedWatts(Measurement w) { simulatedWatts = w; }
  void setSimulatedHr(int hr) { simulatedHr = hr; }
  void setSimulatedCad(float cad) { simulatedCad = cad; }
  void setSimulatedSpeed(float spd) { simulatedSpeed = spd; }
  void setSimulateHr(bool shr) { simulateHr = shr; }
  void setSimulateWatts(bool swt) { simulateWatts = swt; }
  void setSimulateTargetWatts(bool swt) { simulateTargetWatts = swt; }
  void setSimulateCad(bool scd) { simulateCad = scd; }
  void setERGMode(bool erg) { ERGMode = erg; }
  void setShifterPosition(int sp) { shifterPosition = sp; }
  void setStepperRunning(bool running) { stepperRunning = running; }
  void setFoundDevices(String fdev) { foundDevices = fdev; }
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
  String ssid;
  String password;
  String connectedPowerMeter   = "any";
  String connectedHeartMonitor = "any";

 public:
  const char* getFirmwareUpdateURL() { return firmwareUpdateURL.c_str(); }
  const char* getDeviceName() { return deviceName.c_str(); }
  int getShiftStep() { return shiftStep; }
  bool getStealthchop() { return stealthchop; }
  float getInclineMultiplier() { return inclineMultiplier; }
  float getPowerCorrectionFactor() { return powerCorrectionFactor; }
  float getERGSensitivity() { return ERGSensitivity; }
  bool getautoUpdate() { return autoUpdate; }
  const char* getSsid() { return ssid.c_str(); }
  const char* getPassword() { return password.c_str(); }
  const char* getconnectedPowerMeter() { return connectedPowerMeter.c_str(); }
  const char* getconnectedHeartMonitor() { return connectedHeartMonitor.c_str(); }
  int getStepperPower() { return stepperPower; }

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

  String returnJSON(bool includeDebugLog = false);
  void saveToSPIFFS();
  void loadFromSPIFFS();
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
  void saveToSPIFFS();
  void loadFromSPIFFS();
  void printFile();
};
