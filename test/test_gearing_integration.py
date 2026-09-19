"""Exercise production homing, shifting and motor dispatch with fake peripherals.

Run: python -B -m unittest discover -s test -p test_gearing_integration.py
The native suite separately exercises the FTMS search and thermal policies.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestGearingIntegration(unittest.TestCase):
    def test_homing_gears_limits_and_motor_interlocks(self):
        main = (ROOT / "src/Main.cpp").read_text(encoding="utf-8")
        stepper = (ROOT / "src/Stepper.cpp").read_text(encoding="utf-8")
        gearing = (ROOT / "src/VirtualGearing.cpp").read_text(encoding="utf-8")
        header = (ROOT / "include/Main.h").read_text(encoding="utf-8")
        parameters = (ROOT / "src/SmartSpin_parameters.cpp").read_text(encoding="utf-8")
        production = "\n".join([
            function(parameters, "void userParameters::setDefaults("),
            *[function(gearing, signature) for signature in (
                "void SS2K::resetStartingGear(", "bool SS2K::localGearingSelected(",
                "int32_t SS2K::gearTargetPosition(", "int32_t SS2K::simulationTargetPosition(")],
            function(main, "void SS2K::FTMSModeShiftModifier("),
            *[function(stepper, signature) for signature in (
                "void SS2K::moveStepper(", "void SS2K::_findFTMSHome(", "void SS2K::goHome(")],
        ])
        harness = r'''
#include <cassert>
#include <cmath>
#include <cstring>
#include "SmartSpin_parameters.h"
#include "BLE_Definitions.h"
#define ARDUINO_ISR_ATTR
#define SS2K_LOG(...) ((void)0)
using std::max;
enum ButtonState { RELEASED, PRESSED };
/* CONTROLLER */;
SS2K controller;
SS2K* ss2k = &controller;
userParameters config;
userParameters* userConfig = &config;
RuntimeParameters runtime{};
RuntimeParameters* rtConfig = &runtime;
bool safetyReady = true, searchSucceeds = true, abortSearch = false;
int saved = 0, pauses = 0, lastHomingSgThreshold = 10;
void delay(int) {}
struct HomingSafetyPause {
  HomingSafetyPause() { ++pauses; }
  ~HomingSafetyPause() { --pauses; }
};
struct Motor {
  int32_t pos = 0;
  int commands = 0;
  int32_t getCurrentPosition() { return pos; }
  void setCurrentPosition(int32_t value) { pos = value; }
  bool isRunning() { return false; }
  void forceStop() {}
  void stopMove() {}
  int moveTo(int32_t value) { ++commands; pos = value; return 0; }
  void move(int32_t delta, bool) { moveTo(pos + delta); }
  void enableOutputs() {}
  void setAutoEnable(bool) {}
  void setDirectionPin(int, bool) {}
} motor;
Motor* stepper = &motor;
struct Board { bool homingSupported = true; int dirPin = 1; } currentBoard;
struct Remote {
  bool connected = false;
  bool isConnected() { return connected; }
  void sendShiftUp() {}
  void sendShiftDown() {}
} zwiftService, openBikeControlService;
struct BLE_ss2kCustomCharacteristic { static void notify(int) {} };
constexpr int BLE_shifterPosition = 6;
constexpr int LOG_INTERVAL = 1000;
struct Client {
  int32_t forwardedTarget = 0;
  void FTMSControlPointWrite(const uint8_t*, int) { forwardedTarget = ss2k->simulationTargetPosition(); }
} spinBLEClient;
struct Fitness {
  int status = 0;
  void spinDown(int value) { status = value; }
} fitnessMachineService;
struct Erg { void resetTableConfidence() {} } erg;
Erg* ergMode = &erg;
struct Table { void reset() { rtConfig->setHomed(false); userConfig->setHMax(INT32_MIN); } } table;
Table* powerTable = &table;
void userParameters::saveToLittleFS() { ++saved; }
void SS2K::setLEDEnabled(bool) {}
void SS2K::setupTMCStepperDriver(bool) {}
void SS2K::updateStepperPower(int) {}
void SS2K::updateStepperSpeed(int) {}
bool SS2K::stepperSafetyReady() { return safetyReady; }
void SS2K::_resistanceMove() {}
bool SS2K::_findEndStop(bool upper) { motor.pos = upper ? 20000 : -1000; return true; }
// Isolate orchestration from the independently tested sensor search.
namespace FtmsHoming {
enum class Failure { Timeout };
const char* failureName(Failure) { return "timeout"; }
bool supportsRange(int low, int high) { return low == 0 && high == 100; }
template<class IO> struct Search {
  IO& io;
  explicit Search(IO& value) : io(value) {}
  bool endpoint(bool upper, int32_t& result) {
    assert(pauses == 1);
    assert(!io.cancelled()); // Pending shifts before goHome must not abort it.
    if (abortSearch) rtConfig->setShifterPosition(rtConfig->getShifterPosition() + 1);
    if (!searchSucceeds || abortSearch) return false;
    result = upper ? 20000 : 0;
    motor.pos = upper ? 19500 : 500;
    return true;
  }
  Failure failure() { return Failure::Timeout; }
};
}
/* FIRMWARE */
void reset(bool ftms = false) {
  controller = SS2K();
  runtime = RuntimeParameters{};
  config.setDefaults();
  config.setShiftStep(100);
  config.setHMax(20000);
  config.setConnectedPowerMeter(ftms ? "Grupetto" : NONE);
  runtime.resistance.setValue(50, !ftms);
  runtime.resistance.setMin(0);
  runtime.resistance.setMax(100);
  runtime.setMinResistance(0);
  runtime.setMaxResistance(100);
  runtime.setMinStep(0);
  runtime.setMaxStep(20000);
  motor = Motor{};
  stepper = &motor;
  safetyReady = searchSucceeds = true;
  abortSearch = false;
  zwiftService.connected = openBikeControlService.connected = false;
  saved = pauses = 0;
  controller.resetStartingGear();
}
void assertGear(int gear) {
  assert(runtime.getShifterPosition() == gear);
  assert(controller.getLastShifterPosition() == gear);
}
int main() {
  const uint16_t ratios[] = {1000,1100,1200,1300,1400,1500,1600,1700,1800,1900,2000,2100};
  for (bool ftms : {false, true}) {
    for (bool bounded : {false, true}) {
      reset(ftms);
      if (bounded) config.setGearRatios(ratios, 12);
      // A shift pending before homing is not a cancellation.
      runtime.setShifterPosition(3);
      controller.goHome(true);
      assert(runtime.getHomed() && !controller.ftmsHomingFailed && pauses == 0);
      assert(saved == 1);
      assertGear(bounded ? 4 : 8);
      controller.FTMSModeShiftModifier();
      controller.moveStepper();
      assertGear(bounded ? 4 : 8);
      assert(motor.pos == (bounded ? 300 : 800));
    }
  }
  // Failed/aborted FTMS homing retains the gear and latches normal motor control.
  for (bool abort : {false, true}) {
    reset(true);
    runtime.setShifterPosition(5);
    abortSearch = abort;
    searchSucceeds = abort;
    controller.goHome(true);
    assert(!runtime.getHomed() && controller.ftmsHomingFailed && pauses == 0);
    assert(runtime.getShifterPosition() == (abort ? 6 : 5) && saved == 0);
    int commands = motor.commands;
    controller.moveStepper();
    assert(motor.commands == commands);
    searchSucceeds = true;
    abortSearch = false;
    controller.goHome(true);
    assert(runtime.getHomed() && !controller.ftmsHomingFailed);
    assertGear(8);
  }
  // Invalid saved mechanical bounds must not select the homed start gear.
  reset();
  config.setHMax(-10);
  runtime.setShifterPosition(3);
  controller.goHome(false);
  assert(!runtime.getHomed() && pauses == 0);
  assertGear(3);

  // Reject an Unlimited shift at the travel limit, immediately enter ERG,
  // then return to SIM without another local-mode loop in between.
  reset();
  runtime.setHomed(true);
  runtime.setMaxStep(800);
  controller.resetStartingGear();
  controller.FTMSModeShiftModifier();
  runtime.setShifterPosition(9);
  controller.FTMSModeShiftModifier();
  assertGear(8);
  runtime.setFTMSMode(FitnessMachineControlPointProcedure::SetTargetPower);
  controller.FTMSModeShiftModifier();
  runtime.setFTMSMode(FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters);
  controller.FTMSModeShiftModifier();
  assertGear(8);
  runtime.setShifterPosition(7);
  controller.FTMSModeShiftModifier();
  assertGear(7);
  assert(spinBLEClient.forwardedTarget == 700);

  // Ratio targets still pass through final motor clamps and the thermal gate.
  config.setGearRatios(ratios, 12);
  runtime.setShifterPosition(12);
  controller.FTMSModeShiftModifier();
  assert(controller.simulationTargetPosition() == 799);
  safetyReady = false;
  int commands = motor.commands;
  controller.moveStepper();
  assert(motor.commands == commands);
  safetyReady = true;
  controller.moveStepper();
  assert(motor.pos == 799);
}
'''
        harness = harness.replace("/* CONTROLLER */", function(header, "class SS2K {"))
        harness = harness.replace("/* FIRMWARE */", production)
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "gearing.cpp"
            exe = Path(directory) / "gearing.exe"
            cpp.write_text(harness, encoding="utf-8")
            subprocess.run([
                compiler, "-std=c++17", "-DPLATFORMIO_ENV_NATIVE",
                "-I" + str(ROOT / "include"), "-I" + str(ROOT / "lib/SS2K/include"),
                "-I" + str(ROOT / "lib/ArduinoCompat/include"), str(cpp), "-o", str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
