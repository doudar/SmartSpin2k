/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "SS2KStepper.h"

bool SS2KStepper::begin(uint8_t stepPin, bool (*callback)(uint8_t, uint8_t)) {
  Lock lock(*this);
  if (initialized_) return true;
  enableCallback_ = callback;
  if (!queue_.begin(stepPin)) return false;
  if (xTaskCreate(taskEntry, "StepperTask", stackBytes, this, configMAX_PRIORITIES - 1, &task_) != pdPASS) {
    queue_.release();
    return false;
  }
  initialized_ = true;
  return true;
}

void SS2KStepper::taskEntry(void* arg) {
  auto& motor = *static_cast<SS2KStepper*>(arg);
  for (;;) {
    motor.service();
    vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(4)));
  }
}

bool SS2KStepper::enable(bool value) {
  if (enablePin_ == 255 || !enableCallback_) return false;
  // Callback returns the actual active-low EN level, including thermal inhibition.
  bool level = enableCallback_(enablePin_, value ? 0 : 1);
  if (!enabled_ && !level) needEnableDelay_ = true;
  enabled_ = !level;
  return enabled_ == value;
}

bool SS2KStepper::moving() { return plan_.active() || pending_.steps || queue_.busy(); }
void SS2KStepper::abort() {
  plan_.reset(queue_.stop());
  pending_ = {};
  remainingTicks_ = 0;
  pendingDirection_ = false;
  wasQueued_ = false;
}

void SS2KStepper::service() {
  Lock lock(*this);
  if (!initialized_) return;
  const bool queued = queue_.busy();
  if (wasQueued_ && !queued && (plan_.active() || pending_.steps)) {
    // Recover from a scheduling underrun from rest, preserving the requested goal.
    plan_.restartAt(queue_.position());
    pending_ = {};
  }
  if (!moving()) {
    if (autoEnable_ && enabled_ && static_cast<TickType_t>(xTaskGetTickCount() - lastActive_) >= pdMS_TO_TICKS(disableDelay_)) enable(false);
    return;
  }
  if (autoEnable_ && !enabled_ && !enable(true)) { abort(); return; }
  // The firmware also calls manual enable while cadence is present.
  if (!enabled_) return;
  if (needEnableDelay_) {
    if (!queue_.push(3200, 0, queue_.direction())) return;
    needEnableDelay_ = false;
  }
  lastActive_ = xTaskGetTickCount();
  // Bound work and lookahead to 20 ms. A delayed task drains to a stopped output.
  for (unsigned i = 0; i < ss2k_motor::PulseQueue::capacity && queue_.queuedTicks() < StepperPlan::clockHz / 50; ++i) {
    if (!pending_.steps) {
      if (!plan_.next(pending_)) break;
      remainingTicks_ = pending_.ticks;
      pendingDirection_ = queue_.direction() != pending_.forward;
    }
    if (pendingDirection_) {
      // A direction edge precedes any new pulse by at least 200 us.
      if (!queue_.push(3200, 0, pending_.forward, true)) break;
      pendingDirection_ = false;
    }
    if (remainingTicks_ > 65535) {
      const uint16_t pause = static_cast<uint16_t>(std::min<uint32_t>(65535, remainingTicks_ - 3200));
      if (!queue_.push(pause, 0, pending_.forward)) break;
      remainingTicks_ -= pause;
      continue;
    }
    if (!queue_.push(static_cast<uint16_t>(remainingTicks_), pending_.steps, pending_.forward)) break;
    pending_ = {};
  }
  queue_.start();
  wasQueued_ = queue_.busy();
}

int32_t SS2KStepper::getCurrentPosition() { Lock lock(*this); return queue_.position(); }
void SS2KStepper::setCurrentPosition(int32_t value) {
  Lock lock(*this);
  abort();
  queue_.rebase(value);
  plan_.reset(value);
}
int8_t SS2KStepper::moveTo(int32_t value, bool blocking) {
  { Lock lock(*this); if (!initialized_) return -1; plan_.moveTo(value); }
  if (blocking) while (isRunning()) vTaskDelay(1);
  return 0;
}
int8_t SS2KStepper::move(int32_t distance, bool blocking) {
  {
    Lock lock(*this);
    if (!initialized_) return -1;
    const int64_t target = static_cast<int64_t>(moving() ? plan_.relativeBase() : queue_.position()) + distance;
    if (target < INT32_MIN || target > INT32_MAX) return -127;
    plan_.moveTo(static_cast<int32_t>(target));
  }
  if (blocking) while (isRunning()) vTaskDelay(1);
  return 0;
}
int8_t SS2KStepper::continuous(bool forward) { Lock lock(*this); if (!initialized_) return -1; plan_.run(forward); return 0; }
int8_t SS2KStepper::runForward() { return continuous(true); }
int8_t SS2KStepper::runBackward() { return continuous(false); }
void SS2KStepper::stopMove() { Lock lock(*this); plan_.stop(); }
void SS2KStepper::forceStop() { Lock lock(*this); abort(); }
void SS2KStepper::forceStopAtCurrentPosition() { forceStop(); }
bool SS2KStepper::isRunning() { Lock lock(*this); return moving(); }
int8_t SS2KStepper::setSpeedInHz(uint32_t speed) {
  if (!speed || speed > StepperPlan::maxSpeed) return -2;
  Lock lock(*this); plan_.speed = speed; return 0;
}
uint32_t SS2KStepper::getSpeedInMilliHz() { Lock lock(*this); return plan_.speed * 1000; }
int8_t SS2KStepper::setAcceleration(int32_t acceleration) {
  if (acceleration <= 0) return -3;
  Lock lock(*this); plan_.acceleration = acceleration; return 0;
}
void SS2KStepper::setDirectionPin(uint8_t pin, bool forward) { Lock lock(*this); if (!moving()) queue_.directionPin(pin, forward); }
void SS2KStepper::setEnablePin(uint8_t pin) { Lock lock(*this); enablePin_ = pin; enable(false); }
bool SS2KStepper::enableOutputs() { Lock lock(*this); return enable(true); }
bool SS2KStepper::disableOutputs() { Lock lock(*this); return enable(false); }
void SS2KStepper::setAutoEnable(bool value) {
  Lock lock(*this);
  if (autoEnable_ == value) return;
  autoEnable_ = value;
  lastActive_ = xTaskGetTickCount();
}
void SS2KStepper::setDelayToDisable(uint16_t milliseconds) { Lock lock(*this); disableDelay_ = milliseconds; }
