"""Exercise production homing, shifting and motor dispatch with fake peripherals.

Run: python -B -m unittest discover -s test -p test_gearing_integration.py
The native suite separately exercises the FTMS search and thermal policies.
"""

from pathlib import Path
import json
import math
import re
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
                "void SS2K::_resistanceMove(", "void SS2K::moveStepper(", "void SS2K::syncFtmsPosition(", "void SS2K::_findFTMSHome(", "void SS2K::goHome(")],
        ])
        harness = r'''
#include <cassert>
#include <cmath>
#include <cstring>
#include <Arduino.h>
uint32_t clockMs = 1;
unsigned long testMillis() { return clockMs; }
#define millis testMillis
#include "SmartSpin_parameters.h"
#include "BLE_Definitions.h"
#include "FtmsCalibration.h"
#include "ResistanceControl.h"
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
bool metadataPresent = false, saveSucceeds = true;
bool changeSourceDuringMap = false;
int fullSearches = 0, recoveries = 0, mapSearches = 0;
int saved = 0, pauses = 0, lastHomingSgThreshold = 10;
bool homingActive = false;
struct DriverLock { explicit DriverLock(bool) {} bool locked() { return true; } };
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
struct Server { int spinDownFlag = 0; } spinBLEServer;
struct Fitness {
  int status = 0;
  void spinDown(int value) { status = value; }
} fitnessMachineService;
struct Erg { void resetTableConfidence() {} bool isTableSeeking() { return false; } } erg;
Erg* ergMode = &erg;
struct Table {
  FtmsCalibration::Map ftmsCalibration;
  bool ftmsPositionUncertain = false;
  uint32_t positionEpoch = 0;
  bool _hasBeenLoadedThisSession = false;
  int resets = 0, saves = 0;
  bool loadFtmsCalibration() { return metadataPresent; }
  bool _manageSaveState() { _hasBeenLoadedThisSession = true; return true; }
  bool _save() { ++saves; return saveSucceeds; }
  void reset() { ++resets; rtConfig->setHomed(false); userConfig->setHMax(INT32_MIN); }
} table;
Table* powerTable = &table;
void userParameters::saveToLittleFS() { ++saved; }
void SS2K::setLEDEnabled(bool) {}
void SS2K::setupTMCStepperDriver(bool) {}
void SS2K::updateStepperPower(int) {}
void SS2K::updateStepperSpeed(int) {}
bool SS2K::stepperSafetyReady() { return safetyReady; }
bool SS2K::_findEndStop(bool upper) { motor.pos = upper ? 20000 : -1000; return true; }
// Isolate orchestration from the independently tested sensor search.
namespace FtmsHoming {
enum class Failure { Timeout };
const char* failureName(Failure) { return "timeout"; }
bool supportsRange(int low, int high) { return low == 0 && high == 100; }
template<class IO> struct Search {
  IO& io;
  explicit Search(IO& value, int, float) : io(value) {}
  bool endpoint(bool upper, int32_t& result) {
    ++fullSearches;
    assert(pauses == 1);
    assert(!io.cancelled()); // Pending shifts before goHome must not abort it.
    if (abortSearch) rtConfig->setShifterPosition(rtConfig->getShifterPosition() + 1);
    if (!searchSucceeds || abortSearch) return false;
    result = upper ? 20000 : 0;
    motor.pos = upper ? 19500 : 500;
    return true;
  }
  bool reference(int level, int32_t& result, uint8_t& observed, int) {
    ++mapSearches;
    observed = 2 * level;
    result = level * 200;
    motor.pos = result;
    if (changeSourceDuringMap) config.setConnectedPowerMeter("Changed bike");
    return searchSucceeds && !io.cancelled();
  }
  bool recover(const FtmsCalibration::Map& map, int32_t& result) {
    ++recoveries;
    int32_t center, uncertainty;
    if (!map.estimateHalf(2 * rtConfig->resistance.getValue(), center, uncertainty)) return false;
    result = motor.pos - center;
    return searchSucceeds;
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
  fullSearches = recoveries = mapSearches = 0;
  metadataPresent = false;
  saveSucceeds = true;
  changeSourceDuringMap = false;
  table = Table{};
  controller.resetStartingGear();
}
void assertGear(int gear) {
  assert(runtime.getShifterPosition() == gear);
  assert(controller.getLastShifterPosition() == gear);
}
int main() {
  // Actual FTMS spindown mode, for every shipped profile and both home paths.
  /* SHIPPED PROFILES */
  const uint16_t* profiles[] = {nullptr, road, mtb, gravel};
  const int counts[] = {0,24,12,13};
  const int starts[] = {8,8,4,4};
  for (int profile = 0; profile < 4; ++profile) {
    for (bool ftms : {false, true}) {
      for (bool full : {false, true}) {
        reset(ftms);
        config.setShiftStep(1200);
        assert(config.setGearRatios(profiles[profile], counts[profile]));
        runtime.setFTMSMode(FitnessMachineControlPointProcedure::SpinDownControl);
        runtime.setTargetIncline(7484); // Old recovered position must not become terrain.
        controller.goHome(full);
        assertGear(starts[profile]);
        assert(runtime.getFTMSMode() == FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters);
        assert(runtime.getTargetIncline() == 0);
        const int expected = config.getGearRatios().offsetSteps(starts[profile], 1200);
        controller.FTMSModeShiftModifier();
        controller.moveStepper();
        assert(motor.pos == expected && motor.pos < 10000);
      }
    }
  }
  // Run the real resistance controller repeatedly at target: no downward creep.
  reset(true);
  runtime.setHomed(true);
  runtime.setFTMSMode(FitnessMachineControlPointProcedure::SetTargetResistanceLevel);
  runtime.resistance.setTarget(50);
  motor.pos = 9000;
  for (int i = 0; i < 100; ++i) {
    clockMs += 10;
    runtime.resistance.setValue(50, false);
    controller.moveStepper();
    assert(motor.pos == 9000 && runtime.getTargetIncline() == 9000);
  }
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
  // Uneven ratio gaps must use the selected gear's actual ratio offset,
  // not one third of travel or the recovered FTMS sample position.
  const uint16_t uneven[] = {1000,1100,1400,1500,1600,1800,1900,2000,2200,2300,2400,2500,2700};
  for (bool full : {false, true}) {
    reset(true);
    config.setShiftStep(1200);
    config.setGearRatios(uneven, 13);
    controller.goHome(true);
    metadataPresent = true;
    runtime.setShifterPosition(9);
    int commandsBeforeHome = motor.commands;
    controller.goHome(full);
    assertGear(4);
    assert(controller.getTargetPosition() == 6000); // Ratio 1.5 minus 1.0, median gap 0.1.
    assert(motor.commands == commandsBeforeHome); // Dispatch waits for restored motor policy.
    controller.FTMSModeShiftModifier();
    safetyReady = false;
    controller.moveStepper();
    assert(motor.commands == commandsBeforeHome);
    safetyReady = true;
    controller.moveStepper();
    assert(motor.pos == 6000);
  }
  // A starting gear beyond calibrated travel still uses the normal clamp.
  reset(true);
  config.setShiftStep(3000);
  controller.goHome(true);
  assertGear(8);
  assert(controller.getTargetPosition() == 19999);
  controller.FTMSModeShiftModifier();
  controller.moveStepper();
  assert(motor.pos == 19999);

  // Legacy startup calibrates both ends and adds three observations, without
  // resetting the watts table. New metadata bypasses the endpoint searches.
  reset(true);
  controller.goHome(false);
  assert(runtime.getHomed() && fullSearches == 2 && mapSearches == 3 && recoveries == 0);
  assert(table.resets == 0 && table.saves == 1);
  assert(table.ftmsCalibration.position[1] == 10000);
  metadataPresent = true;
  fullSearches = mapSearches = 0;
  controller.goHome(false);
  assert(runtime.getHomed() && fullSearches == 0 && mapSearches == 0 && recoveries == 1);
  assert(motor.pos == 10000 && controller.getTargetPosition() == 800);
  controller.moveStepper();
  assert(motor.pos == 800); // Move directly to eight shifts above zero after recovery.

  reset(true);
  saveSucceeds = false;
  controller.goHome(false);
  assert(!runtime.getHomed() && controller.ftmsHomingFailed && saved == 0);

  reset(true);
  changeSourceDuringMap = true;
  controller.goHome(false);
  assert(!runtime.getHomed() && controller.ftmsHomingFailed && saved == 0 && table.saves == 0);

  // Synchronization changes coordinates, never sends a motor command; the
  // next simulation/ERG update must retain the corrected stationary position.
  for (bool simulation : {true, false}) {
    reset(true);
    config.setShiftStep(1000);
    controller.goHome(false);
    controller.FTMSModeShiftModifier();
    controller.moveStepper();
    if (!simulation) {
      runtime.setFTMSMode(FitnessMachineControlPointProcedure::SetTargetPower);
      runtime.setTargetIncline(motor.pos);
    }
    for (auto& point : table.ftmsCalibration.position) point += 500;
    controller.syncFtmsPosition(); // No feedback yet for this coordinate.
    int commands = motor.commands;
    int32_t oldPosition = motor.pos;
    uint32_t epoch = table.positionEpoch;
    for (int second = 0; second < 70; ++second) {
      clockMs += 1000;
      runtime.resistance.setValue(40, false);
      controller.syncFtmsPosition();
    }
    assert(motor.commands == commands && motor.pos == oldPosition + 500);
    assert(controller.getTargetPosition() == motor.pos && table.positionEpoch == epoch + 1);
    controller.moveStepper();
    assert(motor.pos == oldPosition + 500);
    // A pending target change blocks synchronization even before motor motion.
    runtime.setTargetIncline(runtime.getTargetIncline() + 50);
    oldPosition = motor.pos;
    for (int second = 0; second < 70; ++second) {
      clockMs += 1000;
      runtime.resistance.setValue(40, false);
      controller.syncFtmsPosition();
    }
    assert(motor.pos == oldPosition);
    // Rehoming removes the drift offset and restores the absolute start gear.
    runtime.setFTMSMode(0);
    metadataPresent = true;
    controller.goHome(false);
    controller.FTMSModeShiftModifier();
    controller.moveStepper();
    assertGear(8);
    assert(motor.pos == 8000);
  }
  // Manual removal/reseating must fix the whole offset with zero motor commands.
  // Exercise actual orchestration and the next local-gear update as well.
  reset(true);
  clockMs += FtmsCalibration::INTERVAL_MS;
  config.setShiftStep(1440);
  controller.goHome(false);
  config.setHMax(24413);
  runtime.setMaxStep(24413);
  table.ftmsCalibration.maximum = 24413;
  for (int i = 0; i < FtmsCalibration::COUNT; ++i) table.ftmsCalibration.position[i] = table.ftmsCalibration.level2[i] * 255 / 2 - 720;
  metadataPresent = true;
  runtime.resistance.setValue(48, false);
  controller.goHome(false);
  assert(motor.pos == 11520);
  controller.FTMSModeShiftModifier();
  controller.moveStepper();
  assert(motor.pos == 11520); // Eight shifts of 1440, regardless of recovered position.
  for (int second = 0; second < 25; ++second) {
    clockMs += 1000;
    runtime.resistance.setValue(48, false);
    controller.syncFtmsPosition();
  }
  runtime.setShifterPosition(6);
  controller.FTMSModeShiftModifier();
  controller.moveStepper();
  assert(motor.pos == 8640);
  for (int second = 0; second < 28; ++second) {
    clockMs += 1000;
    runtime.resistance.setValue(36 + second % 2, false);
    controller.syncFtmsPosition();
  }
  int commandsBeforeReseat = motor.commands;
  for (int second = 0; second < 45; ++second) {
    clockMs += 1000;
    runtime.resistance.setValue(32, false);
    controller.syncFtmsPosition();
    if (second == 5) assert(table.ftmsPositionUncertain);
  }
  assert(motor.pos == 7440 && controller.getTargetPosition() == 7440);
  assert(motor.commands == commandsBeforeReseat && !table.ftmsPositionUncertain);
  assertGear(6);
  controller.moveStepper();
  assert(motor.pos == 7440);

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
        preset_pattern = r"chainrings: (\[[^\]]*\]),\s*cassette: (\[[^\]]*\])"
        presets = re.findall(preset_pattern, (ROOT / "data/settings.html").read_text(encoding="utf-8"))
        self.assertEqual(presets, re.findall(preset_pattern, (ROOT / "data_s3/settings.html").read_text(encoding="utf-8")))
        self.assertEqual(len(presets), 4)
        declarations = []
        for name, (fronts, rears) in zip(("road", "mtb", "gravel"), presets[1:]):
            ratios = sorted(math.floor(f / r * 1000 + 0.5) for f in json.loads(fronts) for r in json.loads(rears))
            declarations.append("const uint16_t " + name + "[] = {" + ",".join(map(str, ratios)) + "};")
        harness = harness.replace("/* SHIPPED PROFILES */", "\n".join(declarations))
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
