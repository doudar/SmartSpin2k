/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "BLE_Zwift_Service.h"
#include "BLE_OpenBikeControl_Service.h"
#include <cmath>

bool SS2K::localGearingSelected() const {
  const uint8_t mode = rtConfig->getFTMSMode();
  return !externalControl && !zwiftService.isConnected() && !openBikeControlService.isConnected() &&
         (mode == 0 || mode == FitnessMachineControlPointProcedure::SetTargetInclination || mode == FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters);
}

int32_t SS2K::simulationTargetPosition() const {
  const int32_t shiftStep = userConfig->getShiftStep();
  const bool localSelected = localGearingSelected();
  // An incoming FTMS command can enter sim mode before the next shift-modifier
  // pass restores the saved gear. Forward the same target that pass will select.
  const int gear = localSelected && !localGearingActive ? localGear : rtConfig->getShifterPosition();
  const int64_t offset = localSelected ? userConfig->getGearRatios().offsetSteps(gear, shiftStep) : static_cast<int64_t>(gear) * shiftStep;
  const double target = offset + static_cast<double>(rtConfig->getTargetIncline()) * userConfig->getInclineMultiplier();
  if (!std::isfinite(target)) return currentPosition;
  const int32_t requested = static_cast<int32_t>(std::max(static_cast<double>(INT32_MIN), std::min(static_cast<double>(INT32_MAX), target)));
  // Apply the common travel limits before FTMS forwarding too. The motor loop
  // retains its additional hardware-specific guards and final travel check.
  const int32_t minimum = rtConfig->getMinStep();
  const int32_t maximum = rtConfig->getMaxStep();
  if (minimum < maximum) {
    if (requested < minimum) return minimum + 1;
    if (requested > maximum) return maximum - 1;
  }
  return requested;
}
