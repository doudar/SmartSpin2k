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

// Integrate the previous rate over actual elapsed time. Fractional revolutions
// survive rate changes and stops; event time advances only on a completed turn.
class CyclingRevolutions {
 public:
  void update(uint32_t now, double rpm) {
    if (!initialized) {
      initialized = true;
      lastMillis = now;
      elapsedMillis = now;
    }
    const uint32_t delta = now - lastMillis;
    elapsedMillis += delta;
    lastMillis = now;
    if (rate > 0) {
      const double turns = phase + rate * delta / 60000.0;
      const uint32_t completed = static_cast<uint32_t>(std::floor(turns + 1e-9));
      phase = std::max(0.0, turns - completed);
      if (phase < 1e-9) phase = 0;  // Avoid rounding an exact event down by one BLE clock tick.
      if (completed) {
        revolutions += completed;
        lastEventMillis = static_cast<double>(elapsedMillis) - phase * 60000.0 / rate;
      }
    }
    rate = std::isfinite(rpm) && rpm > 0 ? rpm : 0;
  }

  uint32_t count() const { return revolutions; }
  uint16_t eventTime(unsigned ticksPerSecond) const {
    return static_cast<uint16_t>(std::fmod(lastEventMillis * ticksPerSecond / 1000.0, 65536.0));
  }

 private:
  bool initialized = false;
  uint32_t lastMillis = 0, revolutions = 0;
  uint64_t elapsedMillis = 0;
  double rate = 0, phase = 0, lastEventMillis = 0;
};
