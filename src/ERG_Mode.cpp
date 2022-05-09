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
                          5500,          /* Stack size of task*/
                          NULL,          /* parameter of the task */
                          1,             /* priority of the task*/
                          &ErgTask,      /* Task handle to keep track of created task */
                          0);            /* pin task to core 0 */

  SS2K_LOG(ERG_MODE_LOG_TAG, "ERG Mode task started");
}

void ergTaskLoop(void* pvParameters) {
  ErgMode ergMode = ErgMode(&powerTable);
  PowerBuffer powerBuffer;

  ergMode._writeLogHeader();
  bool isInErgMode            = false;
  bool hasConnectedPowerMeter = false;
  bool simulationRunning      = false;
  int loopCounter             = 0;
  while (true) {
    vTaskDelay(ERG_MODE_DELAY / portTICK_PERIOD_MS);

    isInErgMode            = rtConfig.getERGMode();
    hasConnectedPowerMeter = spinBLEClient.connectedPM;
    simulationRunning      = rtConfig.getSimulateTargetWatts();
    if (!simulationRunning) {
      simulationRunning = rtConfig.getSimulateWatts();
    }

    // add values to power table
    powerTable.processPowerValue(powerBuffer, rtConfig.getSimulatedCad(), rtConfig.getSimulatedWatts());

    // compute ERG
    if (isInErgMode && (hasConnectedPowerMeter || simulationRunning)) {
      ergMode.computErg();
    }

    // Set Min and Max Stepper positions
    if (loopCounter > 50) {
      loopCounter = 0;
      powerTable.setStepperMinMax();
    }
    loopCounter++;

#ifdef DEBUG_STACK
    Serial.printf("ERG Task: %d \n", uxTaskGetStackHighWaterMark(ErgTask));
#endif  // DEBUG_STACK
  }
}

void PowerBuffer::set(int i) {
  this->powerEntry[i].readings       = 1;
  this->powerEntry[i].watts          = rtConfig.getSimulatedWatts().value;
  this->powerEntry[i].cad            = rtConfig.getSimulatedCad();
  this->powerEntry[i].targetPosition = rtConfig.getCurrentIncline();
}

void PowerBuffer::reset() {
  for (int i = 0; i < POWER_SAMPLES; i++) {
    this->powerEntry[i].readings       = 0;
    this->powerEntry[i].watts          = 0;
    this->powerEntry[i].cad            = 0;
    this->powerEntry[i].targetPosition = 0;
  }
}

void PowerTable::processPowerValue(PowerBuffer& powerBuffer, int cadence, Measurement watts) {
  if ((cadence > 70) && (cadence < 100) && (watts.value > 10) && (watts.value < POWERTABLE_SIZE * POWERTABLE_INCREMENT)) {
    if (powerBuffer.powerEntry[0].readings == 0) {
      powerBuffer.set(0);  // Take Initial reading
    } else if (abs(powerBuffer.powerEntry[0].watts - watts.value) < (POWERTABLE_INCREMENT / 2)) {
      for (int i = 1; i < POWER_SAMPLES; i++) {
        if (powerBuffer.powerEntry[i].readings == 0) {
          powerBuffer.set(i);  // Add additional readings to the buffer.
          break;
        }
      }
      if (powerBuffer.powerEntry[POWER_SAMPLES - 1].readings == 1) {  // If buffer is full, create a new table entry and clear the buffer.
        this->newEntry(powerBuffer);
        this->toLog();
        powerBuffer.reset();
      }
    } else {  // Reading was outside the range - clear the buffer and start over.
      powerBuffer.reset();
    }
  }
}

// Set min / max stepper position
void PowerTable::setStepperMinMax() {
  int _return = this->lookup(MIN_WATTS, 90);
  if (_return != RETURN_ERROR) {
    rtConfig.setMinStep(_return);
    SS2K_LOG(ERG_MODE_LOG_TAG, "Min Position Set: %d", _return);
  }
  _return = this->lookup(userConfig.getMaxWatts(), 90);

  if (_return != RETURN_ERROR) {
    rtConfig.setMaxStep(_return);
    SS2K_LOG(ERG_MODE_LOG_TAG, "Max Position Set: %d", _return);
  }
}

// Accepts new data into the table and averages input by number of readings in the power entry.
void PowerTable::newEntry(PowerBuffer& powerBuffer) {
  int watts              = 0;
  int cad                = 0;
  int32_t targetPosition = 0;

  for (int i = 0; i < POWER_SAMPLES; i++) {
    if (powerBuffer.powerEntry[i].readings == 0) {
      // break if powerEntry is not set
      break;
    }

    if (i == 0) {  // first loop -> assign values
      watts          = powerBuffer.powerEntry[i].watts;
      targetPosition = powerBuffer.powerEntry[i].targetPosition;
      cad            = powerBuffer.powerEntry[i].cad;
      continue;
    }

    // calculate average
    watts          = (watts + powerBuffer.powerEntry[i].watts) / 2;
    targetPosition = (targetPosition + powerBuffer.powerEntry[i].targetPosition) / 2;
    cad            = (cad + powerBuffer.powerEntry[i].cad) / 2;
  }

  int i = round(watts / POWERTABLE_INCREMENT);

  if (i == 1) {  // set the minimum resistance level of the trainer.
    rtConfig.setMinStep(this->powerEntry[i].targetPosition);
  }

  if (this->powerEntry[i].readings == 0) {  // if first reading in this entry
    this->powerEntry[i].watts          = watts;
    this->powerEntry[i].cad            = cad;
    this->powerEntry[i].targetPosition = targetPosition;
    this->powerEntry[i].readings       = 1;
  } else {  // Average and update the readings.
    this->powerEntry[i].watts          = (watts + (this->powerEntry[i].watts * this->powerEntry[i].readings)) / (this->powerEntry[i].readings + 1);
    this->powerEntry[i].cad            = (cad + (this->powerEntry[i].cad * this->powerEntry[i].readings)) / (this->powerEntry[i].readings + 1);
    this->powerEntry[i].targetPosition = (targetPosition + (this->powerEntry[i].targetPosition * this->powerEntry[i].readings)) / (this->powerEntry[i].readings + 1);
    this->powerEntry[i].readings++;
    if (this->powerEntry[i].readings > 10) {
      this->powerEntry[i].readings = 10;  // keep from diluting recent readings too far.
    }
  }
}

// looks up an incline for the requested power and cadence and interpolates the result.
// Returns -99 if no entry matched.
int32_t PowerTable::lookup(int watts, int cad) {
  struct entry {
    float power;
    int32_t targetPosition;
    float cad;
  };

  int i         = round(watts / POWERTABLE_INCREMENT);  // find the closest entry
  float scale   = watts / POWERTABLE_INCREMENT - i;     // Should we look at the next higher or next lower index for comparison?
  int indexPair = -1;                                   // The next closes index with data for interpolation
  entry above;
  entry below;
  above.power = 0;
  below.power = 0;

  if (this->powerEntry[i].readings == 0) {  // If matching entry is empty, find the next closest index with data
    for (int x = 1; x < POWERTABLE_SIZE; x++) {
      if (i + x < POWERTABLE_SIZE) {
        if (this->powerEntry[i + x].readings > 0) {
          i += x;
          break;
        }
      }
      if (i - x >= 0) {
        if (this->powerEntry[i - x].readings > 0) {
          i -= x;
          break;
        }
      }
      if ((i - x <= 0) && (i + x >= POWERTABLE_SIZE)) {
        SS2K_LOG(ERG_MODE_LOG_TAG, "No data found in powertable.");
        return RETURN_ERROR;
      }
    }
  }
  if (scale > 0) {  // select the paired element (preferably) above the entry for interpolation
    for (int x = 1; x < POWERTABLE_SIZE; x++) {
      if (i + x < POWERTABLE_SIZE) {
        if (this->powerEntry[i + x].readings > 0) {
          indexPair = i + x;
          break;
        }
      }
      if (i - x >= 0) {
        if (this->powerEntry[i - x].readings > 0) {
          indexPair = i - x;
          break;
        }
      }
    }
  } else if (scale <= 0) {  // select the paired element (preferably) below the entry for interpolation
    for (int x = 1; x < POWERTABLE_SIZE; x++) {
      if (i + x < POWERTABLE_SIZE) {
        if (this->powerEntry[i + x].readings > 0) {
          indexPair = i + x;
          break;
        }
      }
      if (i - x >= 0) {
        if (this->powerEntry[i - x].readings > 0) {
          indexPair = i - x;
          break;
        }
      }
    }
  }

  if (indexPair != -1) {
    if (i > indexPair) {
      below.power          = this->powerEntry[indexPair].watts;
      below.targetPosition = this->powerEntry[indexPair].targetPosition;
      below.cad            = this->powerEntry[indexPair].cad;
      above.power          = this->powerEntry[i].watts;
      above.targetPosition = this->powerEntry[i].targetPosition;
      above.cad            = this->powerEntry[i].cad;
    } else if (i < indexPair) {
      below.power          = this->powerEntry[i].watts;
      below.targetPosition = this->powerEntry[i].targetPosition;
      below.cad            = this->powerEntry[i].cad;
      above.power          = this->powerEntry[indexPair].watts;
      above.targetPosition = this->powerEntry[indexPair].targetPosition;
      above.cad            = this->powerEntry[indexPair].cad;
    }
    if (below.targetPosition >= above.targetPosition) {
      SS2K_LOG(ERG_MODE_LOG_TAG, "Reverse/No Delta in PowerTable");
      return (RETURN_ERROR);
    }
  } else {  // Not enough data
    SS2K_LOG(ERG_MODE_LOG_TAG, "No pair in power table");
    return (RETURN_ERROR);
  }
  SS2K_LOG(ERG_MODE_LOG_TAG, "PowerTable pairs [%d][%d]", i, indexPair);

  // @MarkusSchneider's data shows a linear relationship between CAD and Watts for a given resistance level.
  // It looks like for every 20 CAD increase there is ~50w increase in power. This may need to be adjusted later
  // as higher resistance levels have a steeper slope (bigger increase in power/cad) than low resistance levels.
  float averageCAD = (below.cad + above.cad) / 2;
  float deltaCAD   = abs(averageCAD - cad);

  if (deltaCAD > 5) {
    if (cad > averageCAD) {  // cad is higher than the table so we need to target a lower wattage (and targetPosition)
      watts -= (deltaCAD / 20) * 50;
    }
    if (cad < averageCAD) {  // cad is lower than the table so we need to target a higher wattage (and targetPosition)
      watts += (deltaCAD / 20) * 50;
    }
  }
  // actual interpolation
  int32_t rTargetPosition = below.targetPosition + ((watts - below.power) / (above.power - below.power)) * (above.targetPosition - below.targetPosition);

  return rTargetPosition;
}

bool PowerTable::load() {
  // load power table from littlefs
  return false;  // return unsuccessful
}

bool PowerTable::save() {
  // save powertable from littlefs
  return false;  // return unsuccessful
}

// Display power table in log
void PowerTable::toLog() {
  int len = 4;
  for (int i = 0; i < POWERTABLE_SIZE; i++) {  // Find the longest integer to dynamically size the power table
    int l = snprintf(nullptr, 0, "%d", this->powerEntry[i].targetPosition);
    if (len < l) {
      len = l;
    }
  }
  char buffer[len + 2];
  String oString  = "";
  char oFormat[5] = "";
  sprintf(oFormat, "|%%%dd", len);

  for (int i = 0; i < POWERTABLE_SIZE; i++) {
    sprintf(buffer, oFormat, this->powerEntry[i].watts);
    oString += buffer;
  }
  SS2K_LOG(POWERTABLE_LOG_TAG, "%s|", oString.c_str());
  oString = "";
  for (int i = 0; i < POWERTABLE_SIZE; i++) {
    sprintf(buffer, oFormat, this->powerEntry[i].cad);
    oString += buffer;
  }
  SS2K_LOG(POWERTABLE_LOG_TAG, "%s|", oString.c_str());
  oString = "";
  for (int i = 0; i < POWERTABLE_SIZE; i++) {
    sprintf(buffer, oFormat, this->powerEntry[i].targetPosition);
    oString += buffer;
  }
  SS2K_LOG(POWERTABLE_LOG_TAG, "%s|", oString.c_str());
}

// as a note, Trainer Road sends 50w target whenever the app is connected.
void ErgMode::computErg() {
  Measurement newWatts = rtConfig.getSimulatedWatts();
  float currentIncline = rtConfig.getCurrentIncline();
  int newCadence       = rtConfig.getSimulatedCad();
  int newSetPoint      = rtConfig.getTargetWatts();

  // check for new power value or new setpoint, if watts < 10 treat as faulty
  if ((this->watts.timestamp == newWatts.timestamp && this->setPoint == newSetPoint) || newWatts.value < 10) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "Watts were old.");
    return;
  }

  // set minimum SetPoint to 50 watt if trainer sends setpoints lower than 50 watt.
  if (newSetPoint < 50) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "ERG Target Below Minumum Value.");
    newSetPoint = 50;
  }

  bool isUserSpinning = this->_userIsSpinning(newCadence, currentIncline);
  if (!isUserSpinning) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "ERG Mode but no userspin");
    return;
  }

  // SetPoint changed
  if (this->setPoint != newSetPoint) {
    _setPointChangeState(newSetPoint, newCadence, newWatts, currentIncline);
    SS2K_LOG(ERG_MODE_LOG_TAG, "SetPoint changed");
    return;
  }

  // Setpoint unchanged
  _inSetpointState(newSetPoint, newCadence, newWatts, currentIncline);
}

void ErgMode::_setPointChangeState(int newSetPoint, int newCadence, Measurement& newWatts, float currentIncline) {
  this->cycle         = 0;
  int32_t tableResult = powerTable->lookup(newSetPoint, newCadence);
  if (tableResult == RETURN_ERROR) {
    int wattChange  = newSetPoint - newWatts.value;
    float diviation = ((float)wattChange * 100.0) / ((float)newSetPoint);
    float factor    = abs(diviation) > 10 ? userConfig.getERGSensitivity() : userConfig.getERGSensitivity() / 2;
    tableResult     = currentIncline + (wattChange * factor);
  }

  SS2K_LOG(ERG_MODE_LOG_TAG, "Using PowerTable Result %d", tableResult);
  _updateValues(newSetPoint, newCadence, newWatts, currentIncline, tableResult);

  int i = 0;
  while (rtConfig.getTargetIncline() != rtConfig.getCurrentIncline()) {  // wait while the knob moves to target position.
    vTaskDelay(100 / portTICK_PERIOD_MS);
    if (i > 50) {  // failsafe for infinate loop
      SS2K_LOG(ERG_MODE_LOG_TAG, "Stepper didn't reach target position");
      break;
    }
    i++;
  }

  vTaskDelay(700 / portTICK_PERIOD_MS);  // Wait for power meter to register new power
}

void ErgMode::_inSetpointState(int newSetPoint, int newCadence, Measurement& newWatts, float currentIncline) {
  int watts = newWatts.value;

  // // wait for 3 Cycles (Seconds 3) after setPoint changed -> Power should now be stable
  // if (this->watts.value > 0 && this->cycle > 3) {
  //   watts = newWatts.value + this->watts.value;  // build arg watts to flat measurement failure
  // }

  int wattChange  = newSetPoint - watts;  // setpoint_form_trainer - current_power => Amount to increase or decrease incline
  float diviation = ((float)wattChange * 100.0) / ((float)newSetPoint);

  float factor     = abs(diviation) > 10 ? userConfig.getERGSensitivity() : userConfig.getERGSensitivity() / 2;
  float newIncline = currentIncline + (wattChange * factor);

  _updateValues(newSetPoint, newCadence, newWatts, currentIncline, newIncline);
}

void ErgMode::_updateValues(int newSetPoint, int newCadence, Measurement& newWatts, float currentIncline, float newIncline) {
  rtConfig.setTargetIncline(newIncline);
  _writeLog(this->cycle, currentIncline, newIncline, this->setPoint, newSetPoint, this->watts.value, newWatts.value, this->cadence, newCadence);

  this->watts    = newWatts;
  this->setPoint = newSetPoint;
  this->cadence  = newCadence;
  this->cycle++;
}

bool ErgMode::_userIsSpinning(int cadence, float incline) {
  if (cadence <= 20) {
    if (!this->engineStopped) {                              // Test so motor stop command only happens once.
      ss2k.motorStop();                                      // release tension
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
