/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ERG_Mode.h"
#include "SS2KLog.h"
#include "Main.h"
#include "Power_Table.h"
#include <LittleFS.h>
#include <algorithm>
#include <cmath>

static unsigned long ergTimer = millis() + ERG_MODE_DELAY;

namespace {
double scheduledErgGain(double sensitivity, int operatingWatts, int cadence, bool& usedPowerTable, PowerTableSlopeStatus::Value& slopeStatus) {
  usedPowerTable = false;

  sensitivity           = ErgControl::sanitizeSensitivity(sensitivity);
  const double fallback = ErgControl::fallbackGain(sensitivity, operatingWatts);
  double localStepsPerWatt;
  if (powerTable->lookupErgSlope(operatingWatts, cadence, localStepsPerWatt, &slopeStatus)) {
    const double gain = localStepsPerWatt * sensitivity / ErgControl::SLOPE_CONTROL_DIVISOR;
    usedPowerTable    = true;
    return ErgControl::blendedTableGain(gain, fallback);
  }
  return fallback;
}

}  // namespace

void ErgMode::prepareMode() {
  const bool active = rtConfig->getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetPower;
  if (active == wasErgMode) return;
  wasErgMode                 = active;
  tableSeekState             = TableSeekState::INACTIVE;
  tableSeekFirstArrivalValid = false;
  pausedTargetWatts          = 0;
  feedbackWaiting = tableSeekPidSeedValid = false;
  tableCorrectionNeedsProgress            = false;
  mode                                    = Mode::MAINTAIN;
  cadenceReference                        = 0;
  responseTimestamp                       = 0;
  responsePreviousFresh                   = false;
  responseTrend                           = 0;
  responsePreviousTrend                   = 0;
  transitionTarget                        = INT32_MIN;
  feedbackEarlyRetreatTarget              = INT32_MIN;
  prevWatts.setTarget(0);
  ergTimer = millis();
  if (active) {
    // SIM uses hundredths of a grade in this shared field; ERG uses steps.
    rtConfig->setTargetIncline(ss2k->getCurrentPosition());
    SS2K_LOG(ERG_MODE_LOG_TAG, "Entering ERG at current position %d", ss2k->getCurrentPosition());
  }
}

void ErgMode::_trackControlMove(int32_t position) {
  controlMinimum = std::min(controlMinimum, std::min(position, ss2k->getCurrentPosition()));
  controlMaximum = std::max(controlMaximum, std::max(position, ss2k->getCurrentPosition()));
}

void ErgMode::runERG() {
  static PowerBuffer powerBuffer;
  static int loopCounter = 0;

  _observePowerResponse();

  if (rtConfig->getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetPower && rtConfig->cad.getValue() <= MIN_ERG_CADENCE) {
    if (rtConfig->watts.getTarget() != userConfig->getMinWatts()) {
      SS2K_LOG(ERG_MODE_LOG_TAG, "Cadence below ERG minimum; lowering target to %dw", userConfig->getMinWatts());
      pausedTargetWatts = rtConfig->watts.getTarget();
      rtConfig->watts.setTarget(userConfig->getMinWatts());
      mode     = Mode::MAINTAIN;
      ergTimer = millis();
    }
  } else if (pausedTargetWatts != 0 && rtConfig->getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetPower && rtConfig->cad.getValue() > MIN_ERG_CADENCE) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "Cadence above ERG minimum; restoring target to %dw", pausedTargetWatts);
    rtConfig->watts.setTarget(pausedTargetWatts);
    pausedTargetWatts = 0;
  }

  if (tableSeekPidSeedValid && (rtConfig->getFTMSMode() != FitnessMachineControlPointProcedure::SetTargetPower || rtConfig->watts.getTarget() != tableSeekTargetWatts)) {
    tableSeekPidSeedValid = false;
  }

  _updateTableConfidence();

  // Seek safety gets the first look at fresh power, followed by cadence
  // compensation and acquisition before ordinary feedback may run.
  _handleTrustedTableSeek();
  _handleFeedbackWait();

  if (static_cast<int32_t>(static_cast<uint32_t>(millis()) - static_cast<uint32_t>(ergTimer)) >= 0) {
    if (mode != Mode::MAINTAIN && !isTableSeeking() && !feedbackWaiting) {
      SS2K_LOG(ERG_MODE_LOG_TAG, "ERG setpoint seek complete; resuming PID control");
      mode = Mode::MAINTAIN;
    }

    // reset the timer.
    ergTimer = millis() + ERG_MODE_DELAY;

    static unsigned long int saveFlagCooldown = 0;
    // Homing is already queued by the first uploaded row. Allow ten seconds
    // for the remaining rows before saving; runERG is paused during homing.
    if (powerTable->saveFlag) {
      if (saveFlagCooldown == 0) saveFlagCooldown = millis();
      if ((millis() - saveFlagCooldown) > 10000) {
        saveFlagCooldown = 0;
        if (powerTable->_save()) {
          powerTable->saveFlag = false;
        }
      }
    } else {
      saveFlagCooldown = 0;
    }
    // Load power table if not yet loaded this session
    if (!powerTable->_hasBeenLoadedThisSession) {
      powerTable->_manageSaveState();
    }

    // Always visit collection, including stops and pauses, so no partial window
    // survives substantial movement/acquisition or a switch to table-derived watts.
    powerTable->processPowerValue(powerBuffer, rtConfig->cad.getValue(), rtConfig->watts, collectionAllowed() && !ss2k->usePowerTableForPower());

    if (rtConfig->cad.getValue() > MIN_ERG_CADENCE / 2) {
      // Set Min and Max Stepper positions
      if (loopCounter > 50) {
        loopCounter = 0;
        powerTable->setStepperMinMax();
      }
    }

    if (ss2k->resetPowerTableFlag) {
      LittleFS.remove(POWER_TABLE_FILENAME);
      powerTable->reset();
      resetTableConfidence();
      userConfig->setHMin(INT32_MIN);
      userConfig->setHMax(INT32_MIN);
      spinBLEServer.spinDownFlag = 0;
      rtConfig->setHomed(false);
      userConfig->saveToLittleFS();
    }
    loopCounter++;
  }

  // Handle new power reports and changed targets as soon as they are visible.
  // computeErg() deduplicates sensor samples and target values, so this event
  // gate preserves one correction per observation without waiting for the
  // slower table-collection cadence above.
  const auto powerSample    = rtConfig->watts.getValueSample();
  const bool newPowerSample = prevWatts.getValueSample().timestamp != powerSample.timestamp;
  const bool targetChanged  = prevWatts.getTarget() != rtConfig->watts.getTarget();
  if (rtConfig->cad.getValue() > MIN_ERG_CADENCE / 2 && (newPowerSample || targetChanged) && rtConfig->getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetPower &&
      (spinBLEClient.connectedPM || rtConfig->watts.getTarget() || rtConfig->watts.getSimulate()) && !isTableSeeking() && !feedbackWaiting) {
    computeErg();
  }

  if (ss2k->usePowerTableForPower()) {
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
          _smoothPWR    = 0;
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

  // Target writes also update Measurement's general timestamp. Only a new
  // value sample or an actually different target permits another correction.
  const auto powerSample = rtConfig->watts.getValueSample();
  if ((this->prevWatts.getValueSample().timestamp == powerSample.timestamp && this->prevWatts.getTarget() == rtConfig->watts.getTarget()) || powerSample.value < 0 ||
      static_cast<uint32_t>(millis()) - powerSample.timestamp > ERG_FEEDBACK_MAX_AGE_MS) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "Watts previously processed.");
    return;
  }

#ifdef ERG_MODE_USE_POWER_TABLE
  const int target         = rtConfig->watts.getTarget();
  const int cadence        = rtConfig->cad.getValue();
  const bool targetChanged = this->prevWatts.getTarget() != target;
  const int targetDelta    = target - this->prevWatts.getTarget();
  // Small ramp updates amend feed-forward without repeatedly correcting the
  // same held power report or restarting table acquisition.
  if (targetChanged && this->prevWatts.getTarget() > 0 && abs(targetDelta) < ERG_MODE_PID_WINDOW / 2 && cadence > MIN_ERG_CADENCE) {
    int32_t adjustment = 0;
    if (rtConfig->getHomed() && _tableTargetIsTrusted(target, cadence)) {
      const int32_t before = _constrainedTablePosition(this->prevWatts.getTarget(), cadence);
      const int32_t after  = _constrainedTablePosition(target, cadence);
      if (before != RETURN_ERROR && after != RETURN_ERROR && (static_cast<int64_t>(after) - before) * targetDelta > 0) adjustment = after - before;
    }
    const int32_t base     = this->prevWatts.getValueSample().timestamp == powerSample.timestamp ? static_cast<int32_t>(rtConfig->getTargetIncline()) : _inSetpointState();
    const int64_t position = static_cast<int64_t>(base) + adjustment;
    _updateValues(static_cast<int32_t>(std::max(static_cast<int64_t>(rtConfig->getMinStep()), std::min(static_cast<int64_t>(rtConfig->getMaxStep()), position))));
    return;
  }
  if (targetChanged && abs(this->prevWatts.getTarget() - target) > ERG_MODE_PID_WINDOW) transitionTarget = target;
  if (targetChanged) tableCorrectionNeedsProgress = false;
  if (rtConfig->getHomed() && targetChanged && (abs(this->prevWatts.getTarget() - target) > ERG_MODE_PID_WINDOW || _tableTargetIsTrusted(target, cadence))) {
    result = _setPointChangeState();
  } else if (!tableCorrectionNeedsProgress && rtConfig->getHomed() && cadenceReference > MIN_ERG_CADENCE && abs(cadence - cadenceReference) >= ERG_TABLE_CADENCE_SEEK_RPM &&
             _tableTargetIsTrusted(target, cadence)) {
    // Preserve the correction already learned by feedback. A cadence change
    // needs the difference between two table positions, not a new absolute
    // position that discards the bike's current offset from the surface.
    const int32_t before = powerTable->lookup(target, cadenceReference);
    const int32_t after  = powerTable->lookup(target, cadence);
    if (before != RETURN_ERROR && after != RETURN_ERROR) {
      const int64_t position = static_cast<int64_t>(ss2k->getTargetPosition()) + after - before;
      const int error        = target - powerSample.value;
      if (position > rtConfig->getMinStep() && position < rtConfig->getMaxStep() &&
          (std::abs(error) <= ERG_MODE_PID_WINDOW || (position - ss2k->getCurrentPosition()) * error >= 0)) {
        mode   = position > ss2k->getCurrentPosition() ? Mode::INCREASING : Mode::DECREASING;
        result = static_cast<int32_t>(position);
        _startTrustedTableSeek(result);
      }
    }
  }
  if (targetChanged || result != RETURN_ERROR || cadenceReference == 0) cadenceReference = cadence;
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

  const int currentCadence = rtConfig->cad.getValue();
  const int currentTarget  = rtConfig->watts.getTarget();

  // Once this part of the surface has repeatedly predicted the real bike,
  // use it as a true feed-forward command. PID remains responsible for
  // overshoot recovery and steady-state maintenance.
  if (_tableTargetIsTrusted(currentTarget, currentCadence)) {
    // A constrained sparse curve still has uncertainty outside its measured
    // coverage. Approach the near side of the power window there and let
    // measured feedback finish the transition. Retain ordinary table seeks
    // when the cadence-curve estimate is unavailable.
    const bool extrapolatedCurve = !_tableTargetIsWithinMeasuredBounds(currentTarget, currentCadence) &&
                                   ErgControl::constrainedTablePosition(powerTable->ptData, currentTarget, currentCadence) != RETURN_ERROR;
    const int seekWatts          = currentTarget + (extrapolatedCurve ? (mode == Mode::INCREASING ? -ERG_MODE_PID_WINDOW : ERG_MODE_PID_WINDOW) : 0);
    int32_t tableResult          = _constrainedTablePosition(seekWatts, currentCadence);
    // Preserve the local feedback offset for a large step only when the
    // present power/cadence observation is quiet and the motor has stopped.
    if (tableResult != RETURN_ERROR && abs(currentTarget - prevWatts.getTarget()) > ERG_TABLE_CORRECTION_WATTS && !ss2k->stepperIsRunning &&
        currentCadence == prevCadence.getValue() && std::abs(responseTrend) < ErgControl::ERG_TRANSIENT_MIN_TREND_WPS &&
        std::abs(responsePreviousTrend) < ErgControl::ERG_TRANSIENT_MIN_TREND_WPS) {
      const int32_t measuredPosition = _constrainedTablePosition(rtConfig->watts.getValue(), currentCadence);
      if (measuredPosition != RETURN_ERROR) {
        const int64_t anchored = static_cast<int64_t>(tableResult) + ss2k->getCurrentPosition() - measuredPosition;
        if (anchored > rtConfig->getMinStep() && anchored < rtConfig->getMaxStep()) tableResult = static_cast<int32_t>(anchored);
      }
    }
    const bool insideTravel      = tableResult > rtConfig->getMinStep() && tableResult < rtConfig->getMaxStep();
    const bool movesTowardTarget = (mode == Mode::INCREASING && tableResult > ss2k->getCurrentPosition()) || (mode == Mode::DECREASING && tableResult < ss2k->getCurrentPosition());
    if (tableResult != RETURN_ERROR && tableResult >= 0 && insideTravel && movesTowardTarget) {
      _startTrustedTableSeek(tableResult);
      return tableResult;
    }
    SS2K_LOG(ERG_MODE_LOG_TAG, "Trusted table result was unusable at %dw/%drpm; using conservative seek", currentTarget, currentCadence);
  }

  const int32_t relativeCorrection = _tableCorrection(rtConfig->watts.getValue(), currentTarget, currentCadence);
  if (relativeCorrection != RETURN_ERROR) {
    _startFeedbackWait();
    return relativeCorrection;
  }

  // An absolute table seek needs a measured surface rather than a lone
  // learned cadence row that can produce an unverified distant position.
  if (!_tableHasSeekSupport()) return _inSetpointState();

  // It's better to undershoot increasing watts and overshoot decreasing watts, so lets set the lookup target to the nearest side of POWERTABLE_WATT_INCREMENT
  int adjustedWattTarget = (mode == Mode::INCREASING) ? currentTarget - ERG_MODE_PID_WINDOW : currentTarget + ERG_MODE_PID_WINDOW;
  int32_t tableResult = powerTable->lookup(adjustedWattTarget, (mode == Mode::INCREASING) ? currentCadence + POWERTABLE_CAD_INCREMENT : currentCadence - POWERTABLE_CAD_INCREMENT);

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
  } else if (tableResult != ss2k->getCurrentPosition()) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "Adjusted setpoint returned: %dw %drpm PowerTable Result: %d", adjustedWattTarget, currentCadence, tableResult);
    _startFeedbackWait();
  }
  return tableResult;
}

void ErgMode::_startFeedbackWait() {
  tableCorrectionNeedsProgress = true;
  if (!isTableSeeking() && !feedbackWaiting) controlMinimum = controlMaximum = ss2k->getCurrentPosition();
  feedbackWaiting      = true;
  feedbackMotorSettled = false;
  feedbackTargetWatts  = rtConfig->watts.getTarget();
  feedbackStartWatts   = rtConfig->watts.getValue();
  feedbackIncreasing   = feedbackTargetWatts > rtConfig->watts.getValue();
  feedbackStartedAt    = millis();
  SS2K_LOG(ERG_MODE_LOG_TAG, "ERG feedback wait: %dw -> %dw; waiting for movement and power response", rtConfig->watts.getValue(), feedbackTargetWatts);
}

void ErgMode::_handleFeedbackWait() {
  if (!feedbackWaiting) return;

  const int target = rtConfig->watts.getTarget();
  if (abs(target - feedbackTargetWatts) <= ERG_MODE_PID_WINDOW) feedbackTargetWatts = target;

  // A different request must not inherit the old move's delay. Cadence-stop
  // handling above also gets priority over waiting for power to catch up.
  if (rtConfig->getFTMSMode() != FitnessMachineControlPointProcedure::SetTargetPower || rtConfig->watts.getTarget() != feedbackTargetWatts ||
      rtConfig->cad.getValue() <= MIN_ERG_CADENCE) {
    feedbackWaiting = false;
    mode            = Mode::MAINTAIN;
    ergTimer        = millis();
    return;
  }

  const auto sample  = rtConfig->watts.getValueSample();
  const uint32_t now = millis();  // Read after the snapshot to avoid unsigned age underflow.
  const bool fresh   = sample.value >= 0 && now - sample.timestamp <= ERG_FEEDBACK_MAX_AGE_MS;
  const char* reason = nullptr;
  bool timedOut      = false;
  if (fresh && ErgControl::tableSeekExceededPowerLimit(feedbackTargetWatts, sample.value, feedbackIncreasing)) {
    reason = "power crossed safety limit";
  } else if (now - feedbackStartedAt >= ERG_TABLE_MOVE_TIMEOUT_MS + ERG_FEEDBACK_TIMEOUT_MS) {
    reason   = "overall timeout";
    timedOut = true;
  } else if (ss2k->stepperIsRunning || ss2k->getCurrentPosition() != ss2k->getTargetPosition()) {
    feedbackMotorSettled = false;
    if (now - feedbackStartedAt < ERG_TABLE_MOVE_TIMEOUT_MS) return;
    reason   = "movement timeout";
    timedOut = true;
  } else {
    if (!feedbackMotorSettled) {
      feedbackMotorSettled = true;
      feedbackSettledAt    = now;
      SS2K_LOG(ERG_MODE_LOG_TAG, "ERG motor settled; waiting %ums for power feedback", static_cast<unsigned>(ERG_FEEDBACK_SETTLE_MS));
    }
    // During a power reduction, a fresh post-move report that rises well
    // beyond both the starting watts and target is already evidence that the
    // first retreat was insufficient. Do not hold that severe overshoot for
    // the full acquisition window before permitting another bounded move.
    if (!feedbackIncreasing && feedbackEarlyRetreatTarget != feedbackTargetWatts && fresh && static_cast<int32_t>(sample.timestamp - feedbackSettledAt) > 0 &&
        sample.value > feedbackStartWatts + ERG_FEEDBACK_WORSENING_WATTS && sample.value > feedbackTargetWatts + ERG_FEEDBACK_HIGH_OVERSHOOT_WATTS) {
      reason                     = "power rose after reduction";
      feedbackEarlyRetreatTarget = feedbackTargetWatts;
    } else {
      // Newly delivered reports can still describe the brake before the move.
      // Require a value sample taken after the complete acquisition interval;
      // target/config writes must not count as new power feedback.
      if (fresh && static_cast<int32_t>(sample.timestamp - feedbackSettledAt) >= static_cast<int32_t>(ERG_FEEDBACK_SETTLE_MS)) {
        const int response        = feedbackIncreasing ? sample.value - feedbackStartWatts : feedbackStartWatts - sample.value;
        const int minimumResponse = std::max(ERG_FEEDBACK_MIN_RESPONSE_WATTS, std::abs(feedbackTargetWatts - feedbackStartWatts) / ERG_FEEDBACK_RESPONSE_DIVISOR);
        if (response < minimumResponse && std::abs(sample.value - feedbackTargetWatts) > ERG_MODE_PID_WINDOW && now - feedbackSettledAt < ERG_FEEDBACK_TIMEOUT_MS) return;
        // A report that is still moving strongly toward the request is not a
        // settled residual. Let that response finish before adding another
        // table-sized correction, bounded by the normal feedback deadline.
        if ((feedbackTargetWatts - sample.value) * responseTrend > 0 && std::abs(responseTrend) >= 2.0 && now - feedbackSettledAt < ERG_FEEDBACK_TIMEOUT_MS) return;
        tableCorrectionNeedsProgress = response < minimumResponse;
        // A completed acquisition starts a new feedback trend history.
        responsePreviousTrend = 0;
        reason                = tableCorrectionNeedsProgress ? "insufficient power response; using proportional recovery" : "power acquisition complete";
      } else if (now - feedbackSettledAt >= ERG_FEEDBACK_TIMEOUT_MS) {
        reason   = "power feedback timeout";
        timedOut = true;
      } else {
        return;
      }
    }
  }

  feedbackWaiting = false;
  // The acquired watts already include this cadence; do not compensate twice.
  cadenceReference = rtConfig->cad.getValue();
  mode             = Mode::MAINTAIN;
  // A timeout must not turn stale power into another corrective move. Wait
  // for the next sample (or a new target) through normal ERG deduplication.
  if (timedOut) prevWatts = rtConfig->watts;
  ergTimer = now;
  SS2K_LOG(ERG_MODE_LOG_TAG, "ERG feedback wait ended (%s): %dw, target %dw", reason, sample.value, feedbackTargetWatts);
}

bool ErgMode::_positionPredictionIsAccurate(int watts, int cadence, int32_t actualPosition) {
  if (watts <= 0 || cadence <= 0) return false;
  const int32_t lowPosition  = powerTable->lookup(std::max(0, watts - ERG_MODE_PID_WINDOW), cadence);
  const int32_t highPosition = powerTable->lookup(watts + ERG_MODE_PID_WINDOW, cadence);
  if (lowPosition == RETURN_ERROR || highPosition == RETURN_ERROR) return false;
  return ErgControl::positionMatchesPowerWindow(actualPosition, lowPosition, highPosition, ERG_TABLE_POSITION_PADDING_STEPS);
}

bool ErgMode::_tableTargetIsWithinMeasuredBounds(int watts, int cadence) const { return ErgControl::recordedTableBounds(powerTable->ptData).contains(watts, cadence); }

bool ErgMode::_tableTargetIsUsable(int watts, int cadence) const {
  // Forward lookup already interpolates sparse rows at equal torque and
  // extrapolates from their measured end segments. Its useful domain is not
  // the rectangle of recorded watt/cadence bins. Require an increasing local
  // surface and physical travel instead of a measured-bin boundary.
  if (watts <= 0 || cadence <= MIN_ERG_CADENCE) return false;
  const int32_t low      = powerTable->lookup(std::max(0, watts - ERG_MODE_PID_WINDOW), cadence);
  const int32_t position = powerTable->lookup(watts, cadence);
  const int32_t high     = powerTable->lookup(watts + ERG_MODE_PID_WINDOW, cadence);
  return low != RETURN_ERROR && high != RETURN_ERROR && position != RETURN_ERROR && low < position && position < high && position > rtConfig->getMinStep() &&
         position < rtConfig->getMaxStep();
}

bool ErgMode::_tableHasSeekSupport() const {
  if (powerTable->hasErgSeekSupport()) return true;
  // Overlapping measured row pairs can constrain a sparse surface before it
  // has two complete rows. Runtime confidence is still required for trust.
  return ErgControl::constrainedTablePosition(powerTable->ptData, rtConfig->watts.getTarget(), rtConfig->cad.getValue()) != RETURN_ERROR;
}

int32_t ErgMode::_constrainedTablePosition(int watts, int cadence) const {
  const int32_t constrained = ErgControl::constrainedTablePosition(powerTable->ptData, watts, cadence);
  const int32_t original    = powerTable->lookup(watts, cadence);
  if (constrained == RETURN_ERROR) return original;
  if (original == RETURN_ERROR) return constrained;
  return static_cast<int32_t>(std::round(original + ERG_TABLE_CURVE_BLEND * (static_cast<double>(constrained) - original)));
}

bool ErgMode::_tableTargetIsTrusted(int watts, int cadence) const { return tableConfidence.trusted() && _tableHasSeekSupport() && _tableTargetIsUsable(watts, cadence); }

void ErgMode::_scoreTable(int watts, int cadence, bool accurate) {
  const bool changed = tableConfidence.update(accurate);
  if (changed) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "Power table is now %s after evidence at %dw/%drpm (confidence %u)", tableConfidence.trusted() ? "trusted" : "untrusted", watts, cadence,
             tableConfidence.score());
  }
}

void ErgMode::_updateTableConfidence() {
  if (!rtConfig->getHomed()) {
    if (confidenceWasHomed) resetTableConfidence();
    confidenceWasHomed = false;
    return;
  }
  confidenceWasHomed = true;

  // A table seek has its own settling gate. Scoring its in-flight readings
  // here would mistake motor travel and power-meter latency for table error.
  if (isTableSeeking() || rtConfig->getFTMSMode() != FitnessMachineControlPointProcedure::SetTargetPower || !spinBLEClient.connectedPM || rtConfig->watts.getSimulate() ||
      ss2k->usePowerTableForPower() || feedbackWaiting) {
    return;
  }

  const auto sample      = rtConfig->watts.getValueSample();
  const uint32_t now     = millis();
  const int cadence      = rtConfig->cad.getValue();
  const int32_t position = ss2k->getCurrentPosition();
  if (ss2k->stepperIsRunning || abs(position - confidencePosition) > ERG_TABLE_SETTLED_POSITION_STEPS || abs(cadence - confidenceCadence) > ERG_TABLE_STABLE_CADENCE_DELTA)
    confidenceSettledAt = now;
  confidencePosition                 = position;
  const unsigned long wattsTimestamp = sample.timestamp;
  if (wattsTimestamp == confidenceWattsTimestamp) return;
  confidenceWattsTimestamp = wattsTimestamp;

  const bool cadenceStable = confidenceCadence > 0 && abs(cadence - confidenceCadence) <= ERG_TABLE_STABLE_CADENCE_DELTA;
  confidenceCadence        = cadence;
  if (!cadenceStable || ss2k->stepperIsRunning || abs(ss2k->getCurrentPosition() - ss2k->getTargetPosition()) > ERG_TABLE_SETTLED_POSITION_STEPS) return;

  const int watts = sample.value;
  if (now - sample.timestamp > ERG_FEEDBACK_MAX_AGE_MS || !_tableHasSeekSupport() || !_tableTargetIsUsable(watts, cadence)) return;

  // This runs before processPowerValue(), so the sample cannot certify a
  // table value that was just adjusted using that same observation.
  const bool accurate = _positionPredictionIsAccurate(watts, cadence, position);
  // Positive evidence can accumulate during small maintenance moves. A miss
  // must describe a stationary brake after the meter's acquisition window;
  // delayed power after a move is not evidence that calibration is wrong.
  if (accurate || static_cast<int32_t>(sample.timestamp - confidenceSettledAt) >= static_cast<int32_t>(ERG_FEEDBACK_SETTLE_MS)) {
    _scoreTable(watts, cadence, accurate);
  }
}

unsigned long ErgMode::_trustedTableMoveDeadline(int32_t position) const {
  const unsigned long speed         = std::max(1, userConfig->getStepperSpeed());
  const unsigned long distance      = static_cast<unsigned long>(abs(position - ss2k->getCurrentPosition()));
  const unsigned long estimatedMove = std::min(static_cast<unsigned long>(ERG_TABLE_MOVE_TIMEOUT_MS), distance * 2000UL / speed);
  return millis() + estimatedMove + ERG_TABLE_SETTLE_TIMEOUT_MS;
}

void ErgMode::_startTrustedTableSeek(int32_t position) {
  tableCorrectionNeedsProgress = true;
  if (!isTableSeeking() && !feedbackWaiting) controlMinimum = controlMaximum = ss2k->getCurrentPosition();
  _trackControlMove(position);
  tableSeekState             = TableSeekState::MOVING;
  tableSeekTargetWatts       = rtConfig->watts.getTarget();
  tableSeekCadence           = rtConfig->cad.getValue();
  tableSeekPosition          = position;
  tableSeekLastWatts         = INT32_MIN;
  tableSeekStableMatches     = 0;
  tableSeekStableMisses      = 0;
  tableSeekWattsTimestamp    = rtConfig->watts.getValueSample().timestamp;
  tableSeekDeadline          = _trustedTableMoveDeadline(position);
  tableSeekPidSeedValid      = false;
  tableSeekStartedAt         = millis();
  tableSeekFirstArrivalValid = false;
  tableSeekStartWatts        = rtConfig->watts.getValue();
  tableSeekStartCadence      = rtConfig->cad.getValue();
  tableSeekOffset            = position - _constrainedTablePosition(tableSeekTargetWatts, tableSeekCadence);
  feedbackWaiting            = false;
  SS2K_LOG(ERG_MODE_LOG_TAG, "Trusted table seek%s: %dw/%drpm directly to %d", _tableTargetIsWithinMeasuredBounds(tableSeekTargetWatts, tableSeekCadence) ? "" : " (extrapolated)",
           tableSeekTargetWatts, tableSeekCadence, position);
}

void ErgMode::_stopTrustedTableSeek(const char* reason, bool seedPidFromTable, bool acquireFeedback) {
  if (!isTableSeeking()) return;
  const auto sample = rtConfig->watts.getValueSample();
  // A stationary, near-target seek can return directly to bounded feedback.
  // Preserve acquisition for stale reports and larger unresolved errors.
  if (acquireFeedback && tableSeekState == TableSeekState::SETTLING && static_cast<uint32_t>(millis()) - sample.timestamp <= ERG_FEEDBACK_MAX_AGE_MS &&
      abs(sample.value - tableSeekTargetWatts) <= 2 * ERG_MODE_PID_WINDOW) {
    acquireFeedback = false;
  }
  SS2K_LOG(ERG_MODE_LOG_TAG, "Trusted table seek ended (%s); %s", reason, acquireFeedback ? "acquiring pending movement feedback" : "resuming PID");
  // Leaving a seek during movement or power acquisition is not an observation
  // of the completed move. Preserve the pending acquisition before permitting
  // another correction from lagged watts.
  if (acquireFeedback) {
    const uint32_t arrivedAt = tableSeekArrivedAt;
    _startFeedbackWait();
    feedbackMotorSettled = tableSeekState == TableSeekState::SETTLING;
    feedbackSettledAt    = arrivedAt;
  }
  tableSeekPidSeedValid    = seedPidFromTable;
  tableSeekPidSeedPosition = tableSeekPosition;
  tableSeekState           = TableSeekState::INACTIVE;
  tableSeekStableMatches   = 0;
  tableSeekStableMisses    = 0;
  tableSeekLastWatts       = INT32_MIN;
  mode                     = Mode::MAINTAIN;
  feedbackWaiting          = acquireFeedback;
  cadenceReference         = tableSeekCadence;
  confidenceSettledAt      = millis();
  ergTimer                 = millis();
}

void ErgMode::_handleTrustedTableSeek() {
  if (!isTableSeeking()) return;
  if (!rtConfig->getHomed() || rtConfig->getFTMSMode() != FitnessMachineControlPointProcedure::SetTargetPower) {
    _stopTrustedTableSeek("ERG or homing state changed");
    return;
  }
  const int target         = rtConfig->watts.getTarget();
  const bool targetAmended = target != tableSeekTargetWatts;
  if (abs(target - tableSeekTargetWatts) > ERG_MODE_PID_WINDOW) {
    _stopTrustedTableSeek("setpoint changed");
    return;
  }
  tableSeekTargetWatts = target;

  const int cadence = rtConfig->cad.getValue();
  if (cadence <= MIN_ERG_CADENCE) {
    _stopTrustedTableSeek("cadence below ERG minimum");
    return;
  }
  const auto powerSample = rtConfig->watts.getValueSample();
  if (static_cast<uint32_t>(millis()) - powerSample.timestamp > ERG_FEEDBACK_MAX_AGE_MS) {
    _stopTrustedTableSeek("power feedback became stale", false, true);
    return;
  }
  // Cadence jitter must never keep restarting a seek's entire deadline.
  if (static_cast<uint32_t>(millis()) - tableSeekStartedAt >= ERG_TABLE_MOVE_TIMEOUT_MS) {
    _stopTrustedTableSeek("overall seek timeout", false, true);
    return;
  }

  // A confirmed safety crossing takes priority over another cadence command.
  // It must be able to hand off immediately for a corrective move.
  const int watts = powerSample.value;
  if (ErgControl::tableSeekExceededPowerLimit(tableSeekTargetWatts, watts, mode == Mode::INCREASING)) {
    _stopTrustedTableSeek(mode == Mode::INCREASING ? "power exceeded high-side safety limit" : "power exceeded low-side safety limit", true);
    return;
  }

  const double sustainedTrend = responseTrend * responsePreviousTrend > 0 ? responseTrend : 0;
  if ((mode == Mode::INCREASING && watts >= tableSeekTargetWatts - ErgControl::ERG_TRANSIENT_APPROACH_WATTS && sustainedTrend >= ErgControl::ERG_TRANSIENT_MIN_TREND_WPS &&
       watts + sustainedTrend * ErgControl::ERG_TRANSIENT_BRAKING_SECONDS > tableSeekTargetWatts + ERG_MODE_PID_WINDOW) ||
      (mode == Mode::DECREASING && watts <= tableSeekTargetWatts + ErgControl::ERG_TRANSIENT_APPROACH_WATTS && sustainedTrend <= -ErgControl::ERG_TRANSIENT_MIN_TREND_WPS &&
       watts + sustainedTrend * ErgControl::ERG_TRANSIENT_BRAKING_SECONDS < tableSeekTargetWatts - ERG_MODE_PID_WINDOW)) {
    _stopTrustedTableSeek("power trend crossed target", false);
    return;
  }

  // Recalculate the feed-forward position as cadence changes. Only move for
  // a physically meaningful difference, but remember every cadence update so
  // single-RPM sensor jitter cannot accumulate into command chatter. Power
  // measured while the motor is moving can lag the new cadence and position;
  // it must not turn a valid feed-forward adjustment into a PID handoff.
  if (targetAmended || cadence != tableSeekCadence) {
    if (!_tableTargetIsTrusted(tableSeekTargetWatts, cadence)) {
      _stopTrustedTableSeek("cadence lookup is no longer usable", true, true);
      return;
    }
    const int32_t lookupPosition   = _constrainedTablePosition(tableSeekTargetWatts, cadence);
    const int64_t adjustedPosition = static_cast<int64_t>(lookupPosition) + tableSeekOffset;
    int32_t cadencePosition        = adjustedPosition > rtConfig->getMinStep() && adjustedPosition < rtConfig->getMaxStep() ? static_cast<int32_t>(adjustedPosition) : RETURN_ERROR;
    double cadenceSlope;
    if (!targetAmended && ErgControl::measuredCadenceSlope(powerTable->ptData, tableSeekTargetWatts, cadence, cadenceSlope)) {
      const double position = tableSeekPosition + cadenceSlope * (cadence - tableSeekCadence);
      if (position > rtConfig->getMinStep() && position < rtConfig->getMaxStep()) cadencePosition = static_cast<int32_t>(std::round(position));
    }
    if (lookupPosition == RETURN_ERROR || cadencePosition == RETURN_ERROR) {
      _stopTrustedTableSeek("cadence lookup failed", false, true);
      return;
    }
    const bool followsCadenceDirection =
        targetAmended || (cadence > tableSeekCadence && cadencePosition <= tableSeekPosition) || (cadence < tableSeekCadence && cadencePosition >= tableSeekPosition);
    tableSeekCadence = cadence;
    // More cadence needs less brake position at a fixed watt target. Sparse
    // edge extrapolation can violate that physical relationship; ignore those
    // feed-forward updates and let measured power feedback correct the seek.
    if (followsCadenceDirection && abs(cadencePosition - tableSeekPosition) > ERG_TABLE_POSITION_PADDING_STEPS) {
      tableSeekPosition      = cadencePosition;
      tableSeekState         = TableSeekState::MOVING;
      tableSeekStableMatches = 0;
      tableSeekStableMisses  = 0;
      tableSeekLastWatts     = INT32_MIN;
      tableSeekDeadline      = _trustedTableMoveDeadline(cadencePosition);
      rtConfig->setTargetIncline(cadencePosition);
      _trackControlMove(cadencePosition);
      SS2K_LOG(ERG_MODE_LOG_TAG, "Trusted table seek followed cadence to %drpm, position %d%s", cadence, cadencePosition,
               _tableTargetIsWithinMeasuredBounds(tableSeekTargetWatts, cadence) ? "" : " (edge extrapolation)");
    }
  }

  const int startingError  = tableSeekTargetWatts - tableSeekStartWatts;
  const int remainingError = tableSeekTargetWatts - watts;
  const int progress       = abs(startingError) - abs(remainingError);
  double responseCadenceSlope;
  // Cadence retargeting can repeatedly restart settling. Once the original
  // move has produced a response, let normal feedback finish the correction
  // if it has a measured cadence slope to continue that compensation.
  if (cadence != tableSeekStartCadence && tableSeekFirstArrivalValid &&
      static_cast<int32_t>(powerSample.timestamp - tableSeekFirstArrivedAt) >= static_cast<int32_t>(ERG_FEEDBACK_SETTLE_MS) &&
      progress >= std::max(ERG_FEEDBACK_MIN_RESPONSE_WATTS, abs(startingError) / ERG_FEEDBACK_RESPONSE_DIVISOR) && abs(remainingError) > ERG_MODE_PID_WINDOW &&
      ErgControl::approachingError(remainingError, responseTrend) != 0 && ErgControl::measuredCadenceSlope(powerTable->ptData, target, cadence, responseCadenceSlope)) {
    _stopTrustedTableSeek("initial table response acquired");
    return;
  }

  const unsigned long now = millis();
  if (tableSeekState == TableSeekState::MOVING) {
    if (static_cast<long>(now - tableSeekDeadline) >= 0) {
      _stopTrustedTableSeek("motor movement timed out", false, true);
      return;
    }
    if (ss2k->stepperIsRunning || abs(ss2k->getCurrentPosition() - tableSeekPosition) > ERG_TABLE_SETTLED_POSITION_STEPS) return;

    tableSeekState     = TableSeekState::SETTLING;
    tableSeekArrivedAt = now;
    if (!tableSeekFirstArrivalValid) {
      tableSeekFirstArrivalValid = true;
      tableSeekFirstArrivedAt    = now;
    }
    tableSeekStableMatches  = 0;
    tableSeekStableMisses   = 0;
    tableSeekLastWatts      = INT32_MIN;
    tableSeekWattsTimestamp = powerSample.timestamp;
    tableSeekDeadline       = now + ERG_TABLE_SETTLE_TIMEOUT_MS;
    SS2K_LOG(ERG_MODE_LOG_TAG, "Trusted table seek reached position %d; waiting for stable power", tableSeekPosition);
    return;
  }

  if (static_cast<long>(now - tableSeekDeadline) >= 0) {
    // Expiring a timer is not a settled observation of table accuracy.
    _stopTrustedTableSeek("power settling timed out", false, true);
    return;
  }

  const unsigned long wattsTimestamp = powerSample.timestamp;
  if (wattsTimestamp == tableSeekWattsTimestamp) return;
  tableSeekWattsTimestamp = wattsTimestamp;

  const bool stable  = tableSeekLastWatts != INT32_MIN && abs(watts - tableSeekLastWatts) <= ERG_TABLE_STABLE_WATTS_DELTA;
  tableSeekLastWatts = watts;
  if (!stable) {
    tableSeekStableMatches = 0;
    tableSeekStableMisses  = 0;
    return;
  }

  const bool accurate = abs(watts - tableSeekTargetWatts) <= ERG_MODE_PID_WINDOW && _positionPredictionIsAccurate(watts, cadence, ss2k->getCurrentPosition());
  if (accurate) {
    ++tableSeekStableMatches;
    tableSeekStableMisses = 0;
  } else {
    ++tableSeekStableMisses;
    tableSeekStableMatches = 0;
  }

  if (tableSeekStableMatches >= ERG_TABLE_STABLE_READINGS) {
    _scoreTable(tableSeekTargetWatts, tableSeekCadence, true);
    _stopTrustedTableSeek("power stabilized inside prediction window");
  } else if (tableSeekStableMisses >= ERG_TABLE_STABLE_READINGS) {
    _scoreTable(tableSeekTargetWatts, tableSeekCadence, false);
    _stopTrustedTableSeek("power stabilized outside prediction window");
  }
}

// Incremental position feedback, with gain scheduling and power-trend braking.
int32_t ErgMode::_inSetpointState() {
  const int watts         = rtConfig->watts.getValue();
  const int target        = rtConfig->watts.getTarget();
  const int measuredError = target - watts;
  int error               = ErgControl::approachingError(measuredError, responseTrend);

  const double sustainedTrend = responseTrend * responsePreviousTrend > 0 ? responseTrend : 0;
  if (abs(measuredError) <= ERG_MODE_PID_WINDOW && std::abs(responseTrend) < 2.0) {
    transitionTarget             = INT32_MIN;
    tableCorrectionNeedsProgress = false;
  }
  if ((tableCorrectionNeedsProgress || transitionTarget == target) && (measuredError * sustainedTrend <= 0 || abs(measuredError) <= ErgControl::ERG_TRANSIENT_APPROACH_WATTS) &&
      std::abs(sustainedTrend) >= ErgControl::ERG_TRANSIENT_MIN_TREND_WPS) {
    error = measuredError - static_cast<int>(std::round(sustainedTrend * ErgControl::ERG_TRANSIENT_BRAKING_SECONDS));
  }

  // A table move must earn another acquisition pause by producing a meaningful
  // power response. After a miss, use fresh-sample proportional control until
  // power settles back inside the target window (or a new target is requested).
  // A single report beyond the limit is usually a pedaling disturbance (standing, shifting position) that has
  // already passed by the next report, so the previous fresh report must agree.
  const int previousError = target - responsePreviousWatts;
  if (!tableCorrectionNeedsProgress && abs(measuredError) > ERG_TABLE_CORRECTION_WATTS && abs(error) > ERG_TABLE_CORRECTION_WATTS && error * measuredError > 0 &&
      responsePreviousFresh && abs(previousError) > ERG_TABLE_CORRECTION_WATTS && previousError * measuredError > 0) {
    const int tableError     = measuredError > 0 ? std::min(measuredError, error) : std::max(measuredError, error);
    const int32_t correction = _tableCorrection(target - tableError, target, rtConfig->cad.getValue());
    if (correction != RETURN_ERROR) {
      mode                  = Mode::MAINTAIN;
      tableSeekPidSeedValid = false;
      _startFeedbackWait();
      return correction;
    }
  }

  // Scale the proportional gain to the local power-table slope. This compensates for the eddy-current brake producing fewer watts per step at low resistance
  // and more watts per step at high resistance. ERG sensitivity controls how much of the predicted correction is applied and bounds bad model slopes.
  const double configuredSensitivity       = userConfig->getERGSensitivity();
  bool usedPowerTable                      = false;
  PowerTableSlopeStatus::Value slopeStatus = PowerTableSlopeStatus::InvalidRequest;
  double Kp                                = scheduledErgGain(configuredSensitivity, target, rtConfig->cad.getValue(), usedPowerTable, slopeStatus);
  const double controlSensitivity          = ErgControl::sanitizeSensitivity(configuredSensitivity);

  Kp = ErgControl::errorScheduledGain(Kp, error, mode == Mode::MAINTAIN);

  if (watts < userConfig->getMinWatts()) {
    Kp = Kp * controlSensitivity;  // Increase gain at very low watts to prevent Zwift from timing out on an initial interval.
  }
  Kp = ErgControl::clampGain(Kp, controlSensitivity);

  mode = Mode::MAINTAIN;

  double PID_output = Kp * error;

  // Cap the change to no more than we can move until the next reading
  int maxChange = round((long)((userConfig->getStepperSpeed() * ERG_MODE_DELAY)) / 1000.0f);  // max change based on stepper speed and delay
  if (PID_output > maxChange) {
    PID_output = maxChange;
  } else if (PID_output < -maxChange) {
    PID_output = -maxChange;
  }

  // Calculate new incline. On the first PID update after a table safety
  // handoff, retain whichever of the cadence feed-forward position or normal
  // PID correction reduces the current error more decisively. This prevents
  // PID from cancelling a useful edge-cadence response before the motor has
  // had a chance to execute it.
  float newIncline  = ss2k->getCurrentPosition() + PID_output;
  const int cadence = rtConfig->cad.getValue();
  double cadenceSlope;
  if (target == prevWatts.getTarget() && cadenceReference > MIN_ERG_CADENCE && cadence != cadenceReference && tableConfidence.trusted() &&
      ErgControl::measuredCadenceSlope(powerTable->ptData, target, cadence, cadenceSlope)) {
    const double cadenceMove = cadenceSlope * (cadence - cadenceReference);
    const bool diverging     = measuredError * sustainedTrend < 0 && std::abs(sustainedTrend) >= ErgControl::ERG_TRANSIENT_MIN_TREND_WPS;
    if ((abs(measuredError) <= ErgControl::ERG_TRANSIENT_APPROACH_WATTS && !diverging) || cadenceMove * measuredError >= 0) {
      newIncline += cadenceMove;
      // Consume the cadence change even if feedback exactly cancels its move.
      cadenceReference = cadence;
    }
  }
  newIncline = std::max(static_cast<float>(ss2k->getCurrentPosition() - maxChange), std::min(static_cast<float>(ss2k->getCurrentPosition() + maxChange), newIncline));
  if (tableSeekPidSeedValid) {
    if (watts > target) {
      newIncline = std::min(newIncline, static_cast<float>(tableSeekPidSeedPosition));
    } else if (watts < target) {
      newIncline = std::max(newIncline, static_cast<float>(tableSeekPidSeedPosition));
    }
    tableSeekPidSeedValid = false;
  }

  // Log output at the configured ERG interval.
  static unsigned long lastTime = 0;
  if (millis() - lastTime > ERG_MODE_LOG_INTERVAL_MS) {
    lastTime = millis();
    SS2K_LOG(ERG_MODE_LOG_TAG, "%dw, Target %dw, Kp: %.3f (%s%s%s), PID Output: %f, Moving to: %f", rtConfig->watts.getValue(), rtConfig->watts.getTarget(), Kp,
             usedPowerTable ? "table" : "fallback", usedPowerTable ? "" : ": ", usedPowerTable ? "" : PowerTableSlopeStatus::name(slopeStatus), PID_output, newIncline);
  }

  return newIncline;
}

int32_t ErgMode::_tableCorrection(int watts, int target, int cadence) {
  if (!rtConfig->getHomed() || watts <= 0 || cadence <= MIN_ERG_CADENCE || !_tableHasSeekSupport()) return RETURN_ERROR;
  const int32_t from = powerTable->lookup(watts, cadence);
  const int32_t to   = powerTable->lookup(target, cadence);
  if (from == RETURN_ERROR || to == RETURN_ERROR || from < 0 || to < 0) return RETURN_ERROR;
  const double distance = static_cast<double>(to) - from;
  if (distance * (target - watts) <= 0) return RETURN_ERROR;
  const double fraction  = std::min(1.0, ErgControl::sanitizeSensitivity(userConfig->getERGSensitivity()) / (tableConfidence.trusted() ? 5.0 : 10.0));
  const double limit     = userConfig->getStepperSpeed() * ERG_MODE_DELAY / 1000.0;
  const double move      = std::max(-limit, std::min(distance * fraction, limit));
  const int32_t position = std::max(rtConfig->getMinStep(), std::min(rtConfig->getMaxStep(), static_cast<int32_t>(ss2k->getCurrentPosition() + move)));
  if (position == ss2k->getCurrentPosition()) return RETURN_ERROR;
  SS2K_LOG(ERG_MODE_LOG_TAG, "Table feedback correction: %dw -> %dw at %drpm, fraction %.2f, position %d", watts, target, cadence, fraction, position);
  return position;
}

void ErgMode::_observePowerResponse() {
  const auto sample  = rtConfig->watts.getValueSample();
  const uint32_t now = millis();
  if (sample.value <= rtConfig->watts.getTarget() + ERG_MODE_PID_WINDOW) feedbackEarlyRetreatTarget = INT32_MIN;
  if (sample.timestamp == responseTimestamp) {
    if (now - sample.timestamp > ERG_FEEDBACK_MAX_AGE_MS) responseTrend = responsePreviousTrend = 0;
    return;
  }
  const uint32_t elapsed = sample.timestamp - responseTimestamp;
  responsePreviousTrend  = responseTrend;
  responsePreviousFresh  = responseTimestamp != 0 && elapsed <= ERG_FEEDBACK_MAX_AGE_MS;
  responsePreviousWatts  = responseWatts;
  responseTrend          = responseTimestamp != 0 && elapsed >= 500 && elapsed <= ERG_FEEDBACK_MAX_AGE_MS ? (sample.value - responseWatts) * 1000.0 / elapsed : 0;
  responseTimestamp      = sample.timestamp;
  responseWatts          = sample.value;
}

void ErgMode::_updateValues(float newIncline) {
  if (isTableSeeking() || feedbackWaiting) _trackControlMove(static_cast<int32_t>(newIncline));
  if (!isTableSeeking() && static_cast<int32_t>(newIncline) != ss2k->getCurrentPosition()) cadenceReference = rtConfig->cad.getValue();
  rtConfig->setTargetIncline(newIncline);
  _writeLog(ss2k->getCurrentPosition(), newIncline, this->prevWatts.getTarget(), rtConfig->watts.getTarget(), this->prevWatts.getValue(), rtConfig->watts.getValue(),
            this->prevCadence.getValue(), rtConfig->cad.getValue());

  this->prevWatts   = rtConfig->watts;
  this->prevCadence = rtConfig->cad;
}

void ErgMode::_writeLog(float currentIncline, float newIncline, int currentSetPoint, int newSetPoint, int currentWatts, int newWatts, int currentCadence, int newCadence) {
  SS2K_LOGW(ERG_MODE_LOG_CSV_TAG, "%.2f;%.2f;%d;%d;%d;%d;%d;%d", currentIncline, newIncline, currentSetPoint, newSetPoint, currentWatts, newWatts, currentCadence, newCadence);
}
