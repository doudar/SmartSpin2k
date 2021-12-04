#pragma once

#include <Arduino.h>

#define ERG_MODE_LOG_TAG "ERG_Mode"
#define ERG_MODE_DELAY 1000

class ErgMode {
 public:
  static void setupERG();

 private:
  static TaskHandle_t _ergTask;
  static void loop(void *pvParameters);
  static void computErg(int newSetpoint);
};