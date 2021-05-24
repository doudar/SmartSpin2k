/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <Arduino.h>

 #define BLE_firmwareUpdateURL 0x01
 #define BLE_incline 0x02
 #define BLE_simulatedWatts 0x03
 #define BLE_simulatedHr 0x04
 #define BLE_simulatedCad 0x05
 #define BLE_simulatedSpeed 0x06
 #define BLE_deviceName 0x07
 #define BLE_shiftStep 0x08
 #define BLE_stepperPower 0x09
 #define BLE_stealthchop 0x0A
 #define BLE_inclineMultiplier 0x0B
 #define BLE_powerCorrectionFactor 0x0C
 #define BLE_simulateHr 0x0D
 #define BLE_simulateWatts 0x0F
 #define BLE_simulateCad 0x10
 #define BLE_ERGMode 0x11
 #define BLE_autoUpdate 0x12
 #define BLE_ssid 0x13
 #define BLE_password 0x14
 #define BLE_foundDevices 0x15        
 #define BLE_connectedPowerMeter 0x16  
 #define BLE_connectedHeartMonitor 0x17
 #define BLE_shifterPosition 0x18

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
  float powerCorrectionFactor;
  bool simulateHr;
  bool simulateWatts;
  bool simulateCad;
  bool ERGMode;
  bool autoUpdate;
  String ssid;
  String password;
  String foundDevices          = " ";
  String connectedPowerMeter   = "any";
  String connectedHeartMonitor = "any";
  int shifterPosition;

 public:
  const char* getFirmwareUpdateURL() { return firmwareUpdateURL.c_str(); }
  float getIncline() { return incline; }
  int getSimulatedWatts() { return simulatedWatts; }
  int getSimulatedHr() { return simulatedHr; }
  int getSimulatedCad() { return simulatedCad; }
  float getSimulatedSpeed() { return simulatedSpeed; }
  const char* getDeviceName() { return deviceName.c_str(); }
  int getShiftStep() { return shiftStep; }
  int getStepperPower() { return stepperPower; }
  bool getStealthchop() { return stealthchop; }
  float getInclineMultiplier() { return inclineMultiplier; }
  float getPowerCorrectionFactor() { return powerCorrectionFactor; }
  bool getSimulateHr() { return simulateHr; }
  bool getSimulateWatts() { return simulateWatts; }
  bool getSimulateCad() { return simulateCad; }
  bool getERGMode() { return ERGMode; }
  bool getautoUpdate() { return autoUpdate; }
  const char* getSsid() { return ssid.c_str(); }
  const char* getPassword() { return password.c_str(); }
  const char* getFoundDevices() { return foundDevices.c_str(); }
  const char* getconnectedPowerMeter() { return connectedPowerMeter.c_str(); }
  const char* getconnectedHeartMonitor() { return connectedHeartMonitor.c_str(); }
  int getShifterPosition() { return shifterPosition; }

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
  void setPowerCorrectionFactor(float pm) { powerCorrectionFactor = pm; }
  void setSimulateHr(bool shr) { simulateHr = shr; }
  void setSimulateWatts(bool swt) { simulateWatts = swt; }
  void setSimulateCad(bool scd) { simulateCad = scd; }
  void setERGMode(bool erg) { ERGMode = erg; }
  void setAutoUpdate(bool atupd) { autoUpdate = atupd; }
  void setSsid(String sid) { ssid = sid; }
  void setPassword(String pwd) { password = pwd; }
  void setFoundDevices(String fdev) { foundDevices = fdev; }
  void setConnectedPowerMeter(String cpm) { connectedPowerMeter = cpm; }
  void setConnectedHeartMonitor(String cHr) { connectedHeartMonitor = cHr; }
  void setShifterPosition(int sp) { shifterPosition = sp; }

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
