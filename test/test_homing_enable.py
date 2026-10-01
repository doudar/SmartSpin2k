"""Homing must refresh automatic enable even after cadence selected manual mode."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestHomingEnable(unittest.TestCase):
    def test_long_homing_and_retry_keep_physical_motion(self):
        source = (ROOT / "src/Stepper.cpp").read_text(encoding="utf-8")
        harness = r'''
#include <cassert>
#include <cstdint>
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
struct DriverLock {};
bool homingActive = false, motorInhibited = false, driverConfigured = true;
bool hot = false;
int requestedCurrent = 0, restored = 0;
uint32_t millis() { return 0; }
struct Protection { void update(bool, bool, bool, uint32_t) {} } tmcProtection;
struct Config { int getStepperPower() { return 1000; } } config;
Config* userConfig = &config;
int hardware;
int* driver = &hardware;
bool applyDriverCurrent(bool) { ++restored; return true; }
// FastAccelStepper keeps the old idle countdown when setAutoEnable(false)
// is called. Only automatic moves renew it; manual enable does not clear it.
struct Motor {
  bool automatic = false, enabled = true;
  int countdown = 22, counter = 1108, physical = 1108;
  void setAutoEnable(bool value) { automatic = value; }
  void idle(int seconds) {
    if (countdown > 0) {
      countdown -= seconds;
      if (countdown <= 0) { countdown = 0; enabled = false; }
    }
  }
  void move(int steps) {
    if (automatic) { enabled = true; countdown = 65; }
    counter += steps;
    if (enabled) physical += steps;
  }
} motor;
Motor* stepper = &motor;
void applyMotorInterlock(bool resume) {
  assert(resume);
  homingActive = false;
  motorInhibited = hot;
  if (hot && stepper) stepper->enabled = false;
}
/* PAUSE */;
int main() {
  // Reproduce the old failure: positive software steps with no brake movement.
  motor.idle(23);
  motor.move(3035);
  assert(motor.counter == 4143 && motor.physical == 1108);
  for (bool previouslyDisabled : {false, true}) {
    motor = Motor();
    if (previouslyDisabled) motor.idle(23);
    {
      HomingSafetyPause pause;
      assert(homingActive && motor.automatic);
      // Many small probes/dwells, totaling much more than the old countdown.
      for (int i = 0; i < 30; ++i) { motor.move(-150); motor.idle(4); }
      assert(motor.counter == motor.physical);
      // Even a full automatic timeout must recover on the next move.
      motor.idle(70);
      assert(!motor.enabled);
      motor.move(3035);
      assert(motor.enabled && motor.counter == motor.physical);
    }
    assert(!homingActive && !motorInhibited);
  }
  hot = true;
  { HomingSafetyPause pause; motor.move(150); }
  assert(!homingActive && motorInhibited && !motor.enabled);
  stepper = nullptr;
  { HomingSafetyPause pause; }
  assert(!homingActive);
}
'''
        harness = "#include <initializer_list>\n" + harness.replace("/* PAUSE */", function(source, "class HomingSafetyPause {"))
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "enable.cpp"
            exe = Path(directory) / "enable.exe"
            cpp.write_text(harness, encoding="utf-8")
            subprocess.run([compiler, "-std=c++11", str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
