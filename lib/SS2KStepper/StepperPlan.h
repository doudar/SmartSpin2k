/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// Hardware-independent, single-axis trapezoidal planner. Positions are pulses.
// It plans small constant-frequency batches, not individual pulse interrupts.
class StepperPlan {
 public:
  struct Batch {
    uint32_t ticks;  // 16 MHz interval; long intervals are split by the queue feeder.
    uint8_t steps;
    bool forward;
  };
  static constexpr uint32_t clockHz = 16000000;
  static constexpr uint32_t maxSpeed = 200000;

  int32_t position = 0;
  int32_t target = 0;
  uint32_t speed = 14000;
  int32_t acceleration = 12000;

  void moveTo(int32_t value) { target = value; continuous_ = 0; stopping_ = false; }
  void run(bool forward) { continuous_ = forward ? 1 : -1; stopping_ = false; }
  void stop() { continuous_ = 0; stopping_ = true; }
  void reset(int32_t value) { position = target = value; velocity_ = velocitySquared_ = 0; continuous_ = 0; stopping_ = false; }
  void restartAt(int32_t value) { position = value; velocity_ = velocitySquared_ = 0; if (stopping_) target = value; }
  bool active() const { return velocity_ > 0 || (!stopping_ && (continuous_ != 0 || position != target)); }
  int32_t relativeBase() const { return continuous_ ? position : target; }

  bool next(Batch& batch) {
    if (!active() || speed == 0 || acceleration <= 0) return false;
    int64_t delta = static_cast<int64_t>(target) - position;
    int desired = stopping_ ? 0 : continuous_ ? continuous_ : (delta > 0 ? 1 : delta < 0 ? -1 : 0);
    if (velocity_ == 0) {
      if (!desired) return false;
      direction_ = desired;
    }
    const double a = acceleration;
    const double v2 = velocitySquared_;
    // Approximately 2 ms of motion per batch; low speed uses one pulse.
    uint32_t count = std::max<uint32_t>(1, std::min<uint32_t>(255, static_cast<uint32_t>(velocity_ * 0.002)));
    const bool brake = desired != direction_;
    if (brake) count = std::min(count, std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(v2 / (2 * a)))));
    if (!continuous_ && !brake) count = static_cast<uint32_t>(std::min<int64_t>(count, std::abs(delta)));
    if (!count) return false;
    // Never wrap continuous homing coordinates at the signed position limits.
    const int64_t room = direction_ > 0 ? static_cast<int64_t>(INT32_MAX) - position : static_cast<int64_t>(position) - INT32_MIN;
    if (!room) { reset(position); return false; }
    count = static_cast<uint32_t>(std::min<int64_t>(count, room));
    const double dv2 = 2 * a * count;
    double nextV2 = brake ? 0 : static_cast<double>(speed) * speed;
    if (!brake && !continuous_) nextV2 = std::min(nextV2, 2 * a * (std::abs(delta) - count));
    nextV2 = std::max(std::max(0.0, v2 - dv2), std::min(v2 + dv2, nextV2));
    const double nextV = std::sqrt(nextV2);
    // A one-step, rest-to-rest move contains both acceleration and braking.
    double seconds = velocity_ + nextV > 0 ? 2.0 * count / (velocity_ + nextV) : 2.0 * std::sqrt(count / a);
    if (velocity_ == 0 && nextV == 0 && speed < std::sqrt(a * count)) seconds = static_cast<double>(count) / speed + speed / a;
    seconds = std::max(seconds, static_cast<double>(count) / std::max<double>(speed, velocity_));
    uint32_t ticks = static_cast<uint32_t>(std::min<double>(UINT32_MAX, std::ceil(seconds * clockHz / count)));
    // Leave at least 200 us between queue interrupts, including short final batches.
    ticks = std::max(ticks, (3200u + count - 1) / count);
    batch = {ticks, static_cast<uint8_t>(count), direction_ > 0};
    position = static_cast<int32_t>(static_cast<int64_t>(position) + direction_ * static_cast<int64_t>(count));
    velocity_ = nextV;
    // Preserve the exact squared-speed state rather than repeatedly squaring
    // rounded square roots, which can leave residual velocity at a final stop.
    velocitySquared_ = nextV2;
    if (stopping_ && velocity_ == 0) target = position;
    return true;
  }

 private:
  double velocity_ = 0;
  double velocitySquared_ = 0;
  int direction_ = 1;
  int continuous_ = 0;
  bool stopping_ = false;
};
