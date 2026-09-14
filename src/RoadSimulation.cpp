/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "Power_Table.h"
#include "BLE_Zwift_Service.h"
#include "BLE_OpenBikeControl_Service.h"

bool SS2K::roadSimulationSelected() const {
#ifndef SMARTSPIN2K_S3
  return false;
#else
  const uint8_t mode = rtConfig->getFTMSMode();
  if (!rtConfig->getHomed() || externalControl || zwiftService.isConnected() || openBikeControlService.isConnected() ||
      !(mode == 0 || mode == FitnessMachineControlPointProcedure::SetTargetInclination || mode == FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters)) return false;
  // Readiness does not depend on the current cadence or watts. Cache this scan
  // so the maintenance loop does not repeatedly traverse the complete table.
  static bool calibrated = false;
  static uint32_t checkedAt = 0;
  const uint32_t now = millis();
  if (!checkedAt || now - checkedAt >= 1000) {
    calibrated = powerTable->lookup(150, 90) != RETURN_ERROR;
    checkedAt = now;
  }
  return calibrated;
#endif
}

void SS2K::_roadSimulationMove() {
#ifdef SMARTSPIN2K_S3
  static RoadSimulation::SpeedFilter speed;
  static RoadSimulation::ShiftFeel shiftFeel;
  static uint32_t lastUpdate = 0;
  const uint32_t now = millis();
  if (rtConfig->roadStatus == RoadSimulation::Status::Off || now - lastUpdate > RoadSimulation::CADENCE_TIMEOUT_MS) {
    speed.reset();
    shiftFeel.reset();
  }
  if (rtConfig->roadStatus != RoadSimulation::Status::Off && now - lastUpdate < RoadSimulation::UPDATE_INTERVAL_MS) return;
  lastUpdate = now;
  rtConfig->roadTargetWatts = 0;
  rtConfig->roadLoadSpeedMps = 0;

  const int cadence = rtConfig->cad.getValue();
  if (cadence < 20 || cadence >= 250 || now - static_cast<uint32_t>(rtConfig->cad.getTimestamp()) > RoadSimulation::CADENCE_TIMEOUT_MS) {
    rtConfig->roadStatus = RoadSimulation::Status::NoCadence;
    targetPosition = currentPosition;
    speed.reset();
    shiftFeel.reset();
    return;
  }
  if (!rtConfig->getHomed()) {
    rtConfig->roadStatus = RoadSimulation::Status::Unhomed;
    targetPosition = currentPosition;
    speed.reset();
    shiftFeel.reset();
    return;
  }

  const float ratio = userConfig->getGearRatios().ratio(rtConfig->getShifterPosition());
  rtConfig->roadLoadSpeedMps = speed.update(cadence, ratio, now);
  float watts = RoadSimulation::brakeWatts(cadence, ratio, rtConfig->roadLoadSpeedMps, userConfig->getRiderWeightKg(), rtConfig->roadParameters);
  watts = std::min(4000.0f, watts * shiftFeel.update(ratio, now));
  if (userConfig->getMaxWatts() > 0) watts = std::min(watts, static_cast<float>(userConfig->getMaxWatts()));
  rtConfig->roadTargetWatts = watts;
  const int32_t position = powerTable->lookup(static_cast<int>(std::lround(watts)), cadence);
  if (position == RETURN_ERROR) {
    rtConfig->roadStatus = RoadSimulation::Status::NoTable;
    targetPosition = currentPosition;
    return;
  }
  rtConfig->roadStatus = RoadSimulation::Status::Active;
  // A passive brake cannot provide downhill assistance. Release to the homed
  // minimum rather than extrapolating an arbitrary zero-watt table position.
  const int32_t requested = watts <= 0 ? rtConfig->getMinStep() : position;
  const int32_t travel = std::max(1, userConfig->getStepperSpeed() / 10);
  const int64_t delta = static_cast<int64_t>(requested) - currentPosition;
  targetPosition = currentPosition + static_cast<int32_t>(std::max<int64_t>(-travel, std::min<int64_t>(delta, travel)));
  // Homing limits, Peloton guards and motor direction handling remain in moveStepper().
#endif
}
