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

void SS2K::resetStartingGear() {
  // Spindown is a procedure, not a riding mode. Leaving its opcode selected
  // bypasses local gearing and interprets the recovered position as terrain.
  if ((rtConfig->getHomed() || homingFallback) && rtConfig->getFTMSMode() == FitnessMachineControlPointProcedure::SpinDownControl) {
    rtConfig->setFTMSMode(FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters);
    rtConfig->setTargetIncline(0);
  }
  localGear = homingFallback ? 0 : activeGearRatios().startGear();
  rtConfig->setShifterPosition(homingFallback ? 0 : (localGearingSelected() ? localGear : SHIFTER_MIDDLE_POSITION));
  // A programmatic gear reset is not a rider shift. Keep the shift baseline in sync so
  // nothing downstream (homing's abort check, FTMS forwarding) sees a phantom shift.
  lastShifterPosition = rtConfig->getShifterPosition();
}

VirtualGearing::Gears SS2K::activeGearRatios() const {
  return homingFallback ? VirtualGearing::Gears{} : userConfig->getGearRatios();
}

bool SS2K::usePowerTableForPower() const {
  return userConfig->getPTab4Pwr() && !homingFallback;
}

bool SS2K::localGearingSelected() const {
  const uint8_t mode = rtConfig->getFTMSMode();
  return !externalControl && !zwiftService.isConnected() && !openBikeControlService.isConnected() &&
         (mode == 0 || mode == FitnessMachineControlPointProcedure::SetTargetInclination || mode == FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters);
}

// Unclamped travel target for a logical gear. Shared with the shift-limit check so
// both agree on where a gear would put the knob before the limits are applied.
int32_t SS2K::gearTargetPosition(int gear) const {
  const int32_t shiftStep = userConfig->getShiftStep();
  const int64_t offset = localGearingSelected() ? activeGearRatios().offsetSteps(gear, shiftStep) : static_cast<int64_t>(gear) * shiftStep;
  const double target = offset + static_cast<double>(ftmsSimulationOffset) + static_cast<double>(rtConfig->getTargetIncline()) * userConfig->getInclineMultiplier();
  if (!std::isfinite(target)) return currentPosition;
  return static_cast<int32_t>(std::max(static_cast<double>(INT32_MIN), std::min(static_cast<double>(INT32_MAX), target)));
}

int32_t SS2K::simulationTargetPosition() const {
  const bool localSelected = localGearingSelected();
  // An incoming FTMS command can enter sim mode before the next shift-modifier
  // pass restores the saved gear. Forward the same target that pass will select.
  const int gear = localSelected && !localGearingActive ? localGear : rtConfig->getShifterPosition();
  const int32_t requested = gearTargetPosition(gear);
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
