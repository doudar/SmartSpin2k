/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ERG_Mode.h"
#include "SS2KLog.h"
#include "Main.h"
#include "BLE_Custom_Characteristic.h"
#include "Power_Table.h"
#include <LittleFS.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>

// Create a torque table representing 0w-1000w in 50w increments.
// i.e. powerTable[1] corresponds to the incline required for 50w. powerTable[2] is the incline required for 100w and so on.

static unsigned long int ergTimer = millis();

void ErgMode::runERG() {
  static ErgMode ergMode;
  static PowerBuffer powerBuffer;
  static bool hasConnectedPowerMeter = false;
  static bool simulationRunning      = false;
  static int loopCounter             = 0;
  static bool pTabUsed4Pwr           = false;

  if ((millis() - ergTimer) > ERG_MODE_DELAY) {
    // reset the timer.
    ergTimer = millis();
    // be quiet while updating via BLE
    if (ss2k->isUpdating) {
      return;
    }
    if (spinBLEServer.spinDownFlag) {
      return;
    }

    static unsigned long int saveFlagCooldown = 0;
    // save powertable if saveFlag has been set for 10 seconds using a saveFlagCooldown timer
    // this is to provide enough time to transmit a new powerTable using BLE.
    if (powerTable->saveFlag) {
      if (saveFlagCooldown == 0) {
        saveFlagCooldown = millis();
      }
      if ((millis() - saveFlagCooldown) > 10000) {
        powerTable->_save();
        saveFlagCooldown = 0;
        powerTable->saveFlag         = false;
      }
    }

    // values hard set for testing:
    // userConfig->setPTab4Pwr(true);
    // rtConfig->setHomed(true);

    if ((!spinBLEClient.connectedPM || userConfig->getPTab4Pwr()) && rtConfig->cad.getValue() && rtConfig->getHomed()) {
      // Lookup watts using the Power Table.
      pTabUsed4Pwr = true;
      if (powerTable->_hasBeenLoadedThisSession) {
        rtConfig->watts.setValue(powerTable->lookupWatts(rtConfig->cad.getValue(), ss2k->getCurrentPosition()));
      } else {
        // only run _manageSaveState every 5 seconds
        static unsigned long int saveStateTimer = millis();
        if ((millis() - saveStateTimer) > 5000) {
          // load the power table, true to skip checks.
          powerTable->_manageSaveState(true);
          saveStateTimer = millis();
        }
      }
    } else {
      pTabUsed4Pwr = false;
    }

    if (rtConfig->cad.getValue() > 0 && rtConfig->watts.getValue() > 0) {
      hasConnectedPowerMeter = spinBLEClient.connectedPM;
      simulationRunning      = rtConfig->watts.getTarget();
      if (!simulationRunning) {
        simulationRunning = rtConfig->watts.getSimulate();
      }

      if (!pTabUsed4Pwr) {
        // add values to Power table
        powerTable->processPowerValue(powerBuffer, rtConfig->cad.getValue(), rtConfig->watts);
      }

      // compute ERG
      if ((rtConfig->getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetPower) && (hasConnectedPowerMeter || simulationRunning)) {
        ergMode.computeErg();
      }

      // resistance mode
      if ((rtConfig->getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetResistanceLevel) && (rtConfig->getMaxResistance() != DEFAULT_RESISTANCE_RANGE)) {
        ergMode.computeResistance();
      }

      // Set Min and Max Stepper positions
      if (loopCounter > 50) {
        loopCounter = 0;
        powerTable->setStepperMinMax();
      }
    }

    if (ss2k->resetPowerTableFlag) {
      LittleFS.remove(POWER_TABLE_FILENAME);
      powerTable->reset();
      userConfig->setHMin(INT32_MIN);
      userConfig->setHMax(INT32_MIN);
      spinBLEServer.spinDownFlag = 0;
      rtConfig->setHomed(false);
      userConfig->saveToLittleFS();
    }
    loopCounter++;
  }
}

// compute position for resistance control mode
void ErgMode::computeResistance() {
  static int stepChangePerResistance = userConfig->getShiftStep();
  static Measurement oldResistance;

  if (rtConfig->resistance.getTimestamp() == oldResistance.getTimestamp()) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "Resistance previously processed.");
    return;
  }

  int actualDelta = rtConfig->resistance.getTarget() - rtConfig->resistance.getValue();
  if (actualDelta != 0) {
    rtConfig->setTargetIncline(rtConfig->getTargetIncline() + (TABLE_DIVISOR * actualDelta));
  } else {
    rtConfig->setTargetIncline(ss2k->getCurrentPosition());
  }
  oldResistance = rtConfig->resistance;
}

// as a note, Trainer Road sends 50w target whenever the app is connected.
void ErgMode::computeErg() {
  Measurement newWatts = rtConfig->watts;
  int newCadence       = rtConfig->cad.getValue();

  // check for new torque value or new set point, if watts < 10 treat as faulty
  if ((this->watts.getTimestamp() == newWatts.getTimestamp() && this->setPoint == newWatts.getTarget()) || newWatts.getValue() < 10) {
    SS2K_LOGW(ERG_MODE_LOG_TAG, "Watts previously processed.");
    return;
  }

  // set minimum set point to minimum bike watts if app sends set point lower than minimum bike watts.
  if (newWatts.getTarget() < userConfig->getMinWatts()) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "ERG Target Below Minumum Value.");
    newWatts.setTarget(userConfig->getMinWatts());
  }

  bool isUserSpinning = this->_userIsSpinning(newCadence, ss2k->getCurrentPosition());
  if (!isUserSpinning) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "ERG Mode but no User Spin");
    return;
  }

#ifdef ERG_MODE_USE_POWER_TABLE
// SetPoint changed
#ifdef ERG_MODE_USE_PID
  if (abs(this->setPoint - newWatts.getTarget()) > 20) {
#endif
    _setPointChangeState(newCadence, newWatts);
    return;
#ifdef ERG_MODE_USE_PID
  }
#endif
#endif

#ifdef ERG_MODE_USE_PID
  // Setpoint unchanged
  _inSetpointState(newCadence, newWatts);
#endif
}

void ErgMode::_setPointChangeState(int newCadence, Measurement& newWatts) {
  int32_t tableResult = powerTable->lookup(newWatts.getTarget(), newCadence);

  // Sanity check for targets
  if (tableResult != RETURN_ERROR) {
    if (rtConfig->watts.getValue() > newWatts.getTarget() && tableResult > ss2k->getCurrentPosition()) {
      SS2K_LOG(ERG_MODE_LOG_TAG, "Table Result Failed High Test: %d", tableResult);
      tableResult = RETURN_ERROR;
    }
    if (rtConfig->watts.getValue() < newWatts.getTarget() && tableResult < ss2k->getCurrentPosition()) {
      SS2K_LOG(ERG_MODE_LOG_TAG, "Table Result Failed Low Test: %d", tableResult);
      tableResult = RETURN_ERROR;
    }
  }

  // Handle return errors
  if (tableResult == RETURN_ERROR) {
    int wattChange  = newWatts.getTarget() - newWatts.getValue();
    float deviation = ((float)wattChange * 100.0) / ((float)newWatts.getTarget());
    float factor    = abs(deviation) > 10 ? userConfig->getERGSensitivity() * 2 : userConfig->getERGSensitivity() / 2;
    tableResult     = ss2k->getCurrentPosition() + (wattChange * factor);
  }

  SS2K_LOG(ERG_MODE_LOG_TAG, "SetPoint changed:%dw PowerTable Result: %d", newWatts.getTarget(), tableResult);
  _updateValues(newCadence, newWatts, tableResult);

  if (rtConfig->getTargetIncline() != ss2k->getCurrentPosition()) {  // add some time to wait while the knob moves to target position.
    int timeToAdd = abs(ss2k->getCurrentPosition() - rtConfig->getTargetIncline());
    if (timeToAdd > 5000) {  // 5 seconds
      SS2K_LOG(ERG_MODE_LOG_TAG, "Capping ERG seek time to 5 seconds");
      timeToAdd = 5000;
    }
    ergTimer += timeToAdd;
  }
  ergTimer += (ERG_MODE_DELAY * 2);  // Wait for power meter to register new watts
}

// INTRODUCING PID CONTROL LOOP
// Error: Difference between TW and Current W

// Proportional term: Directly Proportional to error
// Integral term: accumulated sum of errors over time
// Derivative term: rate of change of error

// PrevError
void ErgMode::_inSetpointState(int newCadence, Measurement& newWatts) {
  // Setting Gains For PID Loop
  float Kp = userConfig->getERGSensitivity() * 2;
  float Ki = 0.1;
  float Kd = 0.1;

  static float integral  = 0.0;
  static float prevError = 0.0;

  // retrieves the current Watt output
  int watts = newWatts.getValue();
  // retrieves target Watt output
  int target = newWatts.getTarget();
  // subtracting target from current watts
  float error = target - watts;

  // Defining proportional term
  float proportional = Kp * error;

  // Defining integral term
  integral += error;
  float integralFinal = Ki * integral;

  // Clamping down integral term
  float integralMax = 60;
  float integralMin = -60;

  if (integral > integralMax) {
    integral = integralMax;
  } else if (integral < integralMin) {
    integral = integralMin;
  }

  // Defining derivative term
  float derivative     = error - prevError;  // Difference between current and previous errors
  float derivativeTerm = Kd * derivative;

  // final PID output
  float PID_output = proportional + integralFinal + derivativeTerm;

  // log proportional, integral, derivative every five seconds
  static unsigned long lastTime = 0;
  if (millis() - lastTime > 5000) {
    lastTime = millis();
    SS2K_LOG(ERG_MODE_LOG_TAG, "Proportional: %f, Integral: %f, Derivative: %f", proportional, integralFinal, derivativeTerm);
  }

  // Calculate new incline
  float newIncline = ss2k->getCurrentPosition() + PID_output;

  prevError = error;

  // Apply the new values
  _updateValues(newCadence, newWatts, newIncline);
}

// OLD PI FUNCTION
//  void ErgMode::_inSetpointState(int newCadence, Measurement& newWatts) {
//    // retrieves the current Watt output
//    int watts = newWatts.getValue();

//   // setpoint_form_trainer - current_torque => Amount to increase or decrease incline
//   int wattChange = newWatts.getTarget() - watts;

//   // Calculates how far current power is from the target power, measred as a percentage of target
//   float deviation = ((float)wattChange * 100.0) / ((float)newWatts.getTarget());

//   // retrieves the sensitivity from adjustments from userConfig
//   float factor = abs(deviation) > 10 ? userConfig->getERGSensitivity() : userConfig->getERGSensitivity() / 2;

//   // Adjusts the incline
//   float newIncline = ss2k->getCurrentPosition() + (wattChange * factor);

//   // passes to apply new cadence, power measurement and incline settings
//   _updateValues(newCadence, newWatts, newIncline);
// }

void ErgMode::_updateValues(int newCadence, Measurement& newWatts, float newIncline) {
  rtConfig->setTargetIncline(newIncline);
  _writeLog(ss2k->getCurrentPosition(), newIncline, this->setPoint, newWatts.getTarget(), this->watts.getValue(), newWatts.getValue(), this->cadence, newCadence);

  this->watts    = newWatts;
  this->setPoint = newWatts.getTarget();
  this->cadence  = newCadence;
}

bool ErgMode::_userIsSpinning(int cadence, float incline) {
  if (cadence <= MIN_ERG_CADENCE) {
    if (!this->engineStopped) {       // Test so motor stop command only happens once.                                    // release tension
      rtConfig->setTargetIncline(0);  // release incline
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
  SS2K_LOGW(ERG_MODE_LOG_CSV_TAG, "%d;%.2f;%.2f;%d;%d;%d;%d;%d", currentIncline, newIncline, currentSetPoint, newSetPoint, currentWatts, newWatts, currentCadence, newCadence);
}
