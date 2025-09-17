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
 public:
  Measurement watts;
  Measurement pm_batt;
  Measurement hr;
  Measurement hr_batt;
  Measurement cad;
  Measurement resistance;

  // Parameter template members - made public for direct access
  Parameter<float> targetIncline;
  Parameter<float> simulatedSpeed;
  Parameter<uint8_t> FTMSMode;
  Parameter<int> shifterPosition;
  Parameter<bool> homed;
  Parameter<int32_t> minStep;
  Parameter<int32_t> maxStep;
  Parameter<int> minResistance;
  Parameter<int> maxResistance;
  Parameter<bool> simTargetWatts;

  // Constructor to set default values
  RuntimeParameters() {
    targetIncline.set(0.0);
    simulatedSpeed.set(0.0);
    FTMSMode.set(0x00);
    shifterPosition.set(0);
    homed.set(false);
    minStep.set(-DEFAULT_STEPPER_TRAVEL);
    maxStep.set(DEFAULT_STEPPER_TRAVEL);
    minResistance.set(-DEFAULT_RESISTANCE_RANGE);
    maxResistance.set(DEFAULT_RESISTANCE_RANGE);
    simTargetWatts.set(false);
  }

  // Special setter for MinStep and MaxStep with validation logic
  void setMinStep(int32_t ms) { ms != INT32_MIN ? minStep.set(ms) : minStep.set(-DEFAULT_STEPPER_TRAVEL); }
  void setMaxStep(int32_t ms) { ms != INT32_MIN ? maxStep.set(ms) : maxStep.set(DEFAULT_STEPPER_TRAVEL); }

  String returnJSON();
};

class userParameters {
 public:
  // Parameter template members - made public for direct access
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

  // String parameter getters that provide const char* interface for compatibility
  const char* getFirmwareUpdateURL() { return firmwareUpdateURL.getCStr(); }
  const char* getDeviceName() { return deviceName.getCStr(); }
  const char* getSsid() { return ssid.getCStr(); }
  const char* getPassword() { return password.getCStr(); }
  const char* getConnectedPowerMeter() { return connectedPowerMeter.getCStr(); }
  const char* getConnectedHeartMonitor() { return connectedHeartMonitor.getCStr(); }
  const char* getConnectedRemote() { return connectedRemote.getCStr(); }
  const char* getFoundDevices() { return foundDevices.getCStr(); }

  void setDefaults();
  String returnJSON();
  void saveToLittleFS();
  void loadFromLittleFS();
  void printFile();
};
