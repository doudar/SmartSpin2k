"""Exercise the production motor adapter with a pulse-counting fake peripheral."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestStepperUnits(unittest.TestCase):
    def test_distance_timing_homing_and_safety_keep_legacy_units(self):
        harness = r'''
#include <cassert>
#include <cmath>
#include <initializer_list>
#include "LogicalStepper.h"
#define SS2K_LOG(...) ((void)0)
enum class MotionResult : int8_t { Ok = 0, AccelerationUndefined = -3 };
struct PulseMotor {
  int32_t position = 0, target = 0, acceleration = 0;
  uint32_t speed = 0;
  int speedWrites = 0, moves = 0, stops = 0;
  bool blocking = false, running = false, automatic = false, enabled = false;
  uint16_t disableDelay = 0;
  uint8_t directionPin = 0, enablePin = 0;
  bool forward = true;
  MotionResult result = MotionResult::Ok;
  int32_t getCurrentPosition() { return position; }
  void setCurrentPosition(int32_t value) { position = target = value; running = false; }
  MotionResult moveTo(int32_t value, bool wait) {
    target = value; blocking = wait; running = !wait; ++moves;
    if (wait) position = target;
    return result;
  }
  MotionResult move(int32_t value, bool wait) { return moveTo((running ? target : position) + value, wait); }
  int8_t setSpeedInHz(uint32_t value) { speed = value; ++speedWrites; return 0; }
  uint32_t getSpeedInMilliHz() { return speed * 1000; }
  int8_t setAcceleration(int32_t value) { acceleration = value; return 0; }
  void forceStopAtCurrentPosition() { target = position; running = false; ++stops; }
  bool isRunning() { return running; }
  void stopMove() { running = false; }
  void forceStop() { running = false; }
  MotionResult runForward() { running = forward = true; return result; }
  MotionResult runBackward() { running = true; forward = false; return result; }
  const char* driverTypeString() const { return "MCPWM/PCNT"; }
  bool enableOutputs() { enabled = true; return true; }
  bool disableOutputs() { enabled = false; return true; }
  void setAutoEnable(bool value) { automatic = value; }
  void setDelayToDisable(uint16_t value) { disableDelay = value; }
  void setDirectionPin(uint8_t pin, bool value) { directionPin = pin; forward = value; }
  void setEnablePin(uint8_t pin) { enablePin = pin; }
} pulses;
using MotorStepper = LogicalStepper<PulseMotor>;
MotorStepper adapter;
MotorStepper* stepper = adapter.attach(&pulses);
struct Config { int getStepperSpeed() { return 3500; } } config;
Config* userConfig = &config;
struct SS2K { void updateStepperSpeed(int speed = 0); } controller;
/* SPEED_UPDATE */
int main() {
  static_assert(MotorStepper::microsteps == 16, "Driver must use sixteenth steps");
  static_assert(sizeof(adapter) == sizeof(void*), "Adapter must not grow runtime state");
  assert(adapter.attach(nullptr) == nullptr);
  assert(adapter.attach(&pulses) == &adapter);

  // Defaults retain the same full-step velocity and acceleration, including
  // the ramp duration and distance. No saved setting needs to be multiplied.
  controller.updateStepperSpeed();
  assert(pulses.speed == 14000);
  assert(stepper->getSpeedInMilliHz() == 3500000);
  assert(stepper->setAcceleration(3000) == 0);
  assert(pulses.acceleration == 12000);
  assert(pulses.speed / 16.0 == 3500 / 4.0);
  assert(pulses.acceleration / 16.0 == 3000 / 4.0);
  assert(std::abs(pulses.speed / double(pulses.acceleration) - 3500.0 / 3000) < 1e-12);
  controller.updateStepperSpeed();
  controller.updateStepperSpeed(3504); // Existing logical speed tolerance.
  assert(pulses.speedWrites == 1);
  controller.updateStepperSpeed(3505);
  assert(pulses.speed == 14020 && pulses.speedWrites == 2);
  controller.updateStepperSpeed(350); // Edge slowdown/homing uses the same conversion.
  assert(pulses.speed == 1400);

  for (int32_t target : {-12345, -1200, -1, 0, 1, 1200, 12345}) {
    stepper->setCurrentPosition(0);
    assert(stepper->moveTo(target, true) == 0);
    assert(pulses.position / 16.0 == target / 4.0);
    assert(stepper->getCurrentPosition() == target);
    assert(pulses.blocking);
    // Homing backoffs and relative moves retain signed physical distance.
    assert(stepper->move(-1200, true) == 0);
    assert(stepper->getCurrentPosition() == target - 1200);
    assert(stepper->move(1200, true) == 0);
    assert(stepper->getCurrentPosition() == target);
  }
  stepper->setCurrentPosition(-4321); // Startup/sync/calibration coordinate reset.
  assert(pulses.position == -17284);
  stepper->moveTo(5000);
  assert(!pulses.blocking && pulses.target == 20000);
  stepper->move(250);
  assert(pulses.target == 21000 && pulses.position == -17284);
  pulses.result = MotionResult::AccelerationUndefined;
  assert(stepper->moveTo(0) == -3 && stepper->move(10) == -3);
  assert(stepper->runForward() == -3 && stepper->runBackward() == -3);
  pulses.result = MotionResult::Ok;

  // An interlock at a partial logical step must not change pulse coordinates.
  for (int32_t position : {-7, -3, -1, 1, 3, 7}) {
    pulses.position = position;
    assert(stepper->getCurrentPosition() == position / 4);
    stepper->forceStopAtCurrentPosition();
    stepper->forceStopAtCurrentPosition();
    assert(pulses.position == position && pulses.target == position);
  }

  // Reject unrepresentable conversions without overflow or queue mutation.
  const int moves = pulses.moves;
  assert(stepper->moveTo(INT32_MAX) == MotorStepper::rangeError);
  assert(stepper->moveTo(INT32_MIN) == MotorStepper::rangeError);
  assert(stepper->move(INT32_MIN) == MotorStepper::rangeError);
  assert(!stepper->setCurrentPosition(INT32_MAX));
  assert(pulses.moves == moves && pulses.position == 7);
  assert(stepper->setCurrentPosition(MotorStepper::maxPosition));
  assert(stepper->getCurrentPosition() == MotorStepper::maxPosition);
  assert(stepper->setCurrentPosition(MotorStepper::minPosition));
  assert(stepper->getCurrentPosition() == MotorStepper::minPosition);
  const auto speed = pulses.speed;
  assert(stepper->setSpeedInHz(UINT32_MAX) == MotorStepper::rangeError);
  assert(pulses.speed == speed);
  assert(stepper->setAcceleration(INT32_MAX) == MotorStepper::rangeError);
  assert(stepper->setAcceleration(0) == MotorStepper::rangeError);
  assert(pulses.acceleration == 12000);

  // Dimensionless controls and time delays pass through without scaling.
  stepper->setDirectionPin(12, false);
  stepper->setEnablePin(13);
  stepper->setAutoEnable(true);
  stepper->setDelayToDisable(65535);
  assert(pulses.directionPin == 12 && !pulses.forward && pulses.enablePin == 13);
  assert(pulses.automatic && pulses.disableDelay == 65535);
  assert(stepper->enableOutputs() && pulses.enabled);
  assert(stepper->disableOutputs() && !pulses.enabled);
  assert(stepper->runForward() == 0 && pulses.forward && stepper->isRunning());
  stepper->stopMove();
  assert(!stepper->isRunning());
  assert(stepper->runBackward() == 0 && !pulses.forward && stepper->isRunning());
  stepper->forceStop();
  assert(!stepper->isRunning());
}
'''
        source = (ROOT / "src/Stepper.cpp").read_text(encoding="utf-8")
        harness = harness.replace("/* SPEED_UPDATE */", function(source, "void SS2K::updateStepperSpeed("))
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "stepper_units.cpp"
            exe = Path(directory) / "stepper_units.exe"
            cpp.write_text(harness, encoding="utf-8")
            subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                            "-I" + str(ROOT / "include"), str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
