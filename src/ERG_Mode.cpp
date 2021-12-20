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
PowerTable powerTable;

// Create a power table representing 0w-1000w in 50w increments.
// i.e. powerTable[1] corresponds to the incline required for 50w. powerTable[2] is the incline required for 100w and so on.

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
  }
}

  // Accepts new data into the table and averages input by number of readings in the power entry.
void PowerTable::newEntry(int watts, float incline, int cad) {
  int i = round(watts / 50.0);
  if (this->powerEntry[i].readings = 0) {
    this->powerEntry[i].watts    = watts;
    this->powerEntry[i].cad      = cad;
    this->powerEntry[i].incline  = incline;
    this->powerEntry[i].readings = 1;
  } else {  // Average and update the readings.  Probably should throw out outliers here as well.
    this->powerEntry[i].watts   = (watts + this->powerEntry[i].watts * this->powerEntry[i].readings) / this->powerEntry[i].readings + 1;
    this->powerEntry[i].cad     = (cad + this->powerEntry[i].cad * this->powerEntry[i].readings) / this->powerEntry[i].readings + 1;
    this->powerEntry[i].incline = (incline + this->powerEntry[i].incline * this->powerEntry[i].readings) / this->powerEntry[i].readings + 1;
    this->powerEntry[i].readings++;
    if (this->powerEntry[i].readings > 10) {
      this->powerEntry[i].readings = 10;  // keep from diluting recent readings too far.
    }
  }
}

// looks up an incline for the requested power and cadence and interpolates the result. 
float PowerTable::lookup(int watts, int cad) {
  struct entry {
    float power;
    float incline;
  };
  int i       = round(watts / 50.0); //find the closest entry
  float scale = watts / 50.0 - i;
  entry above;
  entry below;
  if (scale > 0) { //pick the element above closest entry for interpolation
    below.power   = this->powerEntry[i].watts;
    below.incline = this->powerEntry[i].incline;
    above.power   = this->powerEntry[i+1].watts;
    above.incline = this->powerEntry[i+1].incline;
  } else if (scale < 0) { //pick the element below the closest entry for interpolation
    below.power   = this->powerEntry[i-1].watts;
    below.incline = this->powerEntry[i-1].incline;
    above.power   = this->powerEntry[i].watts;
    above.incline = this->powerEntry[i].incline;
  } else {
    // Compute cad offset and return without interpolation
    //
    // Cad offset TODO HERE
    //
    return this->powerEntry->incline;
  }

  float rIncline = below.incline + ((watts - below.power) / (above.power - below.power)) * (above.incline - below.incline);

  return rIncline;
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
