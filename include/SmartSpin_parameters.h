/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <Arduino.h>

class userParameters {
 private:
  String firmwareUpdateURL;
  float incline;
  int simulatedWatts;
  int simulatedHr;
  int simulatedCad;
  float simulatedSpeed;
  String deviceName;
  int shiftStep;
  int stepperPower;
  bool stealthchop;
  float inclineMultiplier;
  bool doublePower;  // didn't really have a good purpose before, going to be
                     // used for HR2VP like calculation
  bool simulateHr;
  bool ERGMode;
  bool autoUpdate;
  String ssid;
  String password;
  String foundDevices          = " ";
  String connectedPowerMeter   = "any";
  String connectedHeartMonitor = "any";

 public:
  const char* getFirmwareUpdateURL() { return firmwareUpdateURL.c_str(); }
  float getIncline() { return incline; }
  int getSimulatedWatts() { return simulatedWatts; }
  int getSimulatedHr() { return simulatedHr; }
  float getSimulatedCad() { return simulatedCad; }
  float getSimulatedSpeed() { return simulatedSpeed; }
  const char* getDeviceName() { return deviceName.c_str(); }
  int getShiftStep() { return shiftStep; }
  int getStepperPower() { return stepperPower; }
  bool getStealthchop() { return stealthchop; }
  float getInclineMultiplier() { return inclineMultiplier; }
  bool getDoublePower() { return doublePower; }
  bool getSimulateHr() { return simulateHr; }
  bool getERGMode() { return ERGMode; }
  bool getautoUpdate() { return autoUpdate; }
  const char* getSsid() { return ssid.c_str(); }
  const char* getPassword() { return password.c_str(); }
  const char* getFoundDevices() { return foundDevices.c_str(); }
  const char* getconnectedPowerMeter() { return connectedPowerMeter.c_str(); }
  const char* getconnectedHeartMonitor() { return connectedHeartMonitor.c_str(); }

  void setDefaults();
  void setFirmwareUpdateURL(String fURL) { firmwareUpdateURL = fURL; }
  void setIncline(float inc) { incline = inc; }
  void setSimulatedWatts(int w) { simulatedWatts = w; }
  void setSimulatedHr(int hr) { simulatedHr = hr; }
  void setSimulatedCad(float cad) { simulatedCad = cad; }
  void setSimulatedSpeed(float spd) { simulatedSpeed = spd; }
  void setDeviceName(String dvcn) { deviceName = dvcn; }
  void setShiftStep(int ss) { shiftStep = ss; }
  void setStepperPower(int sp) { stepperPower = sp; }
  void setStealthChop(bool sc) { stealthchop = sc; }
  void setInclineMultiplier(float im) { inclineMultiplier = im; }
  void setDoublePower(bool sp) { doublePower = sp; }
  void setSimulateHr(bool shr) { simulateHr = shr; }
  void setERGMode(bool erg) { ERGMode = erg; }
  void setAutoUpdate(bool atupd) { autoUpdate = atupd; }
  void setSsid(String sid) { ssid = sid; }
  void setPassword(String pwd) { password = pwd; }
  void setFoundDevices(String fdev) { foundDevices = fdev; }
  void setConnectedPowerMeter(String cpm) { connectedPowerMeter = cpm; }
  void setConnectedHeartMonitor(String cHr) { connectedHeartMonitor = cHr; }

  String returnJSON();
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
