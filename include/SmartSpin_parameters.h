/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#ifndef PLATFORMIO_ENV_NATIVE
#include <Arduino.h>
#else
#include <ArduinoFake.h>
#endif

#include "settings.h"
#include <type_traits>

#define CONFIG_LOG_TAG "Config"

// Generic parameter class for simple set/get operations
template<typename T>
class Parameter {
 private:
  T value;

 public:
  void set(const T& val) { value = val; }
  T get() const { return value; }
  
  // Overload for String types to return const char*
  template<typename U = T>
  typename std::enable_if<std::is_same<U, String>::value, const char*>::type
  getCStr() const { return value.c_str(); }

  Parameter() : value{} {}
  Parameter(const T& defaultValue) : value(defaultValue) {}
  
  // Assignment operator for convenience
  Parameter& operator=(const T& val) { value = val; return *this; }
  
  // Conversion operator for convenience  
  operator T() const { return value; }
};

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

  void setMinStep(int32_t ms) { ms != INT32_MIN ? minStep = ms : minStep = -DEFAULT_STEPPER_TRAVEL; }
  int32_t getMinStep() { return minStep; }

  void setMaxStep(int32_t ms) { ms != INT32_MIN ? maxStep = ms : maxStep = DEFAULT_STEPPER_TRAVEL; }
  int32_t getMaxStep() { return maxStep; }

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
  Parameter<String> firmwareUpdateURL;
  Parameter<String> deviceName;
  Parameter<int> shiftStep;
  Parameter<bool> stealthChop;
  Parameter<float> inclineMultiplier;
  Parameter<float> powerCorrectionFactor;
  Parameter<float> ERGSensitivity;
  Parameter<bool> autoUpdate;
  Parameter<int> stepperPower;
  Parameter<int> maxWatts;
  Parameter<int> minWatts;
  Parameter<int> stepperSpeed;
  Parameter<bool> stepperDir;
  Parameter<bool> shifterDir;
  Parameter<bool> pTab4Pwr;
  Parameter<bool> udpLogEnabled;
  Parameter<int32_t> hMin;
  Parameter<int32_t> hMax;
  Parameter<bool> FTMSControlPointWrite;
  Parameter<int> homingSensitivity;
  Parameter<String> ssid;
  Parameter<String> password;
  Parameter<String> connectedPowerMeter;
  Parameter<String> connectedHeartMonitor;
  Parameter<String> connectedRemote;
  Parameter<String> foundDevices;

 public:
  void setFirmwareUpdateURL(String fURL) { firmwareUpdateURL.set(fURL); }
  const char* getFirmwareUpdateURL() { return firmwareUpdateURL.getCStr(); }

  void setDeviceName(String dvn) { deviceName.set(dvn); }
  const char* getDeviceName() { return deviceName.getCStr(); }

  void setShiftStep(int ss) { shiftStep.set(ss); }
  int getShiftStep() { return shiftStep.get(); }

  void setStealthChop(bool sc) { stealthChop.set(sc); }
  bool getStealthChop() { return stealthChop.get(); }

  void setFTMSControlPointWrite(bool cpw) { FTMSControlPointWrite.set(cpw); }
  bool getFTMSControlPointWrite() { return FTMSControlPointWrite.get(); }

  void setInclineMultiplier(float im) { inclineMultiplier.set(im); }
  float getInclineMultiplier() { return inclineMultiplier.get(); }

  void setPowerCorrectionFactor(float pcf) { powerCorrectionFactor.set(pcf); }
  float getPowerCorrectionFactor() { return powerCorrectionFactor.get(); }

  float getERGSensitivity() { return ERGSensitivity.get(); }
  void setERGSensitivity(float ergS) { ERGSensitivity.set(ergS); }

  void setAutoUpdate(bool atd) { autoUpdate.set(atd); }
  bool getAutoUpdate() { return autoUpdate.get(); }

  void setSsid(String sid) { ssid.set(sid); }
  const char* getSsid() { return ssid.getCStr(); }

  void setPassword(String pwd) { password.set(pwd); }
  const char* getPassword() { return password.getCStr(); }

  void setConnectedPowerMeter(String cpm) { connectedPowerMeter.set(cpm); }
  const char* getConnectedPowerMeter() { return connectedPowerMeter.getCStr(); }

  void setConnectedHeartMonitor(String cHr) { connectedHeartMonitor.set(cHr); }
  const char* getConnectedHeartMonitor() { return connectedHeartMonitor.getCStr(); }

  void setConnectedRemote(String cRemote) { connectedRemote.set(cRemote); }
  const char* getConnectedRemote() { return connectedRemote.getCStr(); }

  void setStepperPower(int sp) { stepperPower.set(sp); }
  int getStepperPower() { return stepperPower.get(); }

  void setStepperSpeed(int sp) { stepperSpeed.set(sp); }
  int getStepperSpeed() { return stepperSpeed.get(); }

  void setMaxWatts(int maxW) { maxWatts.set(maxW); }
  int getMaxWatts() { return maxWatts.get(); }

  void setMinWatts(int minW) { minWatts.set(minW); }
  int getMinWatts() { return minWatts.get(); }

  void setStepperDir(bool sd) { stepperDir.set(sd); }
  bool getStepperDir() { return stepperDir.get(); }

  void setShifterDir(bool shd) { shifterDir.set(shd); }
  bool getShifterDir() { return shifterDir.get(); }

  void setUdpLogEnabled(bool enabled) { udpLogEnabled.set(enabled); }
  bool getUdpLogEnabled() { return udpLogEnabled.get(); }

  void setPTab4Pwr(bool pTab) { pTab4Pwr.set(pTab); }
  bool getPTab4Pwr() { return pTab4Pwr.get(); }

  void setFoundDevices(String fdv) { foundDevices.set(fdv); }
  const char* getFoundDevices() { return foundDevices.getCStr(); }

  void setHMin(int32_t min) { hMin.set(min); }
  int32_t getHMin() { return hMin.get(); }

  void setHMax(int32_t max) { hMax.set(max); }
  int32_t getHMax() { return hMax.get(); }

  void setHomingSensitivity(int sensitivity) { homingSensitivity.set(sensitivity); }
  int getHomingSensitivity() { return homingSensitivity.get(); }

  void setDefaults();
  String returnJSON();
  void saveToLittleFS();
  void loadFromLittleFS();
  void printFile();
};
