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
TorqueTable torqueTable;

// Create a torque table representing 0w-1000w in 50w increments.
// i.e. torqueTable[1] corresponds to the incline required for 50w. torqueTable[2] is the incline required for 100w and so on.

void setupERG() {
  SS2K_LOG(ERG_MODE_LOG_TAG, "Starting ERG Mode task...");
  xTaskCreatePinnedToCore(ergTaskLoop,    /* Task function. */
                          "FTMSModeTask", /* name of task. */
                          5500,           /* Stack size of task*/
                          NULL,           /* parameter of the task */
                          1,              /* priority of the task*/
                          &ErgTask,       /* Task handle to keep track of created task */
                          0);             /* pin task to core 0 */

  SS2K_LOG(ERG_MODE_LOG_TAG, "ERG Mode task started");
}

void ergTaskLoop(void* pvParameters) {
  ErgMode ergMode = ErgMode(&torqueTable);
  TorqueBuffer torqueBuffer;

  ergMode._writeLogHeader();
  bool isInErgMode            = false;
  bool hasConnectedPowerMeter = false;
  bool simulationRunning      = false;
  int loopCounter             = 0;

  while (true) {
    vTaskDelay(ERG_MODE_DELAY / portTICK_PERIOD_MS);

    hasConnectedPowerMeter = spinBLEClient.connectedPM;
    simulationRunning      = rtConfig.watts.getTarget();
    if (!simulationRunning) {
      simulationRunning = rtConfig.watts.getSimulate();
    }

    // add values to torque table
    torqueTable.processTorqueValue(torqueBuffer, rtConfig.cad.getValue(), rtConfig.watts);

    // compute ERG
    if ((rtConfig.getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetPower) && (hasConnectedPowerMeter || simulationRunning)) {
      ergMode.computeErg();
    }

    // resistance mode
    if ((rtConfig.getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetResistanceLevel) && (rtConfig.getMaxResistance() != DEFAULT_RESISTANCE_RANGE)) {
      ergMode.computeResistance();
    }

    // Set Min and Max Stepper positions
    if (loopCounter > 50) {
      loopCounter = 0;
      torqueTable.setStepperMinMax();
    }

    // Check for saved torque table if we have < 3 positions calculated. If so, load saved torque table.

    // Save Torque table if we currently have more positions calculated than the saved version.

    loopCounter++;

#ifdef DEBUG_STACK
    Serial.printf("ERG Task: %d \n", uxTaskGetStackHighWaterMark(ErgTask));
#endif  // DEBUG_STACK
  }
}

void TorqueBuffer::set(int i) {
  this->torqueEntry[i].readings       = 1;
  this->torqueEntry[i].torque         = _wattsToTorque(rtConfig.watts.getValue(), rtConfig.cad.getValue());
  this->torqueEntry[i].targetPosition = rtConfig.getCurrentIncline();
}

void TorqueBuffer::reset() {
  for (int i = 0; i < TORQUE_SAMPLES; i++) {
    this->torqueEntry[i].readings       = 0;
    this->torqueEntry[i].torque         = 0;
    this->torqueEntry[i].targetPosition = 0;
  }
}

void TorqueTable::processTorqueValue(TorqueBuffer& torqueBuffer, int cadence, Measurement watts) {
  float torque = _wattsToTorque(watts.getValue(), cadence);
  if ((cadence >= (NORMAL_CAD - 20)) && (cadence <= (NORMAL_CAD + 20)) && (watts.getValue() > 10) && (torque < (TORQUETABLE_SIZE * TORQUETABLE_INCREMENT))) {
    if (torqueBuffer.torqueEntry[0].readings == 0) {
      // Take Initial reading
      torqueBuffer.set(0);
      // Check that reading is within 1/2 of the initial reading
    } else if (abs(torqueBuffer.torqueEntry[0].torque - torque) < (TORQUETABLE_INCREMENT / 2)) {
      for (int i = 1; i < TORQUE_SAMPLES; i++) {
        if (torqueBuffer.torqueEntry[i].readings == 0) {
          torqueBuffer.set(i);  // Add additional readings to the buffer.
          break;
        }
      }
      if (torqueBuffer.torqueEntry[TORQUE_SAMPLES - 1].readings == 1) {  // If buffer is full, create a new table entry and clear the buffer.
        this->newEntry(torqueBuffer);
        this->toLog();
        torqueBuffer.reset();
      }
    } else {  // Reading was outside the range - clear the buffer and start over.
      torqueBuffer.reset();
    }
  }
}

// Set min / max stepper position
void TorqueTable::setStepperMinMax() {
  int _return = RETURN_ERROR;

  // if the FTMS device reports resistance feedback, skip estimating min_max
  if (rtConfig.resistance.getValue() > 0) {
    rtConfig.setMinStep(-DEFAULT_STEPPER_TRAVEL);
    rtConfig.setMaxStep(DEFAULT_STEPPER_TRAVEL);
    SS2K_LOG(ERG_MODE_LOG_TAG, "Using Resistance Travel Limits");
    return;
  }

  int minBreakWatts = userConfig.getMinWatts();
  if (minBreakWatts > 0) {
    _return = this->lookup(minBreakWatts, NORMAL_CAD);
    if (_return != RETURN_ERROR) {
      // never set less than one shift below current incline.
      if ((_return >= rtConfig.getCurrentIncline()) && (rtConfig.watts.getValue() > userConfig.getMinWatts())) {
        _return = rtConfig.getCurrentIncline() - userConfig.getShiftStep();
      }
      rtConfig.setMinStep(_return);
      SS2K_LOG(ERG_MODE_LOG_TAG, "Min Position Set: %d", _return);
    }
  }

  int maxBreakWatts = userConfig.getMaxWatts();
  if (maxBreakWatts > 0) {
    _return = this->lookup(maxBreakWatts, NORMAL_CAD);
    if (_return != RETURN_ERROR) {
      // never set less than one shift above current incline.
      if ((_return <= rtConfig.getCurrentIncline()) && (rtConfig.watts.getValue() < userConfig.getMaxWatts())) {
        _return = rtConfig.getCurrentIncline() + userConfig.getShiftStep();
      }
      rtConfig.setMaxStep(_return);
      SS2K_LOG(ERG_MODE_LOG_TAG, "Max Position Set: %d", _return);
    }
  }
}

// Accepts new data into the table and averages input by number of readings in the torque entry.
void TorqueTable::newEntry(TorqueBuffer& torqueBuffer) {
  float torque           = 0;
  int32_t targetPosition = 0;

  for (int i = 0; i < TORQUE_SAMPLES; i++) {
    if (torqueBuffer.torqueEntry[i].readings == 0) {
      // break if torqueEntry is not set. This should never happen.
      break;
    }

    if (i == 0) {  // first loop -> assign values
      torque         = torqueBuffer.torqueEntry[i].torque;
      targetPosition = torqueBuffer.torqueEntry[i].targetPosition;
      continue;
    }
#ifdef DEBUG_TORQUETABLE
    SS2K_LOGW(TORQUETABLE_LOG_TAG, "Buf[%d](%dw)(%dpos)(%dcad)", i, torqueBuffer.torqueEntry[i].torque, torqueBuffer.torqueEntry[i].targetPosition,
              torqueBuffer.torqueEntry[i].cad);
#endif
    // calculate average
    torque         = (torque + torqueBuffer.torqueEntry[i].torque) / 2;
    targetPosition = (targetPosition + torqueBuffer.torqueEntry[i].targetPosition) / 2;
  }
#ifdef DEBUG_TORQUETABLE
  SS2K_LOG(TORQUETABLE_LOG_TAG, "Avg:(%dw)(%dpos)(%dcad)", (int)torque, targetPosition, cad);
#endif
  // Done with torqueBuffer
  // To start working on the TorqueTable, we need to calculate position in the table for the new entry
  int i = round(torque / TORQUETABLE_INCREMENT);

  // Prohibit entries that are less than the number to the left
  if (i > 0) {
    for (int j = i - 1; j > 0; j--) {
      if ((this->torqueEntry[j].targetPosition != 0) && (this->torqueEntry[j].targetPosition >= targetPosition)) {
        SS2K_LOG(TORQUETABLE_LOG_TAG, "Target Slot (%dw)(%d)(%d) was less than previous (%d)(%d)", (int)torque, i, targetPosition, j, this->torqueEntry[j].targetPosition);
        this->torqueEntry[j].readings = 1;  // Make previous slot easier to round/faster to change.
        return;
      }
    }
  }
  // Prohibit entries that are greater than the number to the right
  if (i < TORQUETABLE_SIZE) {
    for (int j = i + 1; j < TORQUETABLE_SIZE; j++) {
      if ((this->torqueEntry[j].targetPosition != 0) && (targetPosition >= this->torqueEntry[j].targetPosition)) {
        SS2K_LOG(TORQUETABLE_LOG_TAG, "Target Slot (%dw)(%d)(%d) was greater than next (%d)(%d)", (int)torque, i, targetPosition, j, this->torqueEntry[j].targetPosition);
        this->torqueEntry[j].readings = 1;  // Make next slot easier to round/faster to change.
        return;
      }
    }
  }

  if (this->torqueEntry[i].readings == 0) {  // if first reading in this entry
    this->torqueEntry[i].torque         = torque;
    this->torqueEntry[i].targetPosition = targetPosition;
    this->torqueEntry[i].readings       = 1;
  } else {  // Average and update the readings.
    this->torqueEntry[i].torque         = (torque + (this->torqueEntry[i].torque * this->torqueEntry[i].readings)) / (this->torqueEntry[i].readings + 1.0);
    this->torqueEntry[i].targetPosition = (targetPosition + (this->torqueEntry[i].targetPosition * this->torqueEntry[i].readings)) / (this->torqueEntry[i].readings + 1.0);
    this->torqueEntry[i].readings++;
    if (this->torqueEntry[i].readings > 10) {
      this->torqueEntry[i].readings = 10;  // keep from diluting recent readings too far.
    }
  }
}

// looks up an incline for the requested watts and cadence and interpolates the result.
// Returns -99 if no entry matched.
int32_t TorqueTable::lookup(int watts, int cad) {
  struct entry {
    float torque;
    int32_t targetPosition;
  };

  float torque = _wattsToTorque(watts, cad);

  int i         = round(torque / TORQUETABLE_INCREMENT);  // find the closest entry
  float scale   = torque / TORQUETABLE_INCREMENT - i;     // Should we look at the next higher or next lower index for comparison?
  int indexPair = -1;  // The next closest index with data for interpolation                                                                           // The next closest index
                       // with data for interpolation
  entry above;
  entry below;
  above.torque = 0;
  below.torque = 0;

  if (this->torqueEntry[i].readings == 0) {  // If matching entry is empty, find the next closest index with data
    for (int x = 1; x < TORQUETABLE_SIZE; x++) {
      if (i + x < TORQUETABLE_SIZE) {
        if (this->torqueEntry[i + x].readings > 0) {
          i += x;
          break;
        }
      }
      if (i - x >= 0) {
        if (this->torqueEntry[i - x].readings > 0) {
          i -= x;
          break;
        }
      }
      if ((i - x <= 0) && (i + x >= TORQUETABLE_SIZE)) {
        SS2K_LOG(ERG_MODE_LOG_TAG, "No data found in Torque Table.");
        return RETURN_ERROR;
      }
    }
  }
  if (scale > 0) {  // select the paired element (preferably) above the entry for interpolation
    for (int x = 1; x < TORQUETABLE_SIZE; x++) {
      if (i + x < TORQUETABLE_SIZE) {
        if (this->torqueEntry[i + x].readings > 0) {
          indexPair = i + x;
          break;
        }
      }
      if (i - x >= 0) {
        if (this->torqueEntry[i - x].readings > 0) {
          indexPair = i - x;
          break;
        }
      }
    }
  } else if (scale <= 0) {  // select the paired element (preferably) below the entry for interpolation
    for (int x = 1; x < TORQUETABLE_SIZE; x++) {
      if (i + x < TORQUETABLE_SIZE) {
        if (this->torqueEntry[i + x].readings > 0) {
          indexPair = i + x;
          break;
        }
      }
      if (i - x >= 0) {
        if (this->torqueEntry[i - x].readings > 0) {
          indexPair = i - x;
          break;
        }
      }
    }
  }

  if (indexPair != -1) {
    if (i > indexPair) {
      below.torque         = this->torqueEntry[indexPair].torque;
      below.targetPosition = this->torqueEntry[indexPair].targetPosition;
      above.torque         = this->torqueEntry[i].torque;
      above.targetPosition = this->torqueEntry[i].targetPosition;
    } else if (i < indexPair) {
      below.torque         = this->torqueEntry[i].torque;
      below.targetPosition = this->torqueEntry[i].targetPosition;
      above.torque         = this->torqueEntry[indexPair].torque;
      above.targetPosition = this->torqueEntry[indexPair].targetPosition;
    }
    if (below.targetPosition >= above.targetPosition) {
      SS2K_LOG(ERG_MODE_LOG_TAG, "Reverse/No Delta in Torque Table");
      return (RETURN_ERROR);
    }
  } else {  // Not enough data
    SS2K_LOG(ERG_MODE_LOG_TAG, "No pair in torque table");
    return (RETURN_ERROR);
  }
  SS2K_LOG(ERG_MODE_LOG_TAG, "TorqueTable pairs [%d][%d]", i, indexPair);

  if (!below.torque || !above.torque) {  // We should never get here. This is a failsafe vv
    SS2K_LOG(ERG_MODE_LOG_TAG, "One of the pair was zero. Calculation rejected.");
    return (RETURN_ERROR);
  }

  // actual interpolation
  int32_t rTargetPosition = below.targetPosition + ((torque - below.torque) / (above.torque - below.torque)) * (above.targetPosition - below.targetPosition);

  return rTargetPosition;
}

bool TorqueTable::load() {
  // load torque table from littleFs
  return false;  // return unsuccessful
}

bool TorqueTable::save() {
  // save torque table from littleFs
  return false;  // return unsuccessful
}

float _wattsToTorque(int watts, float cad) {
  float torque = (TORQUE_CONSTANT * watts) / cad + ((NORMAL_CAD-cad)/CAD_MULTIPLIER);
  return torque;
}

int _torqueToWatts(float torque, float cad) {
  int watts = ((CAD_MULTIPLIER*cad*torque)+((cad*cad)-cad*NORMAL_CAD))/(TORQUE_CONSTANT*CAD_MULTIPLIER);
  return watts;
}

// Display torque table in log
void TorqueTable::toLog() {
  int len = 4;
  for (int i = 0; i < TORQUETABLE_SIZE; i++) {  // Find the longest integer to dynamically size the torque table
    int l = snprintf(nullptr, 0, "%d", this->torqueEntry[i].targetPosition);
    if (len < l) {
      len = l;
    }
  }
  char buffer[len + 2];
  String oString   = "";
  char oFormatT[5] = "";
  sprintf(oFormatT, "|%%%dd", len);
  char oFormatP[5] = "";
  sprintf(oFormatP, "|%%%dd", len);

  for (int i = 0; i < TORQUETABLE_SIZE; i++) {
    sprintf(buffer, oFormatT, (int)this->torqueEntry[i].torque);
    oString += buffer;
  }
  SS2K_LOG(TORQUETABLE_LOG_TAG, "%s|", oString.c_str());
  oString = "";

  for (int i = 0; i < TORQUETABLE_SIZE; i++) {
    sprintf(buffer, oFormatP, this->torqueEntry[i].targetPosition);
    oString += buffer;
  }
  SS2K_LOG(TORQUETABLE_LOG_TAG, "%s|", oString.c_str());
}

// compute position for resistance control mode
void ErgMode::computeResistance() {
  static int stepChangePerResistance = userConfig.getShiftStep();
  static Measurement oldResistance;

  if (rtConfig.resistance.getTimestamp() == oldResistance.getTimestamp()) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "Resistance previously processed.");
    return;
  }

  int newSetPoint = rtConfig.resistance.getTarget();
  int actualDelta = rtConfig.resistance.getTarget() - rtConfig.resistance.getValue();
  // SS2K_LOG(ERG_MODE_LOG_TAG, "StepChange %d TargetDelta %d ActualDelta %d OldSetPoint %d NewSetPoint %d", stepChangePerResistance, targetDelta, actualDelta, oldSetPoint,
  // newSetPoint);

  // if (rtConfig.getCurrentIncline() == rtConfig.getTargetIncline()) {
  //  if (actualDelta > 0) {
  //    rtConfig.setTargetIncline(rtConfig.getTargetIncline() + (100 * actualDelta));
  // SS2K_LOG(ERG_MODE_LOG_TAG, "adjusting target up");
  //  SS2K_LOG(ERG_MODE_LOG_TAG, "First run shift up");
  //}
  //   if (actualDelta < 0) {
  rtConfig.setTargetIncline(rtConfig.getTargetIncline() + (100 * actualDelta));
  // SS2K_LOG(ERG_MODE_LOG_TAG, "adjusting target down");
  // SS2K_LOG(ERG_MODE_LOG_TAG, "First run shift down");
  // }
  //}
  if (actualDelta = 0) {
    rtConfig.setTargetIncline(rtConfig.getCurrentIncline());
    // SS2K_LOG(ERG_MODE_LOG_TAG, "Set point Reached - stopping");
  }
  oldResistance = rtConfig.resistance;
}
//}

// as a note, Trainer Road sends 50w target whenever the app is connected.
void ErgMode::computeErg() {
  Measurement newWatts = rtConfig.watts;
  int newCadence       = rtConfig.cad.getValue();

  // check for new torque value or new set point, if watts < 10 treat as faulty
  if ((this->watts.getTimestamp() == newWatts.getTimestamp() && this->setPoint == newWatts.getTarget()) || newWatts.getValue() < 10) {
    SS2K_LOGW(ERG_MODE_LOG_TAG, "Watts previously processed.");
    return;
  }

  // set minimum set point to minimum bike watts if app sends set point lower than minimum bike watts.
  if (newWatts.getTarget() < userConfig.getMinWatts()) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "ERG Target Below Minumum Value.");
    newWatts.setTarget(userConfig.getMinWatts());
  }

  bool isUserSpinning = this->_userIsSpinning(newCadence, rtConfig.getCurrentIncline());
  if (!isUserSpinning) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "ERG Mode but no User Spin");
    return;
  }

  // SetPoint changed
  if (this->setPoint != newWatts.getTarget()) {
    _setPointChangeState(newCadence, newWatts);
    return;
  }

  // Setpoint unchanged
  _inSetpointState(newCadence, newWatts);
}

void ErgMode::_setPointChangeState(int newCadence, Measurement& newWatts) {
  int32_t tableResult = torqueTable->lookup(newWatts.getTarget(), newCadence);
  if (tableResult == RETURN_ERROR) {
    int wattChange  = newWatts.getTarget() - newWatts.getValue();
    float deviation = ((float)wattChange * 100.0) / ((float)newWatts.getTarget());
    float factor    = abs(deviation) > 10 ? userConfig.getERGSensitivity() : userConfig.getERGSensitivity() / 2;
    tableResult     = rtConfig.getCurrentIncline() + (wattChange * factor);
  }

  SS2K_LOG(ERG_MODE_LOG_TAG, "SetPoint changed:%dw TorqueTable Result: %d", newWatts.getTarget(), tableResult);
  _updateValues(newCadence, newWatts, tableResult);

  int i = 0;
  while (rtConfig.getTargetIncline() != rtConfig.getCurrentIncline()) {  // wait while the knob moves to target position.
    vTaskDelay(100 / portTICK_PERIOD_MS);
    if (i > 50) {  // failsafe for infinite loop
      SS2K_LOG(ERG_MODE_LOG_TAG, "Stepper didn't reach target position");
      break;
    }
    i++;
  }

  vTaskDelay((ERG_MODE_DELAY*3) / portTICK_PERIOD_MS);  // Wait for power meter to register new watts
}

void ErgMode::_inSetpointState(int newCadence, Measurement& newWatts) {
  int watts = newWatts.getValue();

  int wattChange  = newWatts.getTarget() - watts;  // setpoint_form_trainer - current_torque => Amount to increase or decrease incline
  float deviation = ((float)wattChange * 100.0) / ((float)newWatts.getTarget());

  float factor     = abs(deviation) > 10 ? userConfig.getERGSensitivity() : userConfig.getERGSensitivity() / 2;
  float newIncline = rtConfig.getCurrentIncline() + (wattChange * factor);

  _updateValues(newCadence, newWatts, newIncline);
}

void ErgMode::_updateValues(int newCadence, Measurement& newWatts, float newIncline) {
  rtConfig.setTargetIncline(newIncline);
  _writeLog(rtConfig.getCurrentIncline(), newIncline, this->setPoint, newWatts.getTarget(), this->watts.getValue(), newWatts.getValue(), this->cadence, newCadence);

  this->watts    = newWatts;
  this->setPoint = newWatts.getTarget();
  this->cadence  = newCadence;
}

bool ErgMode::_userIsSpinning(int cadence, float incline) {
  if (cadence <= MIN_ERG_CADENCE) {
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
  SS2K_LOGW(ERG_MODE_LOG_CSV_TAG, "current incline;new incline;current setpoint;new setpoint;current watts;new watts;current cadence;new cadence;");
}

void ErgMode::_writeLog(float currentIncline, float newIncline, int currentSetPoint, int newSetPoint, int currentWatts, int newWatts, int currentCadence, int newCadence) {
  SS2K_LOGW(ERG_MODE_LOG_CSV_TAG, "%d;%.2f;%.2f;%d;%d;%d;%d;%d;%d", currentIncline, newIncline, currentSetPoint, newSetPoint, currentWatts, newWatts, currentCadence, newCadence);
}
