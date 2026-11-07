"""Exercise production homing, shifting and motor dispatch with fake peripherals.

Run: python -B -m unittest discover -s test -p test_gearing_integration.py
The native suite separately exercises the FTMS search and thermal policies.
"""

from pathlib import Path
import json
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
        table_source = (ROOT / "src/Power_Table.cpp").read_text(encoding="utf-8")
        fitness_source = (ROOT / "src/BLE_Fitness_Machine_Service.cpp").read_text(encoding="utf-8")
        collector = (ROOT / "src/SensorCollector.cpp").read_text(encoding="utf-8")
        production = "\n".join([
            function(parameters, "void userParameters::setDefaults("),
            *[function(gearing, signature) for signature in (
                "void SS2K::resetStartingGear(", "VirtualGearing::Gears SS2K::activeGearRatios(", "bool SS2K::usePowerTableForPower(", "bool SS2K::localGearingSelected(",
                "int32_t SS2K::gearTargetPosition(", "int32_t SS2K::simulationTargetPosition(")],
            function(main, "void SS2K::FTMSModeShiftModifier("),
            function(main, "bool SS2K::pelotonConnected("),
            function(collector, "void collectAndSet("),
            function(table_source, "void PowerTable::clearRuntime("),
            function(table_source, "void PowerTable::setStepperMinMax("),
            function(fitness_source, "int BLE_Fitness_Machine_Service::calculateResistanceFromPosition("),
            *[function(stepper, signature) for signature in (
                "void SS2K::useUnhomedFallback(", "void SS2K::_resistanceMove(", "void SS2K::moveStepper(", "void SS2K::syncFtmsPosition(", "void SS2K::_findFTMSHome(", "void SS2K::goHome(")],
        ])
        harness = r'''
#include <cassert>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>
#include <Arduino.h>
#include "Constants.h"
#include "sensors/PelotonData.h"
uint32_t clockMs = 1;
unsigned long testMillis() { return clockMs; }
#define millis testMillis
#include "SmartSpin_parameters.h"
#include "BLE_Definitions.h"
#include "FtmsCalibration.h"
#include "ResistanceControl.h"
#include "PowerTable_Helpers.h"
#define ARDUINO_ISR_ATTR
#define SS2K_LOG(...) ((void)0)
#define SS2K_LOGD(...) ((void)0)
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
bool metadataPresent = false, saveSucceeds = true, resetSucceeds = true;
bool changeSourceDuringMap = false;
int failedMechanicalEnd = -1;
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
  int32_t queuedTarget = 0;
  bool followTarget = true;
  int commands = 0;
  int32_t getCurrentPosition() { return pos; }
  void setCurrentPosition(int32_t value) { pos = value; }
  bool isRunning() { return false; }
  void forceStop() {}
  void stopMove() {}
  int moveTo(int32_t value) { ++commands; queuedTarget = value; if (followTarget) pos = value; return 0; }
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
} openBikeControlService;
struct BLE_ss2kCustomCharacteristic { static void notify(int) {} };
constexpr int BLE_shifterPosition = 6;
constexpr int LOG_INTERVAL = 1000;
struct Client {
  struct Device { std::string uniqueName; uint32_t lastDataUpdateTime = 0; bool isGrupetto = false; };
  Device myBLEDevices[NUM_BLE_DEVICES];
  bool connectedHRM = false, connectedCD = false, connectedPM = false, connectedSpeed = false;
  int32_t forwardedTarget = 0;
  void FTMSControlPointWrite(const uint8_t*, int) { forwardedTarget = ss2k->simulationTargetPosition(); }
} spinBLEClient;
struct Server { int spinDownFlag = 0; } spinBLEServer;
struct BLE_Fitness_Machine_Service {
  int status = 0;
  void spinDown(int value) { status = value; }
  int calculateResistanceFromPosition();
} fitnessMachineService;
struct Erg { void resetTableConfidence() {} void prepareMode() {} bool isTableSeeking() { return false; } } erg;
Erg* ergMode = &erg;
struct PowerTable {
  PTData ptData;
  PTHelpers ptHelpers;
  unsigned long lastSaveTime = 0;
  bool saveFlag = false;
  FtmsCalibration::Map ftmsCalibration;
  FtmsCalibration::Map savedCalibration;
  uint32_t positionEpoch = 0;
  bool _hasBeenLoadedThisSession = false;
  int resets = 0, saves = 0;
  bool loadFtmsCalibration() { ftmsCalibration = savedCalibration; return metadataPresent; }
  bool _manageSaveState(bool = false, bool = true) { _hasBeenLoadedThisSession = true; return true; }
  bool _save() { ++saves; if (saveSucceeds) savedCalibration = ftmsCalibration; return saveSucceeds; }
  void clearRuntime(bool allowSavedTableLoad = false);
  void setStepperMinMax();
  int32_t lookup(int watts, int cadence) { return ptHelpers.lookup(watts, cadence, ptData); }
  bool reset() { ++resets; rtConfig->setHomed(false); userConfig->setHMax(INT32_MIN); return resetSucceeds; }
} table;
using Table = PowerTable;
Table* powerTable = &table;
void userParameters::saveToLittleFS() { ++saved; }
void SS2K::setLEDEnabled(bool) {}
void SS2K::setupTMCStepperDriver(bool) {}
void SS2K::updateStepperPower(int) {}
int appliedSpeed = 0;
void SS2K::updateStepperSpeed(int speed) { appliedSpeed = speed; }
bool SS2K::stepperSafetyReady() { return safetyReady; }
bool SS2K::_findEndStop(bool upper) { motor.pos = upper ? 20000 : -1000; return failedMechanicalEnd != int(upper); }
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
    observed = level == FtmsCalibration::REFERENCE_LEVEL ? FtmsCalibration::REFERENCE_LEVEL2 : 2 * level;
    result = observed * 100;
    motor.pos = result;
    if (changeSourceDuringMap) config.setConnectedPowerMeter("Changed bike");
    return searchSucceeds && !io.cancelled();
  }
  bool recover(const FtmsCalibration::Map& map, int32_t& result) {
    ++recoveries;
    if (!map.valid()) return false;
    const int32_t crossing = motor.pos + 100;
    result = crossing - map.position[1];
    motor.pos = crossing + 7; // The last probe is not the bracket midpoint.
    return searchSucceeds;
  }
  Failure failure() { return Failure::Timeout; }
};
}
// Use the production parser with an explicit clock; fake only factory routing
// and BLE feedback so the serial -> collector -> motor path stays real.
struct BleFeedback : PelotonData {
  bool hasResistance() override { return true; }
  int getResistance() override { return 63; }
};
struct Factory {
  std::shared_ptr<PelotonData> peloton = std::make_shared<PelotonData>();
  std::shared_ptr<BleFeedback> ble = std::make_shared<BleFeedback>();
  std::shared_ptr<SensorData> getSensorData(const NimBLEUUID& uuid, const std::string&, uint8_t* data, size_t size) {
    if (uuid == PELOTON_DATA_UUID) {
      peloton->decode(data, size, clockMs);
      return peloton;
    }
    return ble;
  }
} sensorDataFactory;
/* FIRMWARE */
void wireResistance(int level) {
  const std::string digits = std::to_string(level);
  std::vector<uint8_t> packet{PELOTON_HEADER, PELOTON_RES_ID, static_cast<uint8_t>(digits.size())};
  packet.insert(packet.end(), digits.rbegin(), digits.rend());
  uint8_t checksum = 0;
  for (auto byte : packet) checksum += byte;
  packet.push_back(checksum);
  collectAndSet(PELOTON_DATA_UUID, PELOTON_DATA_UUID, "Peloton", packet.data(), packet.size());
}
void initializeStartupPosition() {
  /* STARTUP POSITION */
}
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
  openBikeControlService.connected = false;
  saved = pauses = 0;
  fullSearches = recoveries = mapSearches = 0;
  metadataPresent = false;
  saveSucceeds = true;
  resetSucceeds = true;
  changeSourceDuringMap = false;
  failedMechanicalEnd = -1;
  currentBoard.homingSupported = true;
  table = Table{};
  controller.resetStartingGear();
}
void assertGear(int gear) {
  assert(runtime.getShifterPosition() == gear);
  assert(controller.getLastShifterPosition() == gear);
}
int main() {
  // Wired resistance must survive collector filtering even with an external
  // power meter, while unrelated BLE resistance remains ignored.
  for (const char* meter : {NONE, ANY, "External power meter"}) {
    reset();
    config.setConnectedPowerMeter(meter);
    config.setStepperSpeed(3000);
    runtime.watts.setValue(210);
    runtime.cad.setValue(80);
    controller.pelotonIsConnected = true;
    controller.externalControl = true;
    for (int level : {0, 4, 5, 10, 20, 50, 80, 98, 100}) {
      wireResistance(level);
      assert(runtime.resistance.getValue() == level && !runtime.resistance.getSimulate());
      assert(controller.pelotonConnected());
      assert(runtime.getMinResistance() == MIN_PELOTON_RESISTANCE);
      assert(runtime.getMaxResistance() == MAX_PELOTON_RESISTANCE);
      assert(runtime.watts.getValue() == 210 && runtime.cad.getValue() == 80);
      for (int direction : {-1, 1}) {
        runtime.setMinStep(0);
        runtime.setMaxStep(20000);
        motor.pos = 1000;
        controller.setTargetPosition(1000 + direction * 100);
        controller.moveStepper();
        const bool towardLowerEdge = level >= MIN_PELOTON_RESISTANCE && level < MAX_PELOTON_RESISTANCE * 0.2 && direction < 0;
        const bool towardUpperEdge = level <= MAX_PELOTON_RESISTANCE && level > MAX_PELOTON_RESISTANCE * 0.8 && direction > 0;
        assert((appliedSpeed < 3000) == (towardLowerEdge || towardUpperEdge));
        if (level < MIN_PELOTON_RESISTANCE && direction < 0) assert(motor.pos == 1020);
        else if (level > MAX_PELOTON_RESISTANCE && direction > 0) assert(motor.pos == 980);
        else if ((level == MIN_PELOTON_RESISTANCE && direction < 0) || (level == MAX_PELOTON_RESISTANCE && direction > 0)) assert(motor.pos == 1000);
        else assert(motor.pos == 1000 + direction * 100);
      }
    }
    const NimBLEUUID bike = FITNESSMACHINEINDOORBIKEDATA_UUID;
    collectAndSet(bike, FITNESSMACHINESERVICE_UUID, "Other bike", nullptr, 0);
    assert(runtime.resistance.getValue() == 100);
    spinBLEClient.myBLEDevices[0].uniqueName = "Name-locked Grupetto";
    spinBLEClient.myBLEDevices[0].isGrupetto = true;
    collectAndSet(bike, FITNESSMACHINESERVICE_UUID, "Name-locked Grupetto", nullptr, 0);
    assert(runtime.resistance.getValue() == 63 && !runtime.resistance.getSimulate());
    spinBLEClient.myBLEDevices[0].isGrupetto = false;
  }
  // Serial resistance must not select FTMS homing, even with a BLE meter and
  // an advertised 0..100 resistance range. Test startup and full calibration.
  for (const char* meter : {NONE, ANY, "External power meter"}) {
    for (bool full : {false, true}) {
      reset();
      config.setConnectedPowerMeter(meter);
      controller.pelotonIsConnected = true;
      wireResistance(50);
      controller.goHome(full);
      assert(runtime.getHomed() && !controller.homingFallback);
      assert(fullSearches == 0 && mapSearches == 0 && recoveries == 0);
      assert(runtime.getMinStep() == 0 && runtime.getMaxStep() == (full ? 19900 : 20000));
      assert(runtime.resistance.getValue() == 50 && !runtime.resistance.getSimulate());
    }
  }
  // Replay a rapid multi-gear downshift with 300 ms resistance reports and a
  // speed-limited motor. The retained edge must survive feedback recovery and
  // periodic power-table limit maintenance without revisiting the low target.
  reset();
  config.setShiftStep(1000);
  config.setStepperSpeed(6000);
  runtime.setMinStep(-DEFAULT_STEPPER_TRAVEL);
  runtime.setMaxStep(DEFAULT_STEPPER_TRAVEL);
  controller.pelotonIsConnected = true;
  controller.resetStartingGear();
  controller.FTMSModeShiftModifier();
  motor.pos = 8000;
  motor.followTarget = false;
  runtime.setShifterPosition(2);
  controller.FTMSModeShiftModifier();
  int32_t heldPosition = 0;
  for (int tick = 0; tick < 1200; ++tick) {
    clockMs += 10;
    if (tick % 30 == 0) {
      const int level = std::max(0, std::min(100, (motor.pos - 6000) / 200));
      wireResistance(level);
      assert(runtime.resistance.getValue() == level); // No 1 s safety lag.
      controller.pelotonConnected();
    }
    if (tick % 50 == 0) table.setStepperMinMax();
    controller.moveStepper();
    const int travel = std::max(1, appliedSpeed / 100);
    motor.pos += std::max(-travel, std::min(travel, motor.queuedTarget - motor.pos));
    assert(motor.pos > 6000); // Physical zero in this test plant.
    if (tick == 1000) heldPosition = motor.pos;
    if (tick > 1000) assert(motor.pos == heldPosition); // No yo-yo or equality creep.
  }
  const int32_t retainedMinimum = runtime.getMinStep();
  wireResistance(0);
  table.setStepperMinMax();
  assert(runtime.getMinStep() == retainedMinimum);
  wireResistance(6);
  runtime.setShifterPosition(1);
  controller.FTMSModeShiftModifier();
  assertGear(2); // Existing shift blocker uses the retained limit.
  runtime.setShifterPosition(3);
  controller.FTMSModeShiftModifier();
  controller.moveStepper();
  assert(motor.queuedTarget > heldPosition); // One upshift escapes the held edge.
  runtime.setFTMSMode(FitnessMachineControlPointProcedure::SetTargetPower);
  runtime.setTargetIncline(2000);
  controller.moveStepper();
  assert(motor.queuedTarget >= retainedMinimum); // ERG shares the final clamp.

  // The upper edge uses the same shared clamp and must also remain held after
  // resistance returns inside the allowed range, including external commands.
  reset();
  controller.pelotonIsConnected = true;
  controller.externalControl = true;
  motor.pos = 10000;
  for (int level : {99, 98, 97, 98, 97}) {
    wireResistance(level);
    controller.pelotonConnected();
    controller.setTargetPosition(30000);
    controller.moveStepper();
    if (level == 99) heldPosition = motor.pos;
    else assert(motor.pos == heldPosition);
    table.setStepperMinMax();
  }
  // No calibration: power-on gear 8 is the existing knob position. First shifts
  // move one step either way, and mode changes retain the same gear origin.
  reset();
  config.setHMin(INT32_MIN);
  config.setHMax(INT32_MIN);
  runtime.setMinStep(-DEFAULT_STEPPER_TRAVEL);
  controller.resetStartingGear();
  initializeStartupPosition();
  assertGear(8);
  assert(motor.commands == 0 && motor.pos == 800);
  assert(controller.getCurrentPosition() == 800 && controller.getTargetPosition() == 800);
  controller.FTMSModeShiftModifier();
  controller.moveStepper();
  assert(motor.pos == 800);
  for (int gear : {9, 8, 7, 8}) {
    runtime.setShifterPosition(gear);
    controller.FTMSModeShiftModifier();
    controller.moveStepper();
    assert(motor.pos == gear * 100);
  }
  runtime.setFTMSMode(FitnessMachineControlPointProcedure::SetTargetPower);
  controller.FTMSModeShiftModifier();
  runtime.setFTMSMode(FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters);
  controller.FTMSModeShiftModifier();
  controller.moveStepper();
  assertGear(8);
  assert(motor.pos == 800);
  // A later home replaces the power-on origin with calibrated zero.
  controller.goHome(true);
  controller.FTMSModeShiftModifier();
  controller.moveStepper();
  assertGear(8);
  assert(runtime.getHomed() && motor.pos == 800);

  // Actual FTMS spindown mode, for every shipped profile and both home paths.
  /* SHIPPED PROFILES */
  const uint16_t* profiles[] = {nullptr, road, mtb, gravel, allrounder, nullptr};
  const int counts[] = {0,24,12,13,24,24};
  const int starts[] = {8,8,4,4,8,8};
  for (int profile = 0; profile < 6; ++profile) {
    reset();
    config.setShiftStep(1200);
    assert(profile == 5 ? config.setGearPreset(1) : config.setGearTeeth(profiles[profile], counts[profile]));
    controller.resetStartingGear();
    initializeStartupPosition();
    const int startupPosition = config.getGearProfile().offsetSteps(starts[profile], 1200);
    assertGear(starts[profile]);
    assert(motor.commands == 0 && motor.pos == startupPosition);
    controller.FTMSModeShiftModifier();
    controller.moveStepper();
    assert(motor.pos == startupPosition);
    for (bool ftms : {false, true}) {
      for (bool full : {false, true}) {
        reset(ftms);
        config.setShiftStep(1200);
        assert(profile == 5 ? config.setGearPreset(1) : config.setGearTeeth(profiles[profile], counts[profile]));
        runtime.setFTMSMode(FitnessMachineControlPointProcedure::SpinDownControl);
        runtime.setTargetIncline(7484); // Old recovered position must not become terrain.
        controller.goHome(full);
        assertGear(starts[profile]);
        assert(runtime.getFTMSMode() == FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters);
        assert(runtime.getTargetIncline() == 0);
        const int expected = config.getGearProfile().offsetSteps(starts[profile], 1200);
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
  const uint16_t ratios[] = {1010,1110,1210,1310,1410,1510,1610,1710,1810,1910,2010,2110};
  for (bool ftms : {false, true}) {
    for (bool bounded : {false, true}) {
      reset(ftms);
      if (bounded) config.setGearTeeth(ratios, 12);
      // A shift pending before homing is not a cancellation.
      runtime.setShifterPosition(3);
      controller.goHome(true);
      assert(runtime.getHomed() && !controller.homingFallback && pauses == 0);
      assert(saved == 1);
      assert(table.resets == (ftms ? 0 : 1)); // Mechanical reset happens only after success.
      assertGear(bounded ? 4 : 8);
      controller.FTMSModeShiftModifier();
      controller.moveStepper();
      assertGear(bounded ? 4 : 8);
      assert(motor.pos == (bounded ? 300 : 800));
    }
  }
  // Uneven ratio gaps must use the selected gear's actual ratio offset,
  // not one third of travel or the recovered FTMS sample position.
  const uint16_t uneven[] = {1010,1110,1410,1510,1610,1810,1910,2010,2210,2310,2410,2510,2710};
  for (bool full : {false, true}) {
    reset(true);
    config.setShiftStep(1200);
    config.setGearTeeth(uneven, 13);
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

  // Startup homing retains uploaded rows and their pending save, for both
  // mechanical and FTMS paths. Explicit full homing still clears the table.
  for (bool ftms : {false, true}) {
    reset(ftms);
    config.setHMax(20000);
    table.ptData.tableRow[0].tableEntry[0].targetPosition = 1234;
    table.ptData.tableRow[0].tableEntry[0].readings = 4;
    table._hasBeenLoadedThisSession = true;
    table.saveFlag = true;
    controller.goHome(false);
    assert(runtime.getHomed());
    assert(table.ptData.tableRow[0].tableEntry[0].targetPosition == 1234);
    assert(table.ptData.tableRow[0].tableEntry[0].readings == 4);
    assert(table.saveFlag && table._hasBeenLoadedThisSession);
    controller.goHome(true);
    assert(runtime.getHomed());
    assert(table.ptData.tableRow[0].tableEntry[0].targetPosition == INT16_MIN);
    assert(!table.saveFlag);
  }

  // Legacy startup calibrates both ends and adds three observations, without
  // resetting the watts table. New metadata bypasses the endpoint searches.
  reset(true);
  controller.goHome(false);
  assert(runtime.getHomed() && fullSearches == 2 && mapSearches == 3 && recoveries == 0);
  assert(table.resets == 0 && table.saves == 1);
  assert(table.ftmsCalibration.position[1] == 10100);
  metadataPresent = true;
  fullSearches = mapSearches = 0;
  controller.goHome(false);
  assert(runtime.getHomed() && fullSearches == 0 && mapSearches == 0 && recoveries == 1);
  assert(motor.pos == 10107 && controller.getTargetPosition() == 800);
  controller.moveStepper();
  assert(motor.pos == 800); // Move directly to eight shifts above zero after recovery.

  reset(true);
  saveSucceeds = false;
  controller.goHome(false);
  assert(!runtime.getHomed() && controller.homingFallback && saved == 0);

  reset(true);
  changeSourceDuringMap = true;
  controller.goHome(false);
  assert(!runtime.getHomed() && controller.homingFallback && saved == 0 && table.saves == 0);

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
  table.savedCalibration = table.ftmsCalibration;
  metadataPresent = true;
  runtime.resistance.setValue(48, false);
  controller.goHome(false);
  assert(motor.pos == table.ftmsCalibration.position[1] + 7);
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
  }
  assert(std::abs(motor.pos - 7440) <= 1 && controller.getTargetPosition() == motor.pos);
  assert(motor.commands == commandsBeforeReseat);
  assertGear(6);
  controller.moveStepper();
  assert(std::abs(motor.pos - 7440) <= 1); // Half-level map positions truncate to whole steps.

  // Failed/aborted FTMS homing starts a fresh, rideable Unlimited session.
  for (bool abort : {false, true}) {
    reset(true);
    config.setGearTeeth(ratios, 12);
    config.setHMin(0);
    config.setPTab4Pwr(true);
    runtime.setFTMSMode(FitnessMachineControlPointProcedure::SpinDownControl);
    runtime.setShifterPosition(5);
    abortSearch = abort;
    searchSucceeds = abort;
    controller.goHome(true);
    assert(!runtime.getHomed() && controller.homingFallback && pauses == 0);
    assertGear(0);
    assert(saved == 0 && table.resets == 0 && table.saves == 0);
    assert(config.getHMin() == 0 && config.getHMax() == 20000);
    assert(config.getGearProfile().count == 12 && config.getPTab4Pwr());
    assert(controller.activeGearRatios().unlimited() && !controller.usePowerTableForPower());
    assert(runtime.getMinStep() == -DEFAULT_STEPPER_TRAVEL && runtime.getMaxStep() == DEFAULT_STEPPER_TRAVEL);
    assert(runtime.getFTMSMode() == FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters);
    assert(motor.pos == 0 && controller.getTargetPosition() == 0);
    assert(fitnessMachineService.calculateResistanceFromPosition() == 50); // Wide provisional range, not saved 0..20000.
    int commands = motor.commands;
    runtime.setShifterPosition(-1);
    controller.FTMSModeShiftModifier();
    controller.moveStepper();
    assert(motor.commands == commands + 1 && motor.pos == -100);
    runtime.setShifterPosition(1);
    controller.FTMSModeShiftModifier();
    controller.moveStepper();
    assert(motor.pos == 100);
    // Fresh relative samples predict brake-watt limits even with live FTMS
    // resistance feedback. Negative samples survive subsequent insertions.
    config.setMinWatts(90);
    config.setMaxWatts(300);
    runtime.watts.setValue(200);
    table.ptHelpers.enterData(table.ptData, table.ptHelpers.calculateIndex(90, NORMAL_CAD), -20);
    table.ptHelpers.enterData(table.ptData, table.ptHelpers.calculateIndex(300, NORMAL_CAD), 40);
    table.setStepperMinMax();
    assert(runtime.getMinStep() == -200 && runtime.getMaxStep() == 400);
    runtime.setShifterPosition(-2);
    controller.FTMSModeShiftModifier();
    controller.moveStepper();
    assert(motor.pos == -200 && runtime.getShifterPosition() == -2);
    runtime.setShifterPosition(-3);
    controller.FTMSModeShiftModifier();
    assertGear(-2); // Stop extra downshifts at the learned minimum.
    runtime.setShifterPosition(5);
    controller.FTMSModeShiftModifier();
    assertGear(-2); // Stop extra upshifts at the learned maximum.
    // Peloton's ordinary unhomed resistance nudges must not bypass fallback's
    // watt-derived motor limits, including ERG/external position commands.
    controller.pelotonIsConnected = true;
    runtime.setFTMSMode(FitnessMachineControlPointProcedure::SetTargetPower);
    runtime.setTargetIncline(1000);
    controller.moveStepper();
    assert(motor.pos == 399);
    runtime.setTargetIncline(-1000);
    controller.moveStepper();
    assert(motor.pos == -199);
    runtime.setFTMSMode(FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters);
    controller.pelotonIsConnected = false;
    // Hardware protection still inhibits dispatch, without latching homing failure.
    safetyReady = false;
    commands = motor.commands;
    controller.moveStepper();
    assert(motor.commands == commands);
    safetyReady = true;
    searchSucceeds = true;
    abortSearch = false;
    controller.goHome(true);
    assert(runtime.getHomed() && !controller.homingFallback);
    assertGear(4);
    assert(!controller.activeGearRatios().unlimited() && controller.usePowerTableForPower());
  }
  // Invalid saved mechanical bounds must not select the homed start gear.
  reset();
  config.setHMax(-10);
  runtime.setShifterPosition(3);
  controller.goHome(false);
  assert(!runtime.getHomed() && pauses == 0);
  assertGear(0);
  assert(controller.homingFallback && runtime.getMinStep() == -DEFAULT_STEPPER_TRAVEL);

  // A failed physical minimum, maximum, unsupported board or absent motor uses
  // the same fallback without deleting saved calibration or keeping partial limits.
  for (int failure = 0; failure < 5; ++failure) {
    reset();
    config.setHMin(0);
    config.setGearTeeth(ratios, 12);
    runtime.setFTMSMode(FitnessMachineControlPointProcedure::SpinDownControl);
    failedMechanicalEnd = failure;
    if (failure == 2) currentBoard.homingSupported = false;
    if (failure == 3) stepper = nullptr;
    if (failure == 4) resetSucceeds = false;
    controller.goHome(true);
    assert(!runtime.getHomed() && controller.homingFallback && pauses == 0);
    assertGear(0);
    assert(saved == 0 && table.resets == (failure == 4 ? 1 : 0) && table.saves == 0);
    assert(config.getHMin() == 0 && config.getHMax() == 20000);
    assert(runtime.getMinStep() == -DEFAULT_STEPPER_TRAVEL && runtime.getMaxStep() == DEFAULT_STEPPER_TRAVEL);
    if (stepper) {
      runtime.setShifterPosition(-2);
      controller.FTMSModeShiftModifier();
      controller.moveStepper();
      assert(motor.pos == -200);
      // Saved 0..20000 must not be used by simulated resistance mode.
      runtime.setFTMSMode(FitnessMachineControlPointProcedure::SetTargetResistanceLevel);
      runtime.resistance.setTarget(50);
      controller._resistanceMove();
      assert(runtime.getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetPower);
    }
  }

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
  config.setGearTeeth(ratios, 12);
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
        self.assertEqual(len(presets), 5)
        declarations = []
        for name, (fronts, rears) in zip(("road", "mtb", "gravel", "allrounder"), presets[1:]):
            teeth = [f * 100 + r for f in json.loads(fronts) for r in json.loads(rears)]
            declarations.append("const uint16_t " + name + "[] = {" + ",".join(map(str, teeth)) + "};")
        harness = harness.replace("/* SHIPPED PROFILES */", "\n".join(declarations))
        harness = harness.replace("/* CONTROLLER */", function(header, "class SS2K {"))
        harness = harness.replace("/* FIRMWARE */", production)
        startup = main[main.index("  ss2k->setupTMCStepperDriver();"):]
        harness = harness.replace("/* STARTUP POSITION */", function(startup, "if (stepper)"))
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
                str(ROOT / "src/PowerTable_Helpers.cpp"),
                str(ROOT / "lib/SS2K/src/sensors/PelotonData.cpp"),
                str(ROOT / "lib/SS2K/src/sensors/SensorData.cpp"),
                str(ROOT / "lib/ArduinoCompat/src/NimBLEUUID.cpp"),
            ], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
