#pragma once

#include <Arduino.h>
#include "SmartSpin_parameters.h"

#define ERG_MODE_LOG_TAG "ERG_Mode"
#define ERG_MODE_DELAY   250

extern TaskHandle_t ErgTask;
void setupERG();
void ergTaskLoop(void *pvParameters);

class ErgMode {
 public:
  void computErg(int newSetpoint);

 private:
  bool engineStopped  = false;
  bool initialized    = false;
  int setPoint        = 0;
  int cycles          = 0;
  int offsetMuliplier = 0;
  Measurement watts   = Measurement(0);

  bool _userIsSpinning(int cadence, float incline);
};
