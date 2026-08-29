/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ERG_Mode.h"
#include "ERG_Mode_Utils.h"
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

namespace {
double scheduledErgGain(double sensitivity, int operatingWatts, int cadence, bool& usedPowerTable, PowerTableSlopeStatus::Value& slopeStatus) {
  usedPowerTable = false;

  sensitivity     = ErgControl::sanitizeSensitivity(sensitivity);
  const double fallback = ErgControl::fallbackGain(sensitivity, operatingWatts);
  double localStepsPerWatt;
  if (powerTable->lookupErgSlope(operatingWatts, cadence, localStepsPerWatt, &slopeStatus)) {
    const double gain = localStepsPerWatt * sensitivity / ErgControl::SLOPE_CONTROL_DIVISOR;
    usedPowerTable = true;
    return ErgControl::blendedTableGain(gain, fallback);
  }
  return fallback;
}

}  // namespace

void ErgMode::runERG() {
  static PowerBuffer powerBuffer;
  static bool hasConnectedPowerMeter = false;
  static bool simulationRunning      = false;
  static int loopCounter             = 0;
  static int lastSetPoint            = 0;

  if (rtConfig->getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetPower && rtConfig->cad.getValue() <= MIN_ERG_CADENCE) {
    if (rtConfig->watts.getTarget() != userConfig->getMinWatts()) {
      SS2K_LOG(ERG_MODE_LOG_TAG, "Cadence below ERG minimum; lowering target to %dw", userConfig->getMinWatts());
      lastSetPoint = rtConfig->watts.getTarget();
      rtConfig->watts.setTarget(userConfig->getMinWatts());
      mode      = Mode::MAINTAIN;
      isDelayed = false;
      ergTimer  = 0;
    }
  } else if (lastSetPoint != 0 && rtConfig->getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetPower  && rtConfig->cad.getValue() > MIN_ERG_CADENCE) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "Cadence above ERG minimum; restoring target to %dw", lastSetPoint);
    rtConfig->watts.setTarget(lastSetPoint);
    lastSetPoint = 0;
  }

  const bool reachedIncreasingTarget = mode == Mode::INCREASING && rtConfig->watts.getValue() >= rtConfig->watts.getTarget();
  const bool reachedDecreasingTarget = mode == Mode::DECREASING && rtConfig->watts.getValue() <= rtConfig->watts.getTarget();
  if (reachedIncreasingTarget || reachedDecreasingTarget) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "ERG setpoint reached; resuming PID control");
    mode      = Mode::MAINTAIN;
    isDelayed = false;
    ergTimer  = 0;
  }

  if (isDelayed && (ss2k->getCurrentPosition() == ss2k->getTargetPosition())) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "ERG delay cleared,  %dw, tgt %dw, pos %d, tgt %d", rtConfig->watts.getValue(), rtConfig->watts.getTarget(), ss2k->getCurrentPosition(),
             ss2k->getTargetPosition());
    ergTimer  = millis() + ERG_MODE_DELAY;
    isDelayed = false;
  }

  if ((millis() > ergTimer)) {
    if (isDelayed) {
      SS2K_LOG(ERG_MODE_LOG_TAG, "ERG wait expired, %dw, tgt %dw, pos %d, tgt %d", rtConfig->watts.getValue(), rtConfig->watts.getTarget(), ss2k->getCurrentPosition(),
               ss2k->getTargetPosition());
      isDelayed = false;
    }

    if (mode != Mode::MAINTAIN) {
      SS2K_LOG(ERG_MODE_LOG_TAG, "ERG setpoint seek complete; resuming PID control");
      mode = Mode::MAINTAIN;
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
    if (!powerTable->_hasBeenLoadedThisSession) {
      powerTable->_manageSaveState();
    }

    if (rtConfig->cad.getValue() > MIN_ERG_CADENCE / 2) {
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
        this->computeErg();
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
        const int tablePower = powerTable->lookupWatts(rtConfig->cad.getValue(), ss2k->getCurrentPosition());
        // A zero lookup only occurs with zero cadence or an invalid table, so
        // do not carry a previous power value after the rider has stopped.
        if (tablePower > 0) {
          _smoothPWR = (previousPower + tablePower) / 2;
        } else {
          _smoothPWR   = 0;
          previousPower = 0;
        }
      } else {
        // only run _manageSaveState every 5 seconds
        static unsigned long int saveStateTimer = millis();
        if ((millis() - saveStateTimer) > 5000) {
          // load the power table, true to skip checks.
          powerTable->_manageSaveState(true);
          saveStateTimer = millis();
        }
      }
      rtConfig->watts.setValue(_smoothPWR);
      previousPower = (rtConfig->watts.getValue() + previousPower) / 2;
    }
  }
}

// as a note, Trainer Road sends 50w target whenever the app is connected.
void ErgMode::computeErg() {
  int32_t result = RETURN_ERROR;

  // Without known travel limits, keep ERG above the configured minimum bike watts.
  // Once homed, moveStepper() clamps the commanded position to the known min/max step range instead.
  if (!rtConfig->getHomed() && rtConfig->watts.getTarget() < userConfig->getMinWatts()) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "ERG target below minimum value while unhomed.");
    rtConfig->watts.setTarget(userConfig->getMinWatts());
  }

  // check for new watt value or new set point, if watts < 0 treat as faulty
  if ((this->prevWatts.getTimestamp() == rtConfig->watts.getTimestamp() && this->prevWatts.getTarget() == rtConfig->watts.getTarget()) || rtConfig->watts.getValue() < 0) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "Watts previously processed.");
    return;
  }

#ifdef ERG_MODE_USE_POWER_TABLE
  if (abs(this->prevWatts.getTarget() - rtConfig->watts.getTarget()) > (POWERTABLE_WATT_INCREMENT + ERG_MODE_PID_WINDOW) && rtConfig->getHomed()) {
    result = _setPointChangeState();
  }
#endif
#ifdef ERG_MODE_USE_PID
  // Setpoint unchanged
  if (result == INT32_MIN) {
    result = _inSetpointState();
  }
#endif

  // Avoid ERG Black hole
  if (rtConfig->cad.getValue() < MIN_ERG_CADENCE && rtConfig->getHomed()) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "Cadence below ERG minimum");
    result = userConfig->getShiftStep() * SHIFTER_MIDDLE_POSITION;
  }
  _updateValues(result);
}

int32_t ErgMode::_setPointChangeState() {
  mode = (rtConfig->watts.getTarget() > rtConfig->watts.getValue()) ? Mode::INCREASING : Mode::DECREASING;
  // It's better to undershoot increasing watts and overshoot decreasing watts, so lets set the lookup target to the nearest side of POWERTABLE_WATT_INCREMENT
  int adjustedWattTarget = (mode == Mode::INCREASING) ? rtConfig->watts.getTarget() - ERG_MODE_PID_WINDOW : rtConfig->watts.getTarget() + ERG_MODE_PID_WINDOW;
  int32_t tableResult = powerTable->lookup(adjustedWattTarget,
                                           (mode == Mode::INCREASING) ? rtConfig->cad.getValue() + POWERTABLE_CAD_INCREMENT : rtConfig->cad.getValue() - POWERTABLE_CAD_INCREMENT);

  // Sanity check - with homing enabled, we should never have a negative result. If we do, something went wrong.
  if (rtConfig->getHomed() && tableResult < 0) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "PowerTable returned negative result with homing enabled. Using PID");
    tableResult = RETURN_ERROR;
  }

  // Test current watts against the table result. If We're already lower or higher than target, flag the result as a return error.
  if (tableResult != RETURN_ERROR) {
    if (mode == Mode::INCREASING && tableResult <= ss2k->getCurrentPosition()) {
      SS2K_LOG(ERG_MODE_LOG_TAG, "Table Result Failed increasing Test: %d", tableResult);
      tableResult = RETURN_ERROR;
    }
    if (mode == Mode::DECREASING && tableResult >= ss2k->getCurrentPosition()) {
      SS2K_LOG(ERG_MODE_LOG_TAG, "Table Result Failed decreasing Test: %d", tableResult);
      tableResult = RETURN_ERROR;
    }
  }

  // Handle return errors
  if (tableResult == RETURN_ERROR) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "Lookup Error. Using PID");
    tableResult = _inSetpointState();
  } else {
    if (tableResult != ss2k->getCurrentPosition()) {  // add some time to wait while the knob moves to target position.
      isDelayed             = true;
      long int stepDistance = abs(ss2k->getCurrentPosition() - tableResult);
      // Calculate time to add based on step distance and stepper speed
      long int timeToAdd = round((((double)stepDistance * 1000.0) / (double)userConfig->getStepperSpeed()) * 2);
      if (timeToAdd > 10000) {  // 10 seconds
        SS2K_LOG(ERG_MODE_LOG_TAG, "Capping ERG seek time to 10 seconds");
        timeToAdd = 10000;
      }
      SS2K_LOG(ERG_MODE_LOG_TAG, "Adjusted setpoint returned: %dw %drpm Waiting:%dms PowerTable Result: %d", adjustedWattTarget, rtConfig->cad.getValue(), timeToAdd, tableResult);
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
int32_t ErgMode::_inSetpointState() {
  // retrieves the current Watt output
  int watts = rtConfig->watts.getValue();
  // retrieves target Watt output
  int target = rtConfig->watts.getTarget();
  // subtracting target from current watts
  int error = target - watts;

  // Scale the proportional gain to the local power-table slope. This compensates for the eddy-current brake producing fewer watts per step at low resistance
  // and more watts per step at high resistance. ERG sensitivity controls how much of the predicted correction is applied and bounds bad model slopes.
  const double configuredSensitivity = userConfig->getERGSensitivity();
  bool usedPowerTable                = false;
  PowerTableSlopeStatus::Value slopeStatus = PowerTableSlopeStatus::InvalidRequest;
  double Kp                          = scheduledErgGain(configuredSensitivity, target, rtConfig->cad.getValue(), usedPowerTable, slopeStatus);
  const double controlSensitivity    = ErgControl::sanitizeSensitivity(configuredSensitivity);

  Kp = ErgControl::errorScheduledGain(Kp, error, mode == Mode::MAINTAIN);

  if (watts < userConfig->getMinWatts()) {
    Kp = Kp * controlSensitivity;  // Increase gain at very low watts to prevent Zwift from timing out on an initial interval.
  }
  Kp = ErgControl::clampGain(Kp, controlSensitivity);

  mode = Mode::MAINTAIN;

  // Defining proportional term
  double proportional = Kp * error;

  // final PID output
  double PID_output = proportional;

  // Cap the change to no more than we can move until the next reading
  int maxChange = round((long)((userConfig->getStepperSpeed() * ERG_MODE_DELAY)) / 1000.0f);  // max change based on stepper speed and delay
  if (PID_output > maxChange) {
    PID_output = maxChange;
  } else if (PID_output < -maxChange) {
    PID_output = -maxChange;
  }

  // Calculate new incline
  float newIncline = ss2k->getCurrentPosition() + PID_output;

  // Log output at the configured ERG interval.
  static unsigned long lastTime = 0;
  if (millis() - lastTime > ERG_MODE_LOG_INTERVAL_MS) {
    lastTime = millis();
    SS2K_LOG(ERG_MODE_LOG_TAG, "%dw, Target %dw, Kp: %.3f (%s%s%s), PID Output: %f, Moving to: %f", rtConfig->watts.getValue(), rtConfig->watts.getTarget(), Kp,
             usedPowerTable ? "table" : "fallback", usedPowerTable ? "" : ": ", usedPowerTable ? "" : PowerTableSlopeStatus::name(slopeStatus), PID_output, newIncline);
  }

  return newIncline;
}

void ErgMode::_updateValues(float newIncline) {
  rtConfig->setTargetIncline(newIncline);
  _writeLog(ss2k->getCurrentPosition(), newIncline, this->prevWatts.getTarget(), rtConfig->watts.getTarget(), this->prevWatts.getValue(), rtConfig->watts.getValue(),
            this->prevCadence.getValue(), rtConfig->cad.getValue());

  this->prevWatts   = rtConfig->watts;
  this->prevCadence = rtConfig->cad;
}

void ErgMode::_writeLog(float currentIncline, float newIncline, int currentSetPoint, int newSetPoint, int currentWatts, int newWatts, int currentCadence, int newCadence) {
  SS2K_LOGW(ERG_MODE_LOG_CSV_TAG, "%d;%.2f;%.2f;%d;%d;%d;%d;%d", currentIncline, newIncline, currentSetPoint, newSetPoint, currentWatts, newWatts, currentCadence, newCadence);
}
