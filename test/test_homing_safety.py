"""Verify production homing-scope exit restores current limits and motor gates."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestHomingSafety(unittest.TestCase):
    def test_exit_restores_saved_protection_on_success_and_early_return(self):
        source = (ROOT / "src/Stepper.cpp").read_text(encoding="utf-8")
        production = "\n".join(function(source, signature) for signature in (
            "bool guardedEnablePin(",
            "void applyMotorInterlock(bool resumeAfterHoming) {",
            "void driverCommunicationFailed(",
            "bool applyDriverCurrent(bool logChange) {",
        ))
        production += "\n" + function(source, "class HomingSafetyPause {") + ";"
        harness = r'''
#include <cassert>
#include <initializer_list>
#include "ThermalSafety.h"
#define SS2K_LOG(...) ((void)0)
#define portENTER_CRITICAL(mux) ((void)0)
#define portEXIT_CRITICAL(mux) ((void)0)
constexpr int HIGH = 1, LOW = 0, PIN_EXTERNAL_FLAG = 0x80;
constexpr float HOLD_PWR_SCALER = 0.1f;
int enableMux = 0, enableLevel = HIGH, lockDepth = 0;
uint32_t clockMs = 1000;
uint32_t millis() { return clockMs; }
void digitalWrite(int, int level) { enableLevel = level; }
struct DriverLock {
  DriverLock() { ++lockDepth; }
  ~DriverLock() { --lockDepth; }
};
bool homingActive = false, motorInhibited = false;
bool driverConfigured = true, s3MotorInhibited = false;
int requestedCurrent = 800, s3CurrentLimit = 100;
ThermalSafety::TmcProtection tmcProtection;
struct Board { int enablePin = 1; float rSense = 0.08f; } currentBoard;
struct Config { int current = 800; int getStepperPower() { return current; } } config;
Config* userConfig = &config;
struct Controller {
  bool stepperSafetyReady() { return homingActive || !motorInhibited; }
} controller;
Controller* ss2k = &controller;
bool guardedEnablePin(uint8_t, uint8_t);
void applyMotorInterlock(bool resumeAfterHoming = false);
struct Motor {
  bool queued = false;
  int stops = 0;
  int getCurrentPosition() { return 123; }
  void forceStopAndNewPosition(int) {
    assert(enableLevel == HIGH);
    queued = false;
    ++stops;
  }
  void disableOutputs() { guardedEnablePin(currentBoard.enablePin | PIN_EXTERNAL_FLAG, HIGH); }
  void setAutoEnable(bool) {}
} motor;
Motor* stepper = &motor;
struct Driver {
  int appliedCurrent = 800, writes = 0;
  bool expectInhibit = false;
  void rms_current(int value, float) {
    assert(lockDepth == 1 && !homingActive);
    // Inhibited moves must be stopped before the potentially slow UART write.
    if (expectInhibit) assert(enableLevel == HIGH && (!stepper || !stepper->queued));
    appliedCurrent = value;
    ++writes;
  }
} hardware;
Driver* driver = &hardware;
/* FIRMWARE */
void homing(bool earlyReturn) {
  HomingSafetyPause pause;
  assert(homingActive);
  // Homing's original current restores and enable bypass remain available.
  requestedCurrent = 200;
  if (driver) hardware.appliedCurrent = config.current;
  guardedEnablePin(currentBoard.enablePin | PIN_EXTERNAL_FLAG, LOW);
  assert(enableLevel == LOW);
  if (stepper) motor.queued = true; // Final return-to-zero move, or an early exit.
  if (earlyReturn) return;
  assert(hardware.writes == 0 && motor.stops == 0);
}
int main() {
  for (bool earlyReturn : {false, true}) {
    for (int scenario = 0; scenario < 9; ++scenario) {
      config.current = 800;
      hardware = Driver{};
      driver = &hardware;
      motor = Motor{};
      stepper = &motor;
      homingActive = motorInhibited = s3MotorInhibited = false;
      driverConfigured = true;
      enableLevel = HIGH;
      requestedCurrent = 800;
      s3CurrentLimit = 100;
      tmcProtection = ThermalSafety::TmcProtection{};
      clockMs = 1000;
      if (scenario == 1 || scenario == 3 || scenario == 5)
        tmcProtection.state = ThermalSafety::TmcState::Reduced;
      if (scenario == 2) s3CurrentLimit = 75;
      if (scenario == 3) s3CurrentLimit = 60; // Stricter TMC limit wins.
      if (scenario == 4) tmcProtection.state = ThermalSafety::TmcState::Disabled;
      if (scenario == 5) clockMs = ThermalSafety::TMC_COOLDOWN_MS; // Expired during homing.
      if (scenario == 6) s3MotorInhibited = true; // Temperature or failed sensor.
      if (scenario == 7 || scenario == 8) driverConfigured = false;
      if (scenario == 8) { driver = nullptr; stepper = nullptr; }
      const bool inhibited = scenario >= 4;
      // Already latched before homing, except the deadline that expires during it.
      motorInhibited = inhibited && scenario != 5;
      hardware.expectInhibit = inhibited;
      homing(earlyReturn);
      assert(!homingActive && lockDepth == 0);
      assert(motorInhibited == inhibited);
      assert(config.current == 800 && requestedCurrent == 800);
      assert(hardware.writes == (driverConfigured ? 1 : 0));
      if (driverConfigured) {
        const int expected = scenario == 2 ? 600 :
            ((scenario == 1 || scenario == 3 || scenario == 4 || scenario == 5) ? 400 : 800);
        assert(hardware.appliedCurrent == expected);
      }
      if (inhibited) {
        assert(enableLevel == HIGH && !motor.queued);
        assert(motor.stops == (stepper ? 1 : 0));
        // Future automatic enable requests cannot reopen a saved inhibit.
        assert(guardedEnablePin(currentBoard.enablePin, LOW) == HIGH);
      } else {
        // A healthy return-to-zero move is not discarded at the boundary.
        assert(enableLevel == LOW && motor.queued && motor.stops == 0);
      }
    }
  }
}
'''
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "homing_safety.cpp"
            exe = Path(directory) / "homing_safety.exe"
            cpp.write_text(harness.replace("/* FIRMWARE */", production), encoding="utf-8")
            subprocess.run([compiler, "-std=c++11",
                            "-I" + str(ROOT / "include"), str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
