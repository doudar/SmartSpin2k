"""Test firmware recovery with fake UART responses. Requires g++.

Run: python -B -m unittest discover -s test -p test_tmc_recovery.py
"""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class TestTmcRecovery(unittest.TestCase):
    def test_runtime_polling_does_not_disable_on_read_errors(self):
        # Exercise the real maintenance method with fake hardware, not a copy
        # of its decisions. Temperature policies are the production header.
        source = (ROOT / "src/Stepper.cpp").read_text(encoding="utf-8")
        harness = r'''
#include <assert.h>
#include "ThermalSafety.h"
#define SS2K_LOG(...) ((void)0)
struct DriverLock {};
struct Driver {
  bool CRCerror = false, fail = false;
  uint32_t status = 0xc0070000;
  uint8_t reset = 0;
  int reads = 0;
  uint32_t DRV_STATUS() { ++reads; CRCerror = fail; return status; }
  uint8_t GSTAT() { ++reads; CRCerror = fail; return reset; }
} hardware;
Driver* driver = &hardware;
bool homingActive = false, driverConfigured = true, s3MotorInhibited = false;
bool motorInhibited = false;
int s3CurrentLimit = 100, writes = 0, setups = 0, appliedPercent = 100;
uint32_t clockMs = 0;
uint32_t millis() { return clockMs; }
ThermalSafety::TmcProtection tmcProtection;
void applyMotorInterlock() {
  motorInhibited = !driverConfigured || s3MotorInhibited || tmcProtection.disabled();
}
bool applyDriverCurrent(bool) {
  ++writes;
  appliedPercent = ThermalSafety::limitedCurrent(100, tmcProtection.percent(), s3CurrentLimit);
  return true;
}
struct SS2K {
  void updateDriverSafety(int, bool);
  bool stepperSafetyReady() { return !motorInhibited; }
  void setupTMCStepperDriver(bool) { ++setups; }
};
/* FIRMWARE */
int main() {
  SS2K controller;
  controller.updateDriverSafety(100, false);
  assert(writes == 0 && setups == 0 && !motorInhibited);
  hardware.fail = true;
  for (int i = 0; i < 100; ++i) {
    clockMs += 10000;
    controller.updateDriverSafety(100, false);
    assert(driverConfigured && !motorInhibited && writes == 0 && setups == 0);
  }
  // S3 limits still apply when the TMC read fails.
  controller.updateDriverSafety(75, false);
  assert(appliedPercent == 75 && !motorInhibited);
  controller.updateDriverSafety(50, true);
  assert(motorInhibited && appliedPercent == 50);
  controller.updateDriverSafety(100, false);
  assert(!motorInhibited && appliedPercent == 100);
  hardware.fail = false;
  hardware.status |= 0x100;  // T120.
  controller.updateDriverSafety(100, false);
  assert(appliedPercent == 50 && !motorInhibited);
  clockMs += 29999;
  controller.updateDriverSafety(100, false);
  assert(!motorInhibited);
  ++clockMs;
  controller.updateDriverSafety(100, false);
  assert(motorInhibited);
  hardware.fail = true;
  hardware.status = 0xc0070000;
  controller.updateDriverSafety(100, false);
  assert(motorInhibited);  // Invalid data cannot clear an actual thermal stop.
  hardware.fail = false;
  controller.updateDriverSafety(100, false);
  assert(!motorInhibited && appliedPercent == 100);
  hardware.status |= 2;  // OT stops immediately.
  controller.updateDriverSafety(100, false);
  assert(motorInhibited);
  hardware.status = 0xc0070000;
  hardware.reset = 1;
  controller.updateDriverSafety(100, false);
  assert(setups == 1);  // Actual chip resets still request setup.
  driverConfigured = false;
  controller.updateDriverSafety(100, false);
  assert(setups == 2);  // Failed startup still retries.
  homingActive = true;
  int before = hardware.reads;
  controller.updateDriverSafety(50, true);
  assert(hardware.reads == before && setups == 2);
}
'''
        functions = function(source, "void updateTmcTemperature(") + "\n" + function(source, "void SS2K::updateDriverSafety(")
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "poll.cpp"
            exe = Path(directory) / "poll.exe"
            cpp.write_text(harness.replace("/* FIRMWARE */", functions), encoding="utf-8")
            subprocess.run([compiler, "-std=c++11", "-I" + str(ROOT / "include"), str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
