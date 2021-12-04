#pragma once

#include <Arduino.h>

#define ERG_MODE_LOG_TAG "ERG_Mode"
#define ERG_MODE_DELAY   100

extern TaskHandle_t ErgTask;
void setupERG();
void ergTaskLoop(void *pvParameters);

class ErgMode {
 public:
  void computErg(int newSetpoint);
};