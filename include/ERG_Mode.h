#pragma once

#include <Arduino.h>
#include "SmartSpin_parameters.h"

#define ERG_MODE_LOG_TAG "ERG_Mode"
#define ERG_MODE_DELAY   100

extern TaskHandle_t ErgTask;
void setupERG();
void ergTaskLoop(void *pvParameters);

class ErgMode {
 public:
  void computErg(int newSetpoint);

 private:
  bool engineStopped = false;
  int setPoint       = 0;
  Measurement watts  = Measurement(0);
};