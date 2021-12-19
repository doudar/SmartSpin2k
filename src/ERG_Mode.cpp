/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ERG_Mode.h"
#include "SS2KLog.h"
#include "Main.h"

TaskHandle_t ErgTask;

// Create a power table representing 0w-1000w in 50w increments.
// i.e. powerTable[1] corresponds to the incline required for 50w. powerTable[2] is the incline required for 100w and so on.

PowerEntry ergPowerTable[20];

void setupERG() {
  TaskHandle_t task_handle;
  SS2K_LOG(ERG_MODE_LOG_TAG, "Starting ERG Mode task...");
  xTaskCreatePinnedToCore(ergTaskLoop,   /* Task function. */
                          "ERGModeTask", /* name of task. */
                          2500,          /* Stack size of task*/
                          NULL,          /* parameter of the task */
                          1,             /* priority of the task*/
                          &ErgTask,      /* Task handle to keep track of created task */
                          0);            /* pin task to core 0 */

  SS2K_LOG(ERG_MODE_LOG_TAG, "ERG Mode task started");
}

void ergTaskLoop(void* pvParameters) {
  ErgMode ergMode;
  ergMode._writeLogHeader();
  while (true) {
    vTaskDelay(ERG_MODE_DELAY / portTICK_PERIOD_MS);

    int newSetPoint             = rtConfig.getTargetWatts();
    bool isInErgMode            = rtConfig.getERGMode();
    bool hasConnectedPowerMeter = spinBLEClient.connectedPM;
    bool simulationRunning      = rtConfig.getSimulateTargetWatts();

    if (isInErgMode && (hasConnectedPowerMeter || simulationRunning)) {
      ergMode.computErg(newSetPoint);
    } else {
      if (newSetPoint > 0) {
        SS2K_LOG(ERG_MODE_LOG_TAG, "ERG request but ERG off or no connected PM");
      }
    }

#ifdef DEBUG_STACK
    Serial.printf("ERG Task: %d \n", uxTaskGetStackHighWaterMark(ErgTask));
#endif  // DEBUG_STACK

    // collect bike charateristics
    // if (hasConnectedPowerMeter) {
    //   float incline = rtConfig.getCurrentIncline();
    //   int cadence   = rtConfig.getSimulatedCad();
    //   int power     = rtConfig.getSimulatedWatts().value;
    //   SS2K_LOG(ERG_MODE_LOG_TAG, "Bike characteristics: INC: %f; CAD: %d; PWR: %d", incline, cadence, power);
    // }

    /* Continuously build and update powertable here. Obviously this won't work (boot to boot at least) without some sort of homing routine to begin with. 
      
      Method should be if power has been held within 20w for ~3 seconds, update the corresponding ergPowerTable[x] entry by rounding the watts to the nearest 50:
      
      int x = rtConfig.getSimulatedWatts() + 50/2;
       x -= x % 50; 
       x = x/50; 
       Then update the power table:
      if (!ergPowerTable[x].watts){
        ergPowerTable[x].watts = rtConfig.getSimulatedWatts();
        ergPowerTable[x].cad = rtConfig.getSimulatedCad(); 
        ergPowerTable[x].incline = rtConfig.getSimulatedIncline();
        ergPowerTable[x].readings = 1;
      } else{ //Average and update the readings.  Probably should throw out outliers here as well.  
        ergPowerTable[x].watts = (rtConfig.getSimulatedWatts() + ergPowerTable[x].watts * ergPowerTable[x].readings)/ergPowerTable.readings[x]+1;
        ergPowerTable[x].cad = (rtConfig.getSimulatedCad() + ERGPowerTable[x].cad * ergPowerTable[x].readings)/ergPowerTable.readings[x]+1; 
        ergPowerTable[x].incline = rtConfig.getSimulatedIncline() + ERGPowerTable[x].incline *ERGPowerTable[x].readings)/ERGPowerTable.readings[x]+1; 
        ergPowerTable[x].readings ++;
        if(ergPowerTable[x].readings > 10){
          ergPowerTable.readings = 10; //keep from diluting recent readings too far. 
        }
      }

    */
  }
}

// as a note, Trainer Road sends 50w target whenever the app is connected.
void ErgMode::computErg(int newSetPoint) {
  Measurement newWatts = rtConfig.getSimulatedWatts();
  float currentIncline = rtConfig.getCurrentIncline();
  int newCadance       = rtConfig.getSimulatedCad();
  int wattChange       = newSetPoint - newWatts.value;  // setpoint_form_trainer - current_power => Amount to increase or decrease incline
  float diviation      = ((float)wattChange * 100.0) / ((float)newSetPoint);

  if (watts.timestamp == newWatts.timestamp && this->setPoint == newSetPoint) {
    return;  // no new power measurement.
  }

  // set minimum SetPoint to 50 watt if trainer sends setpoints lower than 50 watt.
  if (newSetPoint < 50) {
    newSetPoint = 50;
  }

  // reset cycle counter to start new erg calculation cycle
  if (this->setPoint != newSetPoint) {
    this->cycles = 0;
  }

  bool isUserSpinning = this->_userIsSpinning(newCadance, currentIncline);
  if (!isUserSpinning) {
    return;
  }

  float factor     = abs(diviation) > 10 ? userConfig.getERGSensitivity() : userConfig.getERGSensitivity() / 2;
  float newIncline = currentIncline + (wattChange * factor);

  rtConfig.setTargetIncline(newIncline);
  _writeLog(this->cycles, currentIncline, newIncline, setPoint, newSetPoint, this->watts.value, newWatts.value, this->cadence, newCadance);

  // store for next cycle for timestamp and value compare
  this->watts    = newWatts;
  this->setPoint = newSetPoint;
  this->cadence  = newCadance;
  this->cycles++;
}

bool ErgMode::_userIsSpinning(int cadance, float incline) {
  if (cadance <= 20) {
    if (!this->engineStopped) {                              // Test so motor stop command only happens once.
      motorStop();                                           // release tension
      rtConfig.setTargetIncline(incline - WATTS_PER_SHIFT);  // release incline
      this->engineStopped = true;
    }
    return false;  // Cadence too low, nothing to do here
  }

  this->engineStopped = false;
  return true;
}

void ErgMode::_writeLogHeader() {
  SS2K_LOG(ERG_MODE_LOG_CSV_TAG, "cycles;current incline;new incline;current setpoint;new setpoint;current watts;new watts;current cadence;new cadence;");
}

void ErgMode::_writeLog(int cycles, float currentIncline, float newIncline, int currentSetPoint, int newSetPoint, int currentWatts, int newWatts, int currentCadence,
                        int newCadence) {
  SS2K_LOG(ERG_MODE_LOG_CSV_TAG, "%d;%.2f;%.2f;%d;%d;%d;%d;%d;%d", cycles, currentIncline, newIncline, currentSetPoint, newSetPoint, currentWatts, newWatts, currentCadence,
           newCadence);
}
