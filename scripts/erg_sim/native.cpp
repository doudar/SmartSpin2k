/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Test-only peripheral adapter. Production implementations are inserted at
// build time; no copied ERG, learning, motor-dispatch, or stall decisions.
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include "ERG_Mode.h"
#include "Power_Table.h"
#include "BLE_Definitions.h"
using std::max;
using std::min;
template <typename T> T constrain(T x, T lo, T hi) { return min(hi, max(lo, x)); }
uint32_t clockMs = 1000;
void delay(int ms);
void record(const char* tag, const char* fmt, ...) {
  std::fprintf(stderr, "[%u](%s): ", clockMs, tag);
  va_list args; va_start(args, fmt); std::vfprintf(stderr, fmt, args); va_end(args);
  std::fputc('\n', stderr);
}
#define SS2K_LOG record
#define SS2K_LOGW record
#define MAIN_LOG_TAG "Main"

RuntimeParameters runtime;
RuntimeParameters* rtConfig = &runtime;
userParameters config;
userParameters* userConfig = &config;
void userParameters::saveToLittleFS() {}
struct { bool connectedPM = true; } spinBLEClient;
struct { int spinDownFlag = 0; } spinBLEServer;
struct { int dirPin = 0; float homingSensitivityScaler = 1; } currentBoard;
struct { bool exists(const char*) { return false; } bool remove(const char*) { return true; } } LittleFS;
int commits = 0;
struct BLE_ss2kCustomCharacteristic { static void notify(int, int) { ++commits; } };

// Pulse generation substitutes for FastAccelStepper only. The position is a
// commanded step counter, NOT an encoder fed from the Python plant.
struct Motor {
  double position = 0, velocity = 0;
  int32_t target = 0;
  double speed = DEFAULT_STEPPER_SPEED;
  bool enabled = true;
  int32_t getCurrentPosition() const { return std::lround(position); }
  bool isRunning() const { return std::abs(position-target) > .01 || std::abs(velocity) > .01; }
  void setCurrentPosition(int32_t p) { position = target = p; velocity = 0; }
  int moveTo(int32_t p) { target = p; return 0; }
  void forceStop() { target = getCurrentPosition(); position = target; velocity = 0; }
  void stopMove() { forceStop(); }
  void enableOutputs() { enabled = true; }
  void setAutoEnable(bool) { enabled = true; }
  void setDirectionPin(int, bool) {}
  void runForward() { target = INT32_MAX/2; }
  void runBackward() { target = INT32_MIN/2; }
  int advance(int ms) {
    const int before = getCurrentPosition();
    // A 1 ms integrator keeps pulse counts independent of outer IO tick size.
    for (int i=0; i<ms; ++i) {
      double error = target-position;
      const double wanted = std::copysign(min(speed, std::sqrt(2*STEPPER_ACCELERATION*std::abs(error))), error);
      velocity += constrain(wanted-velocity, -STEPPER_ACCELERATION*.001, STEPPER_ACCELERATION*.001);
      const double step = velocity*.001;
      if (error*step >= 0 && std::abs(step) >= std::abs(error)) { position = target; velocity = 0; }
      else position += step;
    }
    return getCurrentPosition()-before;
  }
} motor;
Motor* stepper = &motor;

struct SS2K {
  int32_t currentPosition = 0, targetPosition = 0;
  int lastShifterPosition = 0;
  bool stepperIsRunning = false, resetPowerTableFlag = false;
  bool externalControl = false, syncMode = false, pelotonIsConnected = false, homingFallback = false;
  int32_t getCurrentPosition() { return currentPosition; }
  int32_t getTargetPosition() { return targetPosition; }
  bool usePowerTableForPower() { return false; }
  bool stepperSafetyReady() { return true; }
  int32_t simulationTargetPosition() { return targetPosition; }
  void _resistanceMove() { std::abort(); } // This harness deliberately accepts ERG only.
  void updateStealthChop(bool) {}
  void updateStepperPower(int) {}
  void updateStepperSpeed(int speed) { motor.speed = speed; }
  void setupTMCStepperDriver(bool) { motor.speed = userConfig->getStepperSpeed(); }
  void moveStepper();
  bool _findEndStop(bool forward);
} controllerMotor;
SS2K* ss2k = &controllerMotor;
PowerTable table;
PowerTable* powerTable = &table;
ErgMode controller;
ErgMode* ergMode = &controller;
bool PowerTable::_manageSaveState(bool, bool) { _hasBeenLoadedThisSession=true; return true; }
bool PowerTable::_save() { return true; }
void PowerTable::toLog() {}
struct Driver {
  double load = 0;
  int SG_RESULT() { return std::lround(300-290*constrain(load, 0.0, 1.0)); }
} driverObject;
Driver* driver = &driverObject;
struct { void spinDown(int) {} } fitnessMachineService;
constexpr int LOG_INTERVAL = 1000;
struct HomingSgBaseline { int threshold; int sensitivity; };
/* PRODUCTION_TABLE */
/* PRODUCTION_ERG */
/* PRODUCTION_STEPPER */
/* PRODUCTION_HOMING */

void delay(int ms) {
  // Blocking firmware procedures still advance the shared simulation clock.
  const int pulses = motor.advance(ms);
  clockMs += ms;
  std::cout << "D " << clockMs << ' ' << pulses << '\n' << std::flush;
  std::string line;
  if (!std::getline(std::cin, line)) std::exit(3);
  std::istringstream input(line); char op; double load;
  if (!(input >> op >> load) || op != 'L') std::exit(4);
  driver->load = load;
}

int main() {
  std::string line;
  bool initialized = false;
  while (std::getline(std::cin, line)) {
    std::istringstream input(line);
    char op; input >> op;
    if (op == 'I') {
      int pos, lo, hi, speed, minWatts; double sensitivity;
      if (initialized || !(input >> pos >> lo >> hi >> speed >> sensitivity >> minWatts)) return 2;
      initialized = true;
      userConfig->setERGSensitivity(sensitivity); userConfig->setStepperSpeed(speed);
      userConfig->setShiftStep(1000); userConfig->setMinWatts(minWatts); userConfig->setMaxWatts(2000);
      userConfig->setHMin(lo); userConfig->setHMax(hi); userConfig->setStepperDir(false);
      userConfig->setStepperPower(900); userConfig->setHomingSensitivity(10);
      rtConfig->setMinStep(lo); rtConfig->setMaxStep(hi); rtConfig->setHomed(true);
      rtConfig->setFTMSMode(FitnessMachineControlPointProcedure::SetTargetPower);
      motor.setCurrentPosition(pos); motor.speed = speed;
      ss2k->currentPosition = ss2k->targetPosition = pos;
      rtConfig->setTargetIncline(pos);
      table.clearRuntime();
      std::cout << "I 1\n" << std::flush;
    } else if (!initialized) return 2;
    else if (op == 'T') {
      uint32_t now; int watts, cadence, fresh, target; double load;
      if (!(input >> now >> watts >> cadence >> fresh >> target >> load) || now < clockMs || now-clockMs > 1000) return 2;
      const int pulses = motor.advance(now-clockMs); clockMs = now;
      if (fresh) { rtConfig->watts.setValue(watts, false); rtConfig->cad.setValue(cadence, false); }
      if (target >= 0) rtConfig->watts.setTarget(target);
      driver->load = load;
      ss2k->moveStepper();
      controller.runERG();
      int cells = 0;
      for (const auto& row:table.ptData.tableRow) for (const auto& cell:row.tableEntry) cells += cell.readings >= 2;
      std::cout << "T " << pulses << ' ' << motor.getCurrentPosition() << ' ' << motor.target << ' '
                << cells << ' ' << controller.isTableSeeking() << ' ' << rtConfig->watts.getTarget() << '\n' << std::flush;
    } else if (op == 'P') {
      // Read-only final RAM snapshot: never send the plant curve into learning.
      std::cout << "P " << TABLE_VERSION << ' ' << table.ptHelpers.getTotalReadings(table.ptData) << ' ' << rtConfig->getHomed() << ' '
                << POWERTABLE_CAD_SIZE << ' ' << POWERTABLE_WATT_SIZE << ' ' << MINIMUM_TABLE_CAD << ' ' << POWERTABLE_CAD_INCREMENT << ' '
                << POWERTABLE_WATT_INCREMENT << ' ' << TABLE_DIVISOR;
      for (const auto& row:table.ptData.tableRow) for (const auto& cell:row.tableEntry)
        std::cout << ' ' << cell.targetPosition << ' ' << static_cast<int>(cell.readings);
      std::cout << '\n' << std::flush;
    } else if (op == 'H') {
      int forward; if (!(input >> forward)) return 2;
      const bool found = ss2k->_findEndStop(forward != 0);
      std::cout << "H " << found << ' ' << clockMs << ' ' << motor.getCurrentPosition() << '\n' << std::flush;
    } else if (op == 'Q') return 0;
    else return 2;
  }
}
