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

static unsigned long ergTimer = millis() + ERG_MODE_DELAY;
static bool isDelayed         = false;

void ErgMode::runERG() {
  static ErgMode ergMode;
  static PowerBuffer powerBuffer;
  static bool hasConnectedPowerMeter = false;
  static bool simulationRunning      = false;
  static int loopCounter             = 0;

  if ((millis() > ergTimer)) {
    if (isDelayed) {
      SS2K_LOG(ERG_MODE_LOG_TAG, "ERG wait expired, pos: %d, tgt: %d", ss2k->getCurrentPosition(), rtConfig->getTargetIncline());
      isDelayed = false;
    }

    // reset the timer.
    ergTimer = millis() + ERG_MODE_DELAY;

    static unsigned long int saveFlagCooldown = 0;
    // save powertable if saveFlag has been set for 10 seconds using a saveFlagCooldown timer
    // this is to provide enough time to transmit a new powerTable using BLE.
    if (powerTable->saveFlag) {
      if (saveFlagCooldown == 0) {
        saveFlagCooldown = millis();
      }
      if ((millis() - saveFlagCooldown) > 10000) {
        powerTable->_save();
        saveFlagCooldown     = 0;
        powerTable->saveFlag = false;
      }
    }
    // Load power table if not yet loaded this session
    if(!powerTable->_hasBeenLoadedThisSession) {
      powerTable->_manageSaveState();
    }

    if (rtConfig->cad.getValue()) {
      hasConnectedPowerMeter = spinBLEClient.connectedPM;
      simulationRunning      = rtConfig->watts.getTarget();
      if (!simulationRunning) {
        simulationRunning = rtConfig->watts.getSimulate();
      }

      if (!userConfig->getPTab4Pwr()) {
        // add values to Power table
        powerTable->processPowerValue(powerBuffer, rtConfig->cad.getValue(), rtConfig->watts);
      }

      // compute ERG
      if ((rtConfig->getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetPower) && (hasConnectedPowerMeter || simulationRunning)) {
        ergMode.computeErg();
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

  if (userConfig->getPTab4Pwr()) {
    // only do this twice as often as ERG_MODE_DELAY
    static float previousPower             = 0;
    static unsigned long int pTab4pwrTimer = millis();
    int _smoothPWR                         = 0;
    if (millis() - pTab4pwrTimer > ERG_MODE_DELAY / 2) {
      // reset the timer.
      pTab4pwrTimer = millis();
      // Lookup watts using the Power Table.
      if (powerTable->_hasBeenLoadedThisSession) {
        // Instead of directly outputting this, we should smooth the output by averaging it with the last value.
        _smoothPWR = ((previousPower + powerTable->lookupWatts(rtConfig->cad.getValue(), ss2k->getCurrentPosition())) / 2);
      } else {
        // only run _manageSaveState every 5 seconds
        static unsigned long int saveStateTimer = millis();
        if ((millis() - saveStateTimer) > 5000) {
          // load the power table, true to skip checks.
          powerTable->_manageSaveState(true);
          saveStateTimer = millis();
        }
      }
      // So the user knows pTab4PWR is enabled, provide some cadence feedback even if the value returned by the table is 0.
      int minimumPower = rtConfig->cad.getValue() / 2;  // 50% of the cadence value
      _smoothPWR       = _smoothPWR < minimumPower ? round((minimumPower + previousPower) / 2.0f) : _smoothPWR;
      rtConfig->watts.setValue(_smoothPWR);
      previousPower = (rtConfig->watts.getValue() + previousPower) / 2;
    }
  }
}

// as a note, Trainer Road sends 50w target whenever the app is connected.
void ErgMode::computeErg() {
  Measurement newWatts = rtConfig->watts;
  int newCadence       = rtConfig->cad.getValue();
  int32_t result       = RETURN_ERROR;

  bool isUserSpinning = this->_userIsSpinning(newCadence, ss2k->getCurrentPosition());
  if (!isUserSpinning) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "ERG Mode but no User Spin");
    return;
  }

  // set minimum set point to minimum bike watts if app sends set point lower than minimum bike watts.
  if (newWatts.getTarget() < userConfig->getMinWatts()) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "ERG Target Below Minumum Value.");
    newWatts.setTarget(userConfig->getMinWatts());
  }

  // check for new torque value or new set point, if watts < 0 treat as faulty
  if ((this->watts.getTimestamp() == newWatts.getTimestamp() && this->setPoint == newWatts.getTarget()) || newWatts.getValue() < 0) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "Watts previously processed.");
    return;
  }

#ifdef ERG_MODE_USE_POWER_TABLE
  if (abs(this->setPoint - newWatts.getTarget()) > ERG_MODE_PID_WINDOW && rtConfig->getHomed()) {
    result = _setPointChangeState(newCadence, newWatts);
  }
#endif
#ifdef ERG_MODE_USE_PID
  // Setpoint unchanged
  if (result == INT32_MIN) {
    result = _inSetpointState(newCadence, newWatts);
  }
#endif
  _updateValues(newCadence, newWatts, result);
}

int32_t ErgMode::_setPointChangeState(int newCadence, Measurement& newWatts) {
  // It's better to undershoot increasing watts and overshoot decreasing watts, so lets set the lookup target to the nearest side of POWERTABLE_WATT_INCREMENT
  int adjustedWattTarget  = newWatts.getTarget() >= newWatts.getValue() ? newWatts.getTarget() - POWERTABLE_WATT_INCREMENT : newWatts.getTarget() + POWERTABLE_WATT_INCREMENT;
  int32_t tableResult = powerTable->lookup(adjustedWattTarget, newCadence);

  // Sanity check - with homing enabled, we should never have a negative result. If we do, something went wrong.
  if (rtConfig->getHomed() && tableResult < 0) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "PowerTable returned negative result with homing enabled. Using PID");
    tableResult = RETURN_ERROR;
  }

  // Test current watts against the table result. If We're already lower or higher than target, flag the result as a return error.
  if (tableResult != RETURN_ERROR) {
    if (adjustedWattTarget > newWatts.getValue() && tableResult < ss2k->getCurrentPosition()) {
      SS2K_LOG(ERG_MODE_LOG_TAG, "Table Result Failed increasing Test: %d", tableResult);
      tableResult = RETURN_ERROR;
    }
    if ((adjustedWattTarget < newWatts.getValue()) && tableResult > ss2k->getCurrentPosition()) {
      SS2K_LOG(ERG_MODE_LOG_TAG, "Table Result Failed decreasing Test: %d", tableResult);
      tableResult = RETURN_ERROR;
    }
  }

  // Handle return errors
  if (tableResult == RETURN_ERROR) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "Lookup Error. Using PID");
    tableResult = _inSetpointState(newCadence, newWatts);
  } else {
    if (tableResult != ss2k->getCurrentPosition()) {  // add some time to wait while the knob moves to target position.
      isDelayed             = true;
      long int stepDistance = abs(ss2k->getCurrentPosition() - tableResult);
      // Calculate time to add based on step distance and stepper speed
      long int timeToAdd = round(((double)stepDistance * 1000.0) / (double)userConfig->getStepperSpeed() + ERG_MODE_DELAY);
      if (timeToAdd > 10000) {  // 10 seconds
        SS2K_LOG(ERG_MODE_LOG_TAG, "Capping ERG seek time to 10 seconds");
        timeToAdd = 10000;
      }
      SS2K_LOG(ERG_MODE_LOG_TAG, "SetPoint changed:%dw Waiting:%dms PowerTable Result: %d", adjustedWattTarget, timeToAdd, tableResult);
      ergTimer += timeToAdd;
    }
    ergTimer += (ERG_MODE_DELAY);  // Wait for power meter to register new watts
  }
  return tableResult;
}

// INTRODUCING PID CONTROL LOOP
// Error: Difference between TW and Current W

// Proportional term: Directly Proportional to error
// Integral term: accumulated sum of errors over time
// Derivative term: rate of change of error

// PrevError
int32_t ErgMode::_inSetpointState(int newCadence, Measurement& newWatts) {
  // Setting Gains For PID Loop
  double Kp = userConfig->getERGSensitivity();  // Proportional gain based on user sensitivity

  // retrieves the current Watt output
  int watts = newWatts.getValue();
  // retrieves target Watt output
  int target = newWatts.getTarget();
  // subtracting target from current watts
  int error = target - watts;

  // modifying gains based on error
  if (abs(error) < 10) {
    Kp = Kp * 0.25;  // decrease further for tiny errors
  } else if (abs(error) < 50) {
    Kp = Kp * 0.75;  // Moderate for medium errors
  } else if (abs(error) > 100) {
    Kp = Kp * 1.25;  // Aggressive for large errors
  }

  // Defining proportional term
  double proportional = Kp * error;
  if (newWatts.getValue() < userConfig->getMinWatts()) {
    proportional = proportional * userConfig->getERGSensitivity();  // increase proportional term when at very low watts. Prevents Zwift from timeout on initial interval.
  }

  // final PID output
  double PID_output = proportional;

  // log proportional every five seconds
  static unsigned long lastTime = 0;
  if (millis() - lastTime > 5000) {
    lastTime = millis();
    SS2K_LOG(ERG_MODE_LOG_TAG, "Proportional: %f", proportional);
  }

  // Cap the change to no more than we can move until the next reading
  int maxChange = round((long)((userConfig->getStepperSpeed() * ERG_MODE_DELAY)) / 1000.0f);  // max change based on stepper speed and delay
  if (PID_output > maxChange) {
    PID_output = maxChange;
  } else if (PID_output < -maxChange) {
    PID_output = -maxChange;
  }

  // Calculate new incline
  float newIncline = ss2k->getCurrentPosition() + PID_output;
  return newIncline;
}

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

void ErgMode::_writeLog(float currentIncline, float newIncline, int currentSetPoint, int newSetPoint, int currentWatts, int newWatts, int currentCadence, int newCadence) {
  SS2K_LOGW(ERG_MODE_LOG_CSV_TAG, "%d;%.2f;%.2f;%d;%d;%d;%d;%d", currentIncline, newIncline, currentSetPoint, newSetPoint, currentWatts, newWatts, currentCadence, newCadence);
}
