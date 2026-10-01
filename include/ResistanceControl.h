/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <stdint.h>
#include <limits.h>
#include <algorithm>
#include <cmath>

namespace ResistanceControl {
// Shared by normal resistance mode and homing. A known calibration scale
// supplies a bounded position estimate; otherwise retain normal shift tuning.
inline int32_t target(int32_t position, int error, int shiftStep, float sensitivity, float stepsPerLevel = 0) {
  if (error == 0) return position;
  const int direction = error > 0 ? 1 : -1;
  const int magnitude = error > 0 ? error : -error;
  const double delta = stepsPerLevel > 0 ? std::max(-static_cast<double>(shiftStep), std::min(static_cast<double>(shiftStep), error * static_cast<double>(stepsPerLevel))) :
                       magnitude > 20 - sensitivity ? shiftStep * direction :
                       (magnitude > 1 ? error * 3 : error) + sensitivity * direction;
  const double next = position + delta;
  return next > INT32_MAX ? INT32_MAX : next < INT32_MIN ? INT32_MIN : static_cast<int32_t>(next);
}

class Controller {
 public:
  int32_t update(int32_t position, int resistance, int requested, uint32_t sampleTime, uint32_t now, int shiftStep, float sensitivity, float stepsPerLevel = 0) {
    if (!initialized_ || requested != requested_ || now - lastCall_ > 10000) {
      *this = Controller();
      initialized_ = true;
      requested_ = requested;
      sampleTime_ = sampleTime;
      previous_ = resistance;
    }
    lastCall_ = now;
    const int error = requested - resistance;
    if (sampleTime != sampleTime_) {
      const uint32_t elapsed = sampleTime - sampleTime_;
      if (elapsed <= 10000) rate_ = 1000.0f * (resistance - previous_) / elapsed;
      else rate_ = 0;
      sampleTime_ = sampleTime;
      previous_ = resistance;
    }
    // Require a crossing beyond the +/-2 noise band. Repeated reports
    // and adjacent-level jitter cannot train damping upward.
    const int side = error > 2 ? 1 : error < -2 ? -1 : 0;
    if (side) {
      if (side_ && side != side_) damping_ = std::min(2.5f, damping_ + 0.5f);
      side_ = side;
    }
    if (!error) return position;
    const int32_t ordinary = target(position, error, shiftStep, sensitivity, stepsPerLevel);
    // D can brake an approach, never command a reversal away from the target.
    // Held/old reports do not imply a continuing resistance velocity forever.
    const float rate = now - sampleTime_ <= 1500 ? rate_ : 0;
    const float approaching = error > 0 ? rate : -rate;
    const float magnitude = std::abs(error);
    const float fraction = std::max(0.0f, std::min(1.0f, (magnitude - damping_ * approaching) / magnitude));
    return static_cast<int32_t>(position + (static_cast<double>(ordinary) - position) * fraction);
  }

  float damping() const { return damping_; }

 private:
  bool initialized_ = false;
  int requested_ = 0, previous_ = 0, side_ = 0;
  uint32_t sampleTime_ = 0, lastCall_ = 0;
  float rate_ = 0, damping_ = 0;
};
}  // namespace ResistanceControl
