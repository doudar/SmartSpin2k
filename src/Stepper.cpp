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
#include "ERG_Mode.h"
#include "Power_Table.h"
#include "settings.h"
#include <Constants.h>
#include "ThermalSafety.h"
#include "FtmsHoming.h"
#include "freertos/semphr.h"

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

// Serialize UART exchanges and the boundary between homing and safety checks.
// Maintenance uses a nonblocking lock so it never waits for a homing operation.
class DriverLock {
 public:
  explicit DriverLock(bool wait = true) {
    static SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutex();
    configASSERT(mutex);
    mutex_ = mutex;
    locked_ = xSemaphoreTakeRecursive(mutex_, wait ? portMAX_DELAY : 0) == pdTRUE;
  }
  ~DriverLock() {
    if (locked_) xSemaphoreGiveRecursive(mutex_);
  }
  bool locked() const { return locked_; }

 private:
  SemaphoreHandle_t mutex_;
  bool locked_;
};

portMUX_TYPE enableMux = portMUX_INITIALIZER_UNLOCKED;
bool motorInhibited    = true;
bool driverConfigured  = false;
bool driverBegun       = false;
bool configuringDriver = false;
int requestedCurrent   = 0;
int s3CurrentLimit     = 100;
#if defined(SMARTSPIN2K_S3)
bool s3MotorInhibited = true;  // Await the first valid temperature sample.
#else
bool s3MotorInhibited = false;
#endif
ThermalSafety::TmcProtection tmcProtection;
bool homingActive = false;

// Pause safety for the whole original homing routine, including all early exits.
// No driver mutex is held during motion or StallGuard sampling.
class HomingSafetyPause {
 public:
  HomingSafetyPause() {
    DriverLock lock;
    portENTER_CRITICAL(&enableMux);
    homingActive      = true;
    bool wasInhibited = motorInhibited;
    portEXIT_CRITICAL(&enableMux);
    if (wasInhibited && stepper) stepper->setAutoEnable(true);
  }
  ~HomingSafetyPause() {
    DriverLock lock;
    portENTER_CRITICAL(&enableMux);
    homingActive = false;
    portEXIT_CRITICAL(&enableMux);
  }
};

// Outside homing, close the race between thermal inhibition and auto-enable.
// During homing, pass enable requests through unchanged.
bool guardedEnablePin(uint8_t pin, uint8_t value) {
  portENTER_CRITICAL(&enableMux);
  bool level = (!homingActive && motorInhibited) || value == HIGH;
  digitalWrite(pin & ~PIN_EXTERNAL_FLAG, level ? HIGH : LOW);
  portEXIT_CRITICAL(&enableMux);
  return level;
}

void applyMotorInterlock() {
  if (homingActive) return;
  bool inhibit = !driverConfigured || s3MotorInhibited || tmcProtection.disabled();
  if (!inhibit && !ss2k->stepperSafetyReady() && stepper) {
    // Discard any movement submitted while blocked. Reset enable bookkeeping
    // before opening the gate after cadence selected manual enable mode.
    stepper->forceStopAndNewPosition(stepper->getCurrentPosition());
    stepper->disableOutputs();
    stepper->setAutoEnable(true);
  }
  portENTER_CRITICAL(&enableMux);
  bool newlyInhibited = inhibit && !motorInhibited;
  motorInhibited      = inhibit;
  if (inhibit) digitalWrite(currentBoard.enablePin, HIGH);
  portEXIT_CRITICAL(&enableMux);
  if (newlyInhibited) {
    if (stepper) {
      stepper->forceStopAndNewPosition(stepper->getCurrentPosition());
      stepper->disableOutputs();  // Also reset auto-enable's cached enable timer.
    }
    SS2K_LOG(MAIN_LOG_TAG, "Motor inhibited: TMC configured=%d, TMC thermal stop=%d, S3 stop=%d; EN held high", driverConfigured, tmcProtection.disabled(), s3MotorInhibited);
  }
}

void driverCommunicationFailed(const char* stage) {
  driverConfigured  = false;
  applyMotorInterlock();
  SS2K_LOG(MAIN_LOG_TAG, "TMC configuration/communication unavailable at %s; EN held high, retry in 10s", stage);
}

bool applyDriverCurrent(bool logChange) {
  int current = ThermalSafety::limitedCurrent(requestedCurrent, tmcProtection.percent(), s3CurrentLimit);
  // Below the smallest high-sensitivity current step, the requested limit is
  // unrepresentable. In particular, rms_current(0) underflows in TMCStepper 0.7.3.
  float minimumCurrent = 1000.0f * 0.180f / (32.0f * 1.41421f * (currentBoard.rSense + 0.02f));
  if (current < ceilf(minimumCurrent)) {
    SS2K_LOG(MAIN_LOG_TAG, "TMC requested current %d mA is below safe library range; motor inhibited", current);
    driverCommunicationFailed("current range");
    return false;
  }
  driver->rms_current(current, HOLD_PWR_SCALER);
  if (logChange)
    SS2K_LOG(MAIN_LOG_TAG, "Stepper current requested=%d mA, applied=%d mA, TMC limit=%d%%, S3 limit=%d%%", requestedCurrent, current, tmcProtection.percent(), s3CurrentLimit);
  return true;
}

void updateTmcTemperature(bool valid, uint32_t status) {
  auto previous = tmcProtection.state;
  // Higher temperature flags also count as hot. OTP may change the OT threshold.
  bool hot      = (status & ((0xFUL << 8) | 0x1UL)) != 0;
  bool shutdown = (status & 0x2UL) != 0;
  tmcProtection.update(valid, hot, shutdown, millis());
  if (previous != tmcProtection.state) {
    switch (tmcProtection.state) {
      case ThermalSafety::TmcState::Normal:
        SS2K_LOG(MAIN_LOG_TAG, "TMC temperature flags cleared; restoring current subject to S3 limit");
        break;
      case ThermalSafety::TmcState::Reduced:
        SS2K_LOG(MAIN_LOG_TAG, "TMC T120/temperature warning asserted; halving motor current, 30s cooldown deadline");
        break;
      case ThermalSafety::TmcState::Disabled:
        SS2K_LOG(MAIN_LOG_TAG, "TMC cooling failed after 30s or OT shutdown asserted; EN held high until temperature flags clear");
        break;
    }
  }
}

constexpr uint8_t TMC2209_OTP_IHOLD_SHIFT      = 21;
constexpr uint32_t TMC2209_OTP_IHOLD_MASK      = 0x03UL << TMC2209_OTP_IHOLD_SHIFT;
constexpr uint32_t TMC2209_OTP_IHOLD_9_PERCENT = 0x01UL << TMC2209_OTP_IHOLD_SHIFT;
constexpr uint16_t TMC2209_OTP_PROGRAM_IHOLD_9 = 0xBD25;  // Magic 0xBD, OTP byte 2, bit 5.

bool recoverTmc2209OperationalConnection(TMC2209Stepper* tmcDriver) {
  if (!homingActive) {
    // test_connection() infers connectivity from DRV_STATUS != 0. Use the
    // fixed chip identity instead, so status/thermal state cannot block the
    // reads needed to release an interlock. Keep EN high during recovery.
    auto probe = TmcUart::probe(*tmcDriver);
    if (probe.valid()) return true;
    SS2K_LOG(MAIN_LOG_TAG, "TMC UART identity failed: IOIN=0x%08lX, CRC error=%d; forcing idle-high recovery", static_cast<unsigned long>(probe.ioin), probe.crcError);
    initializeStepperSerial(true);
    probe = TmcUart::probe(*tmcDriver);
    if (!probe.valid()) {
      SS2K_LOG(MAIN_LOG_TAG, "TMC UART identity recovery failed: IOIN=0x%08lX, CRC error=%d", static_cast<unsigned long>(probe.ioin), probe.crcError);
      return false;
    }
    SS2K_LOG(MAIN_LOG_TAG, "TMC UART recovered");
    return true;
  }

  // Preserve the original connection check during homing.
  uint8_t connectionStatus = tmcDriver->test_connection();
  if (connectionStatus == 0) {
    return true;
  }

  SS2K_LOG(MAIN_LOG_TAG, "TMC UART test failed (%u); forcing idle-high recovery", static_cast<unsigned>(connectionStatus));
  initializeStepperSerial(true);
  connectionStatus = tmcDriver->test_connection();
  if (connectionStatus != 0) {
    SS2K_LOG(MAIN_LOG_TAG, "TMC UART recovery failed (%u)", static_cast<unsigned>(connectionStatus));
    return false;
  }

  SS2K_LOG(MAIN_LOG_TAG, "TMC UART recovered");
  return true;
}

bool verifyTmc2209ConnectionForOtp(TMC2209Stepper* tmcDriver) {
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
  if (ss2k->ftmsHomingFailed) return;
  if (!ss2k->stepperSafetyReady()) return;
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
  DriverLock lock;
  if (!homingActive) {
    driverConfigured = false;
    applyMotorInterlock();
  }
  if (!driver) {
    driver = new TMC2209Stepper(&stepperSerial, currentBoard.rSense, 0b00);
  }
  const bool initializeFastAccel = !reset || stepper == nullptr;

  // FastAccelStepper only owns the ESP32 pulse-generation peripheral; it does
  // not require a responding TMC UART. Initialize it even when the physical
  // driver is absent so runtime setting writes cannot encounter a null object.
  if (initializeFastAccel) {
    engine.init();
    stepper = engine.stepperConnectToPin(currentBoard.stepPin);
    if (stepper == nullptr) {
      SS2K_LOG(MAIN_LOG_TAG, "Unable to initialize FastAccelStepper on pin %u", static_cast<unsigned>(currentBoard.stepPin));
      return;
    }
    stepper->setDirectionPin(currentBoard.dirPin, userConfig->getStepperDir());
    engine.setExternalCallForPin(guardedEnablePin);
    stepper->setEnablePin(currentBoard.enablePin | PIN_EXTERNAL_FLAG);
    stepper->setAutoEnable(true);
    stepper->setSpeedInHz(DEFAULT_STEPPER_SPEED);
    stepper->setAcceleration(STEPPER_ACCELERATION);
    stepper->setDelayToDisable(65535);
  }

  // Confirm UART reads and writes during setup. Normal temperature polling
  // does not reconfigure or disable a working driver on a missed reply.
  if (!recoverTmc2209OperationalConnection(driver)) {
    if (homingActive) {
      SS2K_LOG(MAIN_LOG_TAG, "Skipping TMC driver setup because UART is unavailable");
    } else {
      driverCommunicationFailed("UART identity");
    }
    return;
  }

  if (!driverBegun && !homingActive) {
    // TMC Driver Setup
    driver->begin();
    if (verifyTmc2209ConnectionForOtp(driver)) {
      programTmc2209LowHoldCurrentOtp(driver);
    } else {
      SS2K_LOG(MAIN_LOG_TAG, "Skipping irreversible TMC OTP programming because IFCNT verification failed");
    }
    driverBegun = true;
  }

  uint8_t setupCounter = 0;
  if (!homingActive) {
    uint32_t status = driver->DRV_STATUS();
    if (driver->CRCerror) {
      driverCommunicationFailed("setup DRV_STATUS (CRC)");
      return;
    }
    SS2K_LOG(MAIN_LOG_TAG, "TMC setup thermal status: DRV_STATUS=0x%08lX", static_cast<unsigned long>(status));
    updateTmcTemperature(true, status);
    setupCounter = driver->IFCNT();
    if (driver->CRCerror) {
      driverCommunicationFailed("setup IFCNT before write (CRC)");
      return;
    }
    driver->GSTAT(1);  // Acknowledge reset; later resets trigger reconfiguration.
  }
  driver->pdn_disable(true);       // Use PDN pin to enable UART communication instead of grounding signal
  driver->mstep_reg_select(true);  // Use register instead of ms1&ms2 pins for microstep selection
  driver->microsteps(4);           // Set microsteps to 1/4
  driver->iholddelay(5);           // Controls the number of clock cycles for motor power down after standstill is detected
  driver->TPOWERDOWN(16);          // delay until hold current (0-255). 255 = 5.6s, 2 is minimum for StealthChop.
  driver->toff(5);                 // needs >0 for driver enable. 1-15 controls duration of slow decay phase of pwm.
  configuringDriver = true;
  this->updateStealthChop();
  configuringDriver = false;
  this->updateStepperSpeed();
  if (homingActive) {
    // Preserve the original between-tap restore: no EN/queue changes, thermal
    // reads, current derating or added IFCNT checks during homing.
    this->updateStepperPower();
    this->setCurrentPosition(stepper->getCurrentPosition());
    return;
  }
  requestedCurrent = userConfig->getStepperPower();
  if (!applyDriverCurrent(true)) return;
  uint8_t finalCounter  = driver->IFCNT();
  if (driver->CRCerror || finalCounter == setupCounter) {
    SS2K_LOG(MAIN_LOG_TAG, "TMC setup writes not acknowledged: IFCNT %u -> %u, read error=%d; retry in 10s", setupCounter, finalCounter, driver->CRCerror);
    driverCommunicationFailed("setup write acknowledgement");
    return;
  }
  driverConfigured = true;
  applyMotorInterlock();
  SS2K_LOG(MAIN_LOG_TAG, "TMC setup complete; UART reads and writes working");
  this->setCurrentPosition(stepper->getCurrentPosition());
}

bool SS2K::stepperSafetyReady() {
  portENTER_CRITICAL(&enableMux);
  bool ready = homingActive || !motorInhibited;
  portEXIT_CRITICAL(&enableMux);
  return ready;
}

void SS2K::updateHardwareSafety() {
  DriverLock lock(false);
  if (!lock.locked() || homingActive) return;
  checkHardwareSafety();
}

void SS2K::updateDriverSafety(int s3Percent, bool s3Disabled) {
  DriverLock lock;
  if (homingActive) return;
  bool limitChanged = s3CurrentLimit != s3Percent;
  s3CurrentLimit    = s3Percent;
  // Apply an S3 temperature stop immediately, independently of TMC telemetry.
  if (s3Disabled) {
    s3MotorInhibited = true;
    applyMotorInterlock();
  }
  s3MotorInhibited = s3Disabled;
  if (!driverConfigured) {
    SS2K_LOG(MAIN_LOG_TAG, "Retrying TMC setup with motor inhibited");
    setupTMCStepperDriver(true);
    return;
  }
  uint32_t status = driver->DRV_STATUS();
  bool valid      = !driver->CRCerror;
  auto previous   = tmcProtection.state;
  updateTmcTemperature(valid, status);
  // Close promptly on overtemperature. A missed read alone is not a motor fault.
  if (tmcProtection.disabled()) applyMotorInterlock();
  if (!valid) {
    SS2K_LOG(MAIN_LOG_TAG, "TMC temperature read failed; keeping previous thermal state, retry in 10s");
  }
  uint8_t resetStatus = driver->GSTAT();
  if (driver->CRCerror) {
    SS2K_LOG(MAIN_LOG_TAG, "TMC reset-status read failed; keeping driver configured, retry in 10s");
  } else if (resetStatus & 1) {
    SS2K_LOG(MAIN_LOG_TAG, "TMC reset detected; reapplying complete configuration");
    setupTMCStepperDriver(true);
    return;
  }
  if ((limitChanged || previous != tmcProtection.state) && !applyDriverCurrent(true)) return;
  bool wasReady = stepperSafetyReady();
  applyMotorInterlock();
  if (!wasReady && stepperSafetyReady()) SS2K_LOG(MAIN_LOG_TAG, "Motor thermal stop cleared; resuming with current limit");
}

static int lastHomingSgThreshold = 0;

static int getScaledHomingSensitivity() { return round(userConfig->getHomingSensitivity() * currentBoard.homingSensitivityScaler); }

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
  int maxSensitivity        = min(HOMING_MAX_SENSITIVITY, max(threshold, 1));
  measuredSensitivity       = constrain(measuredSensitivity + 10, 1, maxSensitivity);
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
  SS2K_LOG(MAIN_LOG_TAG, "pos: %d", stepper->getCurrentPosition());

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
      SS2K_LOG(MAIN_LOG_TAG, "pos: %d", stepper->getCurrentPosition());
      lastLogTime = millis();
      if (moveForward) fitnessMachineService.spinDown(FitnessMachineStatus::SpinDown_StopPedaling);
    }

    // Check for the stall condition
    if (currentSgResult < (baseline.threshold - baseline.sensitivity)) {
      int32_t stallPosition = stepper->getCurrentPosition();
      stepper->forceStop();
      SS2K_LOG(MAIN_LOG_TAG, "Stall detected! SG dropped to %d. Threshold: %d", currentSgResult, baseline.threshold - baseline.sensitivity);
      SS2K_LOG(MAIN_LOG_TAG, "pos: %d", stallPosition);
      delay(100);                   // Let motor settle
      setupTMCStepperDriver(true);  // Restore normal driver settings
      return true;
    }
  }
  // If we get here, the loop timed out
  int32_t timeoutPosition = stepper->getCurrentPosition();
  stepper->forceStop();
  SS2K_LOG(MAIN_LOG_TAG, "Homing timed out!");
  SS2K_LOG(MAIN_LOG_TAG, "pos: %d", timeoutPosition);
  setupTMCStepperDriver(true);  // Restore normal driver settings
  return false;
}

void SS2K::_findFTMSHome(bool bothDirections) {
  // These progress phrases are parsed by the companion calibration widgets.
  SS2K_LOG(MAIN_LOG_TAG, "Starting FTMS Homing...");
  SS2K_LOG(MAIN_LOG_TAG, "FTMS homing request: both=%d resistance=%d range=%d-%d savedMax=%d", bothDirections, rtConfig->resistance.getValue(),
           rtConfig->resistance.getMin(), rtConfig->resistance.getMax(), userConfig->getHMax());
  rtConfig->setHomed(false);
  struct HomingIO {
    int shifterPosition;
    bool searchingMax = false;
    int targetResistance = 10;
    uint32_t lastLog = 0;
    uint32_t now() { return millis(); }
    Measurement::ValueSample sample() { return rtConfig->resistance.getValueSample(); }
    int32_t position() { return stepper->getCurrentPosition(); }
    bool moving() { return stepper->isRunning(); }
    bool cancelled() { return rtConfig->getShifterPosition() != shifterPosition; }
    void stop() { stepper->forceStop(); }
    void logProgress() {
      SS2K_LOG(MAIN_LOG_TAG, "Homing to %s Resistance... Current: %d, Target: %d, pos: %d", searchingMax ? "Max" : "Min", sample().value,
               targetResistance, position());
      lastLog = now();
    }
    void setBoundaryTarget(int target) {
      targetResistance = target;
      logProgress();
    }
    bool moveTo(int32_t target, int speed) {
      SS2K_LOG(MAIN_LOG_TAG, "FTMS seek: resistance=%d position=%d target=%d speed=%d", sample().value, position(), target, speed);
      ss2k->updateStepperSpeed(speed);
      return stepper->moveTo(target) == 0;
    }
    void poll() {
      delay(5);
      ss2k->setCurrentPosition(position());
      if (now() - lastLog > LOG_INTERVAL) {
        logProgress();
        // The companion interprets this status as minimum found / seeking max.
        if (searchingMax) fitnessMachineService.spinDown(FitnessMachineStatus::SpinDown_StopPedaling);
      }
    }
  } io{ss2k->lastShifterPosition};

  auto fail = [&]() {
    if (io.cancelled()) SS2K_LOG(MAIN_LOG_TAG, "Homing aborted by user.");
    ss2k->ftmsHomingFailed = true;
    stepper->forceStop();
    while (stepper->isRunning()) delay(5);
    ss2k->setCurrentPosition(stepper->getCurrentPosition());
    ss2k->setTargetPosition(ss2k->getCurrentPosition());
    rtConfig->setTargetIncline(ss2k->getCurrentPosition());
    setupTMCStepperDriver(true);
    SS2K_LOG(MAIN_LOG_TAG, "FTMS homing failed or aborted; motor held until successful homing. Calibration was not saved.");
  };

  // Only the interior 1..99 levels are required; advertised 0/100 are optional.
  if (!FtmsHoming::supportsRange(rtConfig->resistance.getMin(), rtConfig->resistance.getMax())) {
    SS2K_LOG(MAIN_LOG_TAG, "FTMS transition homing requires the 1-99 interior of the 0-100 resistance scale.");
    fail();
    return;
  }
  if (!bothDirections && userConfig->getHMax() <= 0) {
    SS2K_LOG(MAIN_LOG_TAG, "Full FTMS calibration is required before startup homing.");
    fail();
    return;
  }
  FtmsHoming::Search<HomingIO> search(io);
  int32_t minimum, maximum;
  auto findEndpoint = [&](bool upper, int32_t& endpoint) {
    if (search.endpoint(upper, endpoint)) return true;
    auto sample = io.sample();
    SS2K_LOG(MAIN_LOG_TAG, "FTMS homing failure: %s; resistance=%d simulated=%d age=%lu ms position=%d", FtmsHoming::failureName(search.failure()), sample.value,
             sample.simulate, static_cast<unsigned long>(io.now() - sample.timestamp), io.position());
    if (search.failure() == FtmsHoming::Failure::Timeout) SS2K_LOG(MAIN_LOG_TAG, "Homing timed out!");
    fail();
    return false;
  };
  if (!findEndpoint(false, minimum)) return;
  SS2K_LOG(MAIN_LOG_TAG, "Min position found: 0 (FTMS origin: %d)", minimum);
  if (bothDirections) {
    io.searchingMax = true;
    io.setBoundaryTarget(90);
    fitnessMachineService.spinDown(FitnessMachineStatus::SpinDown_StopPedaling);
    if (!findEndpoint(true, maximum)) return;
  }
  int64_t range = bothDirections ? static_cast<int64_t>(maximum) - minimum : userConfig->getHMax();
  if (range <= 0 || range > INT32_MAX || io.cancelled()) {
    fail();
    return;
  }

  // Rebase only after all requested measurements succeeded. The motor is stopped
  // at a measured interior point; zero is a virtual, extrapolated coordinate.
  int64_t rebased = static_cast<int64_t>(stepper->getCurrentPosition()) - minimum;
  if (rebased < 0 || rebased > INT32_MAX) {
    fail();
    return;
  }
  stepper->setCurrentPosition(static_cast<int32_t>(rebased));
  ss2k->setCurrentPosition(static_cast<int32_t>(rebased));
  rtConfig->setMinStep(0);
  rtConfig->setMaxStep(static_cast<int32_t>(range));
  if (bothDirections) {
    if (!userConfig->getPTab4Pwr()) powerTable->reset();
    userConfig->setHMin(0);
    userConfig->setHMax(static_cast<int32_t>(range));
    userConfig->saveToLittleFS();
    SS2K_LOG(MAIN_LOG_TAG, "Max Position found: %d", static_cast<int32_t>(range));
  }
  setupTMCStepperDriver(true);
  rtConfig->setShifterPosition(0);
  ss2k->setTargetPosition(0);
  rtConfig->setTargetIncline(0);
  stepper->moveTo(0);
  rtConfig->setHomed(true);
  ss2k->ftmsHomingFailed = false;
  SS2K_LOG(MAIN_LOG_TAG, "FTMS homing complete: estimated zero=%d, range=%d steps", minimum, static_cast<int32_t>(range));
  SS2K_LOG(MAIN_LOG_TAG, "Homing procedure complete.");
}

void SS2K::goHome(bool bothDirections) {
  HomingSafetyPause safetyPause;
  SS2K_LOG(MAIN_LOG_TAG, "Starting homing procedure...");
  ergMode->resetTableConfidence();
  const bool useFTMSHoming = !rtConfig->resistance.getSimulate() && strcmp(userConfig->getConnectedPowerMeter(), NONE) != 0 && rtConfig->resistance.getMax() > 0;
  if (bothDirections) {
    fitnessMachineService.spinDown(FitnessMachineStatus::SpinDown_SpinDownRequested);
    if (!userConfig->getPTab4Pwr() && !useFTMSHoming) {
      // clean slate for homing
      powerTable->reset();
    }
  }

  if (!stepper) {
    SS2K_LOG(MAIN_LOG_TAG, "Homing unavailable because the stepper pulse generator is not initialized.");
    fitnessMachineService.spinDown(FitnessMachineStatus::SpinDown_Error);
    return;
  }

  // if we're using real resistance from a FTMS bike, find those values for the reported min and max resistance instead of using hard stops.
  if (useFTMSHoming) {
    ss2k->_findFTMSHome(bothDirections);
    if (rtConfig->getHomed()) {
      fitnessMachineService.spinDown(FitnessMachineStatus::SpinDown_Success);
    } else {
      fitnessMachineService.spinDown(FitnessMachineStatus::SpinDown_Error);
    }
    // An FTMS abort/failure must not start a different, mechanical homing run.
    return;
  }

  if (!currentBoard.homingSupported) {
    SS2K_LOG(MAIN_LOG_TAG, "Homing is not supported by this board.");
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
  if (rtConfig->getHomed()) ss2k->ftmsHomingFailed = false;
}

// Applies current power to driver
void SS2K::updateStepperPower(int pwr) {
  DriverLock lock;
  requestedCurrent = (pwr == 0) ? userConfig->getStepperPower() : pwr;
  if (homingActive) {
    // Original homing current path, including its operational UART recovery.
    if (driver == nullptr || !recoverTmc2209OperationalConnection(driver)) {
      SS2K_LOG(MAIN_LOG_TAG, "Skipping stepper power update because TMC UART is unavailable");
      return;
    }
    uint16_t rmsPwr = requestedCurrent;
    driver->rms_current(rmsPwr, HOLD_PWR_SCALER);
    SS2K_LOG(MAIN_LOG_TAG, "Stepper power is now %d mA (driver setpoint %d mA)", rmsPwr, driver->rms_current());
    return;
  }
  if (!driverConfigured) return;  // The 10s recovery path applies full setup.
  applyDriverCurrent(true);
}

// Applies current StealthChop to driver
void SS2K::updateStealthChop(bool coolStepEnabled) {
  DriverLock lock;
  if (driver == nullptr || (!homingActive && !driverConfigured && !configuringDriver)) {
    SS2K_LOG(MAIN_LOG_TAG, "Skipping StealthChop update because the TMC driver is not initialized");
    return;
  }

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
  if (stepper == nullptr) {
    SS2K_LOG(MAIN_LOG_TAG, "Skipping stepper speed update because FastAccelStepper is unavailable");
    return;
  }

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
