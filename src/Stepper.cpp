/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Stepper.h"
#include "Main.h"
#include "SS2KLog.h"
#include "BLE_Fitness_Machine_Service.h"
#include "Power_Table.h"
#include "settings.h"
#include <Constants.h>

HardwareSerial stepperSerial(2);
// Construct after hardware detection so the selected board's sense resistor is used.
static TMC2209Stepper* driver = nullptr;
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper* stepper     = NULL;

extern Board currentBoard;

void initializeStepperSerial(bool restart) {
  if (restart) {
    stepperSerial.end();
  }

  // The TMC2209 requires an idle-high interval to reset and resynchronize its
  // UART receiver after an incomplete or invalid datagram. Drive TX manually
  // before handing the pin to the UART peripheral so boot state is deterministic.
  digitalWrite(currentBoard.stepperSerialTxPin, HIGH);
  pinMode(currentBoard.stepperSerialTxPin, OUTPUT);
  delay(20);

  stepperSerial.begin(57600, SERIAL_8N1, currentBoard.stepperSerialRxPin, currentBoard.stepperSerialTxPin);
}

namespace {

constexpr uint8_t TMC2209_OTP_IHOLD_SHIFT      = 21;
constexpr uint32_t TMC2209_OTP_IHOLD_MASK      = 0x03UL << TMC2209_OTP_IHOLD_SHIFT;
constexpr uint32_t TMC2209_OTP_IHOLD_9_PERCENT = 0x01UL << TMC2209_OTP_IHOLD_SHIFT;
constexpr uint16_t TMC2209_OTP_PROGRAM_IHOLD_9 = 0xBD25;  // Magic 0xBD, OTP byte 2, bit 5.

bool recoverTmc2209Connection(TMC2209Stepper* tmcDriver) {
  static uint8_t lastInterfaceCounter = 0;
  static bool interfaceCounterValid   = false;

  uint8_t connectionStatus = tmcDriver->test_connection();
  if (connectionStatus == 0) {
    const uint8_t interfaceCounter = tmcDriver->IFCNT();
    if (tmcDriver->CRCerror) {
      SS2K_LOG(MAIN_LOG_TAG, "TMC IFCNT read failed CRC; forcing idle-high recovery");
    } else if (!interfaceCounterValid) {
      lastInterfaceCounter  = interfaceCounter;
      interfaceCounterValid = true;
      return true;
    } else if (interfaceCounter != lastInterfaceCounter) {
      lastInterfaceCounter = interfaceCounter;
      return true;
    } else {
      SS2K_LOG(MAIN_LOG_TAG, "TMC IFCNT did not increment from %u; forcing idle-high recovery", static_cast<unsigned>(interfaceCounter));
    }
  } else {
    SS2K_LOG(MAIN_LOG_TAG, "TMC UART test failed (%u); forcing idle-high recovery", static_cast<unsigned>(connectionStatus));
  }

  initializeStepperSerial(true);
  connectionStatus = tmcDriver->test_connection();

  if (connectionStatus != 0) {
    SS2K_LOG(MAIN_LOG_TAG, "TMC UART recovery failed (%u)", static_cast<unsigned>(connectionStatus));
    return false;
  }

  lastInterfaceCounter = tmcDriver->IFCNT();
  if (tmcDriver->CRCerror) {
    SS2K_LOG(MAIN_LOG_TAG, "TMC UART recovered, but IFCNT read failed CRC");
    interfaceCounterValid = false;
    return false;
  }
  interfaceCounterValid = true;
  SS2K_LOG(MAIN_LOG_TAG, "TMC UART recovered");
  return true;
}

void programTmc2209LowHoldCurrentOtp(TMC2209Stepper* tmcDriver) {
  uint32_t otpRead        = tmcDriver->OTP_READ();
  const uint32_t otpIhold = otpRead & TMC2209_OTP_IHOLD_MASK;
  if (otpIhold == TMC2209_OTP_IHOLD_9_PERCENT) {
    SS2K_LOG(MAIN_LOG_TAG, "TMC OTP hold current is already programmed to 9%%");
    return;
  }
  if (otpIhold != 0) {
    const uint8_t otpIholdSetting = static_cast<uint8_t>(otpIhold >> TMC2209_OTP_IHOLD_SHIFT);
    SS2K_LOG(MAIN_LOG_TAG, "TMC OTP hold current is already programmed (setting %u); leaving irreversible OTP unchanged", static_cast<unsigned>(otpIholdSetting));
    return;
  }

  SS2K_LOG(MAIN_LOG_TAG, "Programming TMC OTP hold current to 9%%");
  tmcDriver->OTP_PROG(TMC2209_OTP_PROGRAM_IHOLD_9);
  delay(10);
  otpRead = tmcDriver->OTP_READ();

  if ((otpRead & TMC2209_OTP_IHOLD_MASK) != TMC2209_OTP_IHOLD_9_PERCENT) {
    // The datasheet recommends retrying a missing OTP bit with a 100 ms programming time.
    tmcDriver->OTP_PROG(TMC2209_OTP_PROGRAM_IHOLD_9);
    delay(100);
    otpRead = tmcDriver->OTP_READ();
  }

  if ((otpRead & TMC2209_OTP_IHOLD_MASK) == TMC2209_OTP_IHOLD_9_PERCENT) {
    SS2K_LOG(MAIN_LOG_TAG, "TMC OTP hold current programmed and verified at 9%%");
  } else {
    SS2K_LOG(MAIN_LOG_TAG, "TMC OTP hold-current programming failed verification (OTP_READ=0x%06lX)", static_cast<unsigned long>(otpRead & 0xFFFFFFUL));
  }
}

}  // namespace

void SS2K::moveStepper() {
  static bool _stepperDir = userConfig->getStepperDir();
  if (stepper) {
    ss2k->stepperIsRunning = stepper->isRunning();
    ss2k->currentPosition  = stepper->getCurrentPosition();
    if (!ss2k->externalControl) {
      if ((rtConfig->getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetPower)) {
#ifdef ERG_GUARDRAILS
        // don't drive lower out of bounds. This is a final test that should never happen.
        if ((stepper->getCurrentPosition() > rtConfig->getTargetIncline()) && (rtConfig->watts.getValue() < rtConfig->watts.getTarget())) {
          rtConfig->setTargetIncline(stepper->getCurrentPosition() + 1);
        }
        // don't drive higher out of bounds. This is a final test that should never happen.
        if ((stepper->getCurrentPosition() < rtConfig->getTargetIncline()) && (rtConfig->watts.getValue() > rtConfig->watts.getTarget())) {
          rtConfig->setTargetIncline(stepper->getCurrentPosition() - 1);
        }
#endif
        ss2k->targetPosition = rtConfig->getTargetIncline();
      } else if ((rtConfig->getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetResistanceLevel)) {
        ss2k->_resistanceMove();
      } else {
        // Simulation Mode
        ss2k->targetPosition = rtConfig->getShifterPosition() * userConfig->getShiftStep();
        ss2k->targetPosition += rtConfig->getTargetIncline() * userConfig->getInclineMultiplier();
      }
    } else {
      // periodically log external control message
      static long int lastTime = millis();
      if (millis() - lastTime > 5000) {
        SS2K_LOG(MAIN_LOG_TAG, "External Control Mode");
        lastTime = millis();
      }
    }

    if (ss2k->syncMode) {
      stepper->stopMove();
      SS2K_LOG(MAIN_LOG_TAG, "Sync Mode");
      stepper->setCurrentPosition(ss2k->targetPosition);
      ss2k->syncMode = false;
    }

    if (ss2k->pelotonIsConnected && !rtConfig->getHomed()) {
      // Peloton + not homed: gently walk away from the edges unless the user is actively shifting past them
      if (rtConfig->resistance.getValue() < rtConfig->getMinResistance()) {  // Below allowed resistance
        // Nudge upward unless the user already asked to move higher
        if (ss2k->targetPosition <= ss2k->getCurrentPosition()) {
          ss2k->targetPosition = ss2k->getCurrentPosition() + 20;
        }
      }
      if (rtConfig->resistance.getValue() > rtConfig->getMaxResistance()) {
        // Nudge downward unless the user already asked to move lower
        if (ss2k->targetPosition > ss2k->getCurrentPosition()) {
          ss2k->targetPosition = ss2k->getCurrentPosition() - 20;
        }
      }
    } else if (!rtConfig->getHomed()) {  // Not homed: keep target inside the provisional range.
      if (ss2k->targetPosition < rtConfig->getMinStep()) {
        ss2k->targetPosition = rtConfig->getMinStep() + 1;
      } else if (ss2k->targetPosition > rtConfig->getMaxStep()) {
        ss2k->targetPosition = rtConfig->getMaxStep() - 1;
      }
    } else {  // Homed: simple clamp to the known good range
      if (ss2k->targetPosition < rtConfig->getMinStep()) {
        ss2k->targetPosition = rtConfig->getMinStep() + 1;
      } else if (ss2k->targetPosition > rtConfig->getMaxStep()) {
        ss2k->targetPosition = rtConfig->getMaxStep() - 1;
      }
    }

    stepper->moveTo(ss2k->targetPosition);

    if (rtConfig->cad.getValue() > 1) {
      stepper->enableOutputs();
      stepper->setAutoEnable(false);
    } else {
      stepper->setAutoEnable(true);
    }
    if (_stepperDir != userConfig->getStepperDir()) {  // User changed the config direction of the stepper wires
      _stepperDir = userConfig->getStepperDir();
      while (stepper->isRunning()) {  // Wait until the motor stops running
        delay(100);
      }
      stepper->setDirectionPin(currentBoard.dirPin, _stepperDir);
    }
    ss2k->currentPosition = stepper->getCurrentPosition();
  }
}

void SS2K::_resistanceMove() {
  // Get absolute position for a given resistance percent (0-100)
  if (rtConfig->resistance.getSimulate()) {
    int32_t minPos, maxPos;
    bool usePwr = false;
    if (userConfig->getHMin() != INT32_MIN && userConfig->getHMax() != INT32_MIN) {
      minPos = userConfig->getHMin();
      maxPos = userConfig->getHMax();
    } else if (rtConfig->getMinStep() != -DEFAULT_STEPPER_TRAVEL && rtConfig->getMaxStep() != DEFAULT_STEPPER_TRAVEL) {
      minPos = rtConfig->getMinStep();
      maxPos = rtConfig->getMaxStep();
    } else {  // No good position information. Fallback to using ERG
      minPos = userConfig->getMinWatts();
      maxPos = userConfig->getMaxWatts();
      usePwr = true;
    }
    int resistancePercent = rtConfig->resistance.getTarget();
    if (resistancePercent < 0) resistancePercent = 0;
    if (resistancePercent > 100) resistancePercent = 100;
    int64_t span = (int64_t)maxPos - (int64_t)minPos;
    int32_t pos  = minPos + (int32_t)round((span * resistancePercent) / 100.0f);
    if (usePwr) {  // fallback to using ERG
      rtConfig->watts.setTarget(pos);
      rtConfig->setFTMSMode(FitnessMachineControlPointProcedure::SetTargetPower);
      return;
    }
    rtConfig->setTargetIncline(pos);
  } else {
    int actualDelta = rtConfig->resistance.getTarget() - rtConfig->resistance.getValue();
    int direction   = (actualDelta > 0) ? 1 : -1;
    if (abs(actualDelta) > 20 - userConfig->getERGSensitivity()) {
      rtConfig->setTargetIncline(ss2k->getCurrentPosition() + userConfig->getShiftStep() * direction);
    } else if (abs(actualDelta) > 1) {
      rtConfig->setTargetIncline(ss2k->getCurrentPosition() + actualDelta * 3 + (userConfig->getERGSensitivity() * direction));
    } else {
      rtConfig->setTargetIncline(ss2k->getCurrentPosition() + actualDelta + (userConfig->getERGSensitivity() * direction));
    }
  }
  ss2k->targetPosition = rtConfig->getTargetIncline();
}

void SS2K::setupTMCStepperDriver(bool reset) {
  if (!driver) {
    driver = new TMC2209Stepper(&stepperSerial, currentBoard.rSense, 0b00);
  }
  const bool initializeFastAccel = !reset || stepper == nullptr;

  // Verify communication before issuing any driver configuration writes. A
  // failed recovery leaves the existing hardware state untouched.
  if (!recoverTmc2209Connection(driver)) {
    SS2K_LOG(MAIN_LOG_TAG, "Skipping TMC driver setup because UART is unavailable");
    return;
  }

  // FastAccel setup
  if (initializeFastAccel) {
    engine.init();
    stepper = engine.stepperConnectToPin(currentBoard.stepPin);
    stepper->setDirectionPin(currentBoard.dirPin, userConfig->getStepperDir());
    stepper->setEnablePin(currentBoard.enablePin);
    stepper->setAutoEnable(true);
    stepper->setSpeedInHz(DEFAULT_STEPPER_SPEED);
    stepper->setAcceleration(STEPPER_ACCELERATION);
    stepper->setDelayToDisable(65535);
    // TMC Driver Setup
    driver->begin();
    programTmc2209LowHoldCurrentOtp(driver);
  }

  driver->pdn_disable(true);       // Use PDN pin to enable UART communication instead of grounding signal
  driver->mstep_reg_select(true);  // Use register instead of ms1&ms2 pins for microstep selection
  driver->microsteps(4);           // Set microsteps to 1/4
  driver->iholddelay(5);           // Controls the number of clock cycles for motor power down after standstill is detected
  driver->TPOWERDOWN(16);          // delay until hold current (0-255). 255 = 5.6s, 2 is minimum for StealthChop.
  driver->toff(5);                 // needs >0 for driver enable. 1-15 controls duration of slow decay phase of pwm.
  this->updateStealthChop();
  this->updateStepperSpeed();
  this->updateStepperPower();
  this->setCurrentPosition(stepper->getCurrentPosition());
}

static int lastHomingSgThreshold = 0;

static int getScaledHomingSensitivity() {
  return round(userConfig->getHomingSensitivity() * currentBoard.homingSensitivityScaler);
}

static HomingSgBaseline getHomingSgBaseline() {
  int samples[HOMING_SG_SAMPLE_COUNT];
  int totalSgResult  = 0;
  int minSampleIndex = 0;
  int maxSampleIndex = 0;

  for (int i = 0; i < HOMING_SG_SAMPLE_COUNT; i++) {
    samples[i] = driver->SG_RESULT();
    if (samples[i] == 0) {
      delay(30);
      samples[i] = driver->SG_RESULT();
    }
    totalSgResult += samples[i];
    if (samples[i] < samples[minSampleIndex]) minSampleIndex = i;
    if (samples[i] > samples[maxSampleIndex]) maxSampleIndex = i;
    delay(30);
  }

  int trimmedTotal = totalSgResult - samples[minSampleIndex] - samples[maxSampleIndex];
  int trimmedCount = HOMING_SG_SAMPLE_COUNT - 2;
  int trimmedMin   = INT_MAX;
  int trimmedMax   = INT_MIN;

  for (int i = 0; i < HOMING_SG_SAMPLE_COUNT; i++) {
    if (i == minSampleIndex || i == maxSampleIndex) continue;
    if (samples[i] < trimmedMin) trimmedMin = samples[i];
    if (samples[i] > trimmedMax) trimmedMax = samples[i];
  }

  int configuredSensitivity = getScaledHomingSensitivity();
  int threshold             = round(trimmedTotal / (float)trimmedCount);
  int normalLowDrop         = threshold - trimmedMin;
  int measuredSensitivity   = max(configuredSensitivity, normalLowDrop + max(configuredSensitivity / 2, HOMING_SG_MIN_SAMPLE_MARGIN));
  int maxSensitivity      = min(HOMING_MAX_SENSITIVITY, max(threshold, 1));
  measuredSensitivity     = constrain(measuredSensitivity + 10, 1, maxSensitivity);
  SS2K_LOG(MAIN_LOG_TAG, "Homing SG baseline used %d/%d trimmed samples. Dropped: %d/%d, Spread: %d-%d, measured sensitivity: %d", trimmedCount, HOMING_SG_SAMPLE_COUNT,
           samples[minSampleIndex], samples[maxSampleIndex], trimmedMin, trimmedMax, measuredSensitivity);
  return {threshold, measuredSensitivity};
}

/**
 * @brief Private helper function to find a single end stop using StallGuard.
 * @param moveForward True to move forward to find the max end stop, false to move backward for the min.
 */
bool SS2K::_findEndStop(bool moveForward) {
  unsigned long timeoutTimer = millis();
  HomingSgBaseline baseline  = {0, getScaledHomingSensitivity()};

  // --- SETUP DRIVER FOR SENSORLESS HOMING ---
  // Use very low power for sensitive stall detection
  updateStealthChop(false);
  updateStepperPower(userConfig->getStepperPower() * PWR_SCALER_FOR_HOMING);  // Use reduced power for homing. This prevents a stuck knob, we can free it using higher power.
  updateStepperSpeed(1500);                                                   // Use a slow-medium speed for homing

  // Start the motor moving in the specified direction
  if (moveForward) {
    stepper->runForward();
  } else {
    stepper->runBackward();
  }

  // Wait for the motor to reach a stable speed before sampling
  delay(300);

  baseline              = getHomingSgBaseline();
  lastHomingSgThreshold = baseline.threshold;

  SS2K_LOG(MAIN_LOG_TAG, "Homing %s. Stable Threshold: %d, Sensitivity: %d", moveForward ? "forward (max)" : "backward (min)", baseline.threshold, baseline.sensitivity);

  unsigned long lastLogTime = millis() - LOG_INTERVAL;  // Initialize last log time
  int currentSgResult       = 0;
  while ((millis() - timeoutTimer) < HOME_TIMEOUT) {
    delay(5);
    // Allow user to abort the homing process with a shift
    if (rtConfig->getShifterPosition() != ss2k->lastShifterPosition) {
      SS2K_LOG(MAIN_LOG_TAG, "Homing aborted by user.");
      stepper->forceStop();
      setupTMCStepperDriver(true);  // Restore normal driver settings
      return false;
    }

    currentSgResult = driver->SG_RESULT();
    // if zero detected, wait 10ms and sample again.
    if (currentSgResult == 0) {
      delay(10);
      currentSgResult = driver->SG_RESULT();
    }

    // Periodically log the status for tuning
    if (millis() - lastLogTime > LOG_INTERVAL) {
      SS2K_LOG(MAIN_LOG_TAG, "Homing... Current SG: %d, Baseline: %d, Target: < %d", currentSgResult, baseline.threshold, baseline.threshold - baseline.sensitivity);
      lastLogTime = millis();
      if (moveForward) fitnessMachineService.spinDown(FitnessMachineStatus::SpinDown_StopPedaling);
    }

    // Check for the stall condition
    if (currentSgResult < (baseline.threshold - baseline.sensitivity)) {
      stepper->forceStop();
      SS2K_LOG(MAIN_LOG_TAG, "Stall detected! SG dropped to %d. Threshold: %d", currentSgResult, baseline.threshold - baseline.sensitivity);
      delay(100);                   // Let motor settle
      setupTMCStepperDriver(true);  // Restore normal driver settings
      return true;
    }
  }
  // If we get here, the loop timed out
  stepper->forceStop();
  SS2K_LOG(MAIN_LOG_TAG, "Homing timed out!");
  setupTMCStepperDriver(true);  // Restore normal driver settings
  return false;
}

void SS2K::_findFTMSHome(bool bothDirections) {
  SS2K_LOG(MAIN_LOG_TAG, "Starting FTMS Homing...");
  unsigned long timer       = millis();
  unsigned long lastLogTime = 0;
  int lastResistance        = 0;
  int i                     = 0;
  const int iMax            = 600;

  auto runHomingSweep = [&](int targetResistance, const char* logTemplate, bool notifySpinDown) {
    timer                   = millis();
    i                       = 0;
    int32_t lastPosition    = ss2k->getCurrentPosition();
    const int32_t minTravel = userConfig->getShiftStep();
    while ((rtConfig->resistance.getValue() != targetResistance) && ((i < iMax) || (abs(ss2k->getCurrentPosition() - lastPosition) < minTravel))) {
      if (millis() - timer > HOME_TIMEOUT) {
        SS2K_LOG(MAIN_LOG_TAG, "FTMS Homing timed out!");
        setupTMCStepperDriver(true);  // Restore normal driver settings
        return;
      }
      if (rtConfig->getShifterPosition() != ss2k->lastShifterPosition) {
        SS2K_LOG(MAIN_LOG_TAG, "FTMS Homing aborted by user.");
        stepper->forceStop();
        setupTMCStepperDriver(true);  // Restore normal driver settings
        return;
      }
      ss2k->setCurrentPosition(stepper->getCurrentPosition());
      rtConfig->resistance.setTarget(targetResistance);
      rtConfig->setTargetIncline(ss2k->getCurrentPosition());
      ss2k->_resistanceMove();
      stepper->moveTo(ss2k->targetPosition);
      delay(5);
      if (lastResistance != rtConfig->resistance.getValue()) {
        lastResistance = rtConfig->resistance.getValue();
        lastPosition   = ss2k->getCurrentPosition();
        i              = 0;
      }
      if (logTemplate && (millis() - lastLogTime > LOG_INTERVAL)) {
        SS2K_LOG(MAIN_LOG_TAG, logTemplate, rtConfig->resistance.getValue(), targetResistance);
        if (notifySpinDown) {
          fitnessMachineService.spinDown(FitnessMachineStatus::SpinDown_StopPedaling);
        }
        lastLogTime = millis();
      }
      i++;
    }

    bool reachedTarget   = (rtConfig->resistance.getValue() == targetResistance);
    int32_t travelDelta  = abs(ss2k->getCurrentPosition() - lastPosition);
    bool iterExceeded    = (i >= iMax);
    bool travelSatisfied = (travelDelta >= minTravel);
    SS2K_LOG(MAIN_LOG_TAG, "FTMS Homing sweep exit: target=%d current=%d reached=%s iter=%d/%d travelΔ=%d minTravel=%d travelMet=%s", targetResistance,
             rtConfig->resistance.getValue(), reachedTarget ? "true" : "false", i, iMax, travelDelta, minTravel, travelSatisfied ? "true" : "false");
  };

  ss2k->updateStepperSpeed(1500);  // Use a slow-medium speed for homing

  // first back off of the stop if we're already there
  int midTarget = round((rtConfig->resistance.getMax() - rtConfig->resistance.getMin()) / 4.0f);
  rtConfig->resistance.setTarget(midTarget);
  runHomingSweep(midTarget, nullptr, false);
  runHomingSweep(rtConfig->resistance.getMin(), "Homing to Min Resistance... Current: %d, Target: %d", false);
  lastResistance = rtConfig->resistance.getValue();

  // log found positions
  SS2K_LOG(MAIN_LOG_TAG, "Found Min Resistance Position: %d", rtConfig->resistance.getValue());
  stepper->setCurrentPosition(0);
  ss2k->setCurrentPosition(0);
  ss2k->setTargetPosition(0);
  rtConfig->setTargetIncline(0);
  rtConfig->setMinStep(0);
  if (bothDirections) {
    runHomingSweep(rtConfig->resistance.getMax(), "Homing to Max Resistance... Current: %d, Target: %d", true);
    rtConfig->setMaxStep(stepper->getCurrentPosition());
    userConfig->setHMin(rtConfig->getMinStep());
    userConfig->setHMax(rtConfig->getMaxStep());
    SS2K_LOG(MAIN_LOG_TAG, "Found Max Resistance Position: %d", rtConfig->resistance.getValue());
  }
  setupTMCStepperDriver(true);
  rtConfig->setShifterPosition(0);
  ss2k->setTargetPosition(0);
  rtConfig->setTargetIncline(0);
  stepper->moveTo(0);
  rtConfig->setMaxStep(userConfig->getHMax());  // Ensure it's set from config if not found
  rtConfig->setHomed(true);
  userConfig->saveToLittleFS();
}

void SS2K::goHome(bool bothDirections) {
  SS2K_LOG(MAIN_LOG_TAG, "Starting homing procedure...");
  if (bothDirections) {
    fitnessMachineService.spinDown(FitnessMachineStatus::SpinDown_SpinDownRequested);
    if (!userConfig->getPTab4Pwr()) {
      // clean slate for homing
      powerTable->reset();
    }
  }

  // if we're using real resistance from a FTMS bike, find those values for the reported min and max resistance instead of using hard stops.
  if (!rtConfig->resistance.getSimulate() && userConfig->getConnectedPowerMeter() != NONE && rtConfig->resistance.getMax() > 0) {
    ss2k->_findFTMSHome(bothDirections);
    if (rtConfig->getHomed()) {
      fitnessMachineService.spinDown(FitnessMachineStatus::SpinDown_Success);
      return;
    }
  }

  if (!stepper || !currentBoard.homingSupported) {
    SS2K_LOG(MAIN_LOG_TAG, "Homing not supported or stepper not initialized.");
    fitnessMachineService.spinDown(FitnessMachineStatus::SpinDown_Error);
    return;
  }

  const int32_t homingBackoffSteps = (userConfig->getShiftStep() > DEFAULT_SHIFT_STEP ? userConfig->getShiftStep() : DEFAULT_SHIFT_STEP) * 2;

  auto backOffEndStop = [&](bool moveForward, bool recovery = false) {
    int32_t backoffSteps = recovery ? homingBackoffSteps * HOMING_RECOVERY_BACKOFF_MULT : homingBackoffSteps;
    if (recovery) updateStepperPower(userConfig->getStepperPower());
    stepper->move(moveForward ? -backoffSteps : backoffSteps, true);
    if (recovery) updateStepperPower(userConfig->getStepperPower() * PWR_SCALER_FOR_HOMING);
  };

  auto findStableEndStop = [&](bool moveForward, const char* endStopName) -> bool {
    int32_t previousPosition = 0;
    bool havePrevious        = false;
    bool haveBaseThreshold   = false;
    int baseThreshold        = 0;
    int stableTapCount       = 0;
    int requiredStableTaps   = 2;

    for (int attempt = 1; attempt <= HOMING_TAP_MAX_ATTEMPTS; attempt++) {
      if (!ss2k->_findEndStop(moveForward)) {
        SS2K_LOG(MAIN_LOG_TAG, "%s end stop search failed on tap %d/%d.", endStopName, attempt, HOMING_TAP_MAX_ATTEMPTS);
        return false;
      }

      int32_t foundPosition  = stepper->getCurrentPosition();
      int thresholdDelta     = abs(lastHomingSgThreshold - baseThreshold);
      int thresholdReference = max(abs(baseThreshold), 1);
      if (haveBaseThreshold && (thresholdDelta * 100) > (thresholdReference * HOMING_SG_MAX_THRESHOLD_DRIFT)) {
        SS2K_LOG(MAIN_LOG_TAG, "%s homing SG threshold drifted from %d to %d. Recovering from possible stalled baseline.", endStopName, baseThreshold, lastHomingSgThreshold);
        backOffEndStop(moveForward, true);
        haveBaseThreshold  = false;
        havePrevious       = false;
        stableTapCount     = 0;
        requiredStableTaps = 2;
        continue;
      }

      if (!haveBaseThreshold) {
        baseThreshold     = lastHomingSgThreshold;
        haveBaseThreshold = true;
      }

      if (havePrevious) {
        int32_t tapDelta = abs(foundPosition - previousPosition);
        SS2K_LOG(MAIN_LOG_TAG, "%s end stop tap %d/%d found %d, previous %d, delta %d steps", endStopName, attempt, HOMING_TAP_MAX_ATTEMPTS, foundPosition, previousPosition,
                 tapDelta);
        if (tapDelta <= HOMING_TAP_TOLERANCE) {
          stableTapCount++;
          if (stableTapCount >= requiredStableTaps) {
            SS2K_LOG(MAIN_LOG_TAG, "%s end stop stable with %d consecutive taps within %d steps.", endStopName, stableTapCount, HOMING_TAP_TOLERANCE);
            return true;
          }
        } else {
          if (attempt == 2) requiredStableTaps = HOMING_TAP_REQUIRED_STABLE;
          stableTapCount = 1;
        }
      } else {
        SS2K_LOG(MAIN_LOG_TAG, "%s end stop tap %d/%d found %d", endStopName, attempt, HOMING_TAP_MAX_ATTEMPTS, foundPosition);
        stableTapCount = 1;
      }

      previousPosition = foundPosition;
      havePrevious     = true;
      if (attempt < HOMING_TAP_MAX_ATTEMPTS) backOffEndStop(moveForward);
    }

    SS2K_LOG(MAIN_LOG_TAG, "%s end stop did not stabilize within %d taps.", endStopName, HOMING_TAP_MAX_ATTEMPTS);
    return false;
  };

  // --- FIND MIN END STOP (Mandatory) ---
  // First, back off the limit in case we are already there
  backOffEndStop(false);
  if (!findStableEndStop(false, "Min")) {
    setupTMCStepperDriver(true);
    rtConfig->setHomed(false);
    fitnessMachineService.spinDown(FitnessMachineStatus::SpinDown_Error);
    return;
  }
  stepper->move(userConfig->getShiftStep(), true);  // Back off the end stop slightly
  stepper->setCurrentPosition(0);
  ss2k->setTargetPosition(0);
  rtConfig->setMinStep(0);
  SS2K_LOG(MAIN_LOG_TAG, "Min position found and set to 0.");

  // --- FIND MAX END STOP (Optional) ---
  if (bothDirections) {
    fitnessMachineService.spinDown(FitnessMachineStatus::SpinDown_StopPedaling);
    if (!findStableEndStop(true, "Max")) {
      setupTMCStepperDriver(true);
      rtConfig->setHomed(false);
      fitnessMachineService.spinDown(FitnessMachineStatus::SpinDown_Error);
      return;
    }
    rtConfig->setMaxStep(stepper->getCurrentPosition() - userConfig->getShiftStep());
    userConfig->setHMax(rtConfig->getMaxStep());
    SS2K_LOG(MAIN_LOG_TAG, "Max Position found: %d", rtConfig->getMaxStep());
  }

  rtConfig->setHomed(true);
  setupTMCStepperDriver(true);  // Restore normal driver settings
  rtConfig->setShifterPosition(0);
  ss2k->setTargetPosition(0);
  stepper->moveTo(0);
  if (bothDirections) fitnessMachineService.spinDown(FitnessMachineStatus::SpinDown_Success);

  // --- FINALIZE AND SAVE ---
  rtConfig->setMaxStep(userConfig->getHMax());  // Ensure max step is set from config if not found
  if (bothDirections) {
    userConfig->setHMin(rtConfig->getMinStep());
    userConfig->setHMax(rtConfig->getMaxStep());
    userConfig->saveToLittleFS();
  } else if (rtConfig->getMaxStep() < rtConfig->getMinStep()) {  // homing failed
    SS2K_LOG(MAIN_LOG_TAG, "Homing failed. Positions were reversed. Min:%d Max:%d", rtConfig->getMinStep(), rtConfig->getMaxStep());
    rtConfig->setMaxStep(INT32_MIN);
    rtConfig->setMinStep(INT32_MIN);
    rtConfig->setHomed(false);
  }
  SS2K_LOG(MAIN_LOG_TAG, "Homing procedure complete.");
}

// Applies current power to driver
void SS2K::updateStepperPower(int pwr) {
  if (driver == nullptr || !recoverTmc2209Connection(driver)) {
    SS2K_LOG(MAIN_LOG_TAG, "Skipping stepper power update because TMC UART is unavailable");
    return;
  }

  uint16_t rmsPwr = (pwr == 0) ? userConfig->getStepperPower() : pwr;
  driver->rms_current(rmsPwr, HOLD_PWR_SCALER);
  SS2K_LOG(MAIN_LOG_TAG, "Stepper power is now %d mA (driver setpoint %d mA)", rmsPwr, driver->rms_current());
}

// Applies current StealthChop to driver
void SS2K::updateStealthChop(bool coolStepEnabled) {
  bool stealthChopEnabled = userConfig->getStealthChop();
  driver->en_spreadCycle(!stealthChopEnabled);
  driver->pwm_autoscale(stealthChopEnabled);
  driver->pwm_autograd(stealthChopEnabled);

  // Reuse homing sensitivity as CoolStep load tolerance when StealthChop is active.
  uint8_t coolstepTolerance = (uint8_t)constrain(userConfig->getHomingSensitivity(), 0, 255);
  if (stealthChopEnabled && coolStepEnabled) {
    driver->SGTHRS(coolstepTolerance);
    driver->semin(1);  // Enable CoolStep
    driver->seup(1);
    driver->sedn(1);
    driver->semax((uint8_t)constrain((coolstepTolerance / 16) + 1, 1, 15));
    driver->seimin(false);
  } else {
    driver->semin(0);  // Disable CoolStep
    driver->SGTHRS(0);
  }

  SS2K_LOG(MAIN_LOG_TAG, "StealthChop:%d CoolStep:%d SGTHRS:%d", stealthChopEnabled, stealthChopEnabled && coolStepEnabled, coolstepTolerance);
}

// Applies userconfig stepper speed if speed not specified
/**
 * @brief Updates the speed of the stepper motor.
 *
 * This function updates the speed of the stepper motor to the specified value.
 * If the provided speed is 0, it retrieves the speed from the user configuration.
 * The function also includes a tolerance check to avoid unnecessary updates if
 * the current speed is within 5 units of the target speed.
 *
 * @param speed The desired speed for the stepper motor. If 0, the speed is retrieved from user configuration.
 */
void SS2K::updateStepperSpeed(int speed) {
  if (speed == 0) {
    speed = userConfig->getStepperSpeed();
  }
  int s = stepper->getSpeedInMilliHz() / 1000;
  // Because the conversion to/from the TMC driver is not perfect, we need to allow a little bit of slop.
  // Skip the update if the speed is within 5 of the target.
  if (abs(s - speed) < 5) {
    return;
  }
  // SS2K_LOG(MAIN_LOG_TAG, "StepperSpeed is now %d, %d", speed, s);
  stepper->setSpeedInHz(speed);
}
