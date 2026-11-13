/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <stdint.h>

// Firmware positions, saved tables, speed and acceleration retain their original
// quarter-step units. Only the pulse generator uses sixteenth steps.
template <typename Motor>
class LogicalStepper {
 public:
  static constexpr uint16_t microsteps = 16;
  static constexpr int32_t pulsesPerStep = microsteps / 4;
  static constexpr int32_t minPosition = INT32_MIN / pulsesPerStep;
  static constexpr int32_t maxPosition = INT32_MAX / pulsesPerStep;
  static constexpr int8_t rangeError = -127;

  LogicalStepper* attach(Motor* motor) {
    motor_ = motor;
    return motor ? this : nullptr;
  }

  // Partial logical steps during motion are truncated toward zero. Settled
  // absolute targets round-trip exactly, including negative coordinates.
  int32_t getCurrentPosition() { return motor_->getCurrentPosition() / pulsesPerStep; }
  bool setCurrentPosition(int32_t position) {
    if (!fits(position)) return false;
    motor_->setCurrentPosition(position * pulsesPerStep);
    return true;
  }
  int8_t moveTo(int32_t position, bool blocking = false) {
    if (!fits(position)) return rangeError;
    return static_cast<int8_t>(motor_->moveTo(position * pulsesPerStep, blocking));
  }
  int8_t move(int32_t distance, bool blocking = false) {
    if (!fits(distance)) return rangeError;
    // Preserve the library's relative-to-queued-target semantics.
    return static_cast<int8_t>(motor_->move(distance * pulsesPerStep, blocking));
  }
  int8_t setSpeedInHz(uint32_t speed) {
    if (speed > UINT32_MAX / pulsesPerStep) return rangeError;
    return motor_->setSpeedInHz(speed * pulsesPerStep);
  }
  uint32_t getSpeedInMilliHz() { return motor_->getSpeedInMilliHz() / pulsesPerStep; }
  int8_t setAcceleration(int32_t acceleration) {
    if (acceleration <= 0 || acceleration > maxPosition) return rangeError;
    return motor_->setAcceleration(acceleration * pulsesPerStep);
  }

  // Safety stops must not discard a fractional logical step by converting the
  // position to logical units and back before aborting the hardware queue.
  void forceStopAtCurrentPosition() { motor_->forceStopAtCurrentPosition(); }

  bool isRunning() { return motor_->isRunning(); }
  void stopMove() { motor_->stopMove(); }
  void forceStop() { motor_->forceStop(); }
  int8_t runForward() { return static_cast<int8_t>(motor_->runForward()); }
  int8_t runBackward() { return static_cast<int8_t>(motor_->runBackward()); }
  const char* driverTypeString() const { return motor_->driverTypeString(); }
  int stackFree() const { return motor_->stackFree(); }
  bool enableOutputs() { return motor_->enableOutputs(); }
  bool disableOutputs() { return motor_->disableOutputs(); }
  void setAutoEnable(bool enabled) { motor_->setAutoEnable(enabled); }
  void setDelayToDisable(uint16_t milliseconds) { motor_->setDelayToDisable(milliseconds); }
  void setDirectionPin(uint8_t pin, bool forward) { motor_->setDirectionPin(pin, forward); }
  void setEnablePin(uint8_t pin) { motor_->setEnablePin(pin); }

 private:
  static bool fits(int32_t value) { return value >= minPosition && value <= maxPosition; }
  Motor* motor_ = nullptr;
};
