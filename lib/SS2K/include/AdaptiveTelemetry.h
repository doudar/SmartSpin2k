/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <algorithm>
#include <cstdint>

// A baseline heartbeat plus fresh-sample sends. No queued history and no catch-up
// burst after a stall. Timestamps use unsigned subtraction across millis wrap.
class AdaptiveTelemetry {
 public:
  AdaptiveTelemetry(uint32_t baselineMs, uint32_t minimumMs)
      : baseline(baselineMs), minimum(minimumMs), interval(minimumMs) {}

  bool due(uint32_t now, uint32_t sequence, bool constrained) {
    if (!initialized) {
      initialized = true;
      lastAttempt = now;
    }
    const uint32_t elapsed = now - lastAttempt;
    const uint32_t limit = constrained ? baseline : interval;
    return elapsed >= baseline || (sequence != sentSequence && elapsed >= limit);
  }

  void attempted(uint32_t now, uint32_t sequence, bool success) {
    lastAttempt = now;
    sentSequence = sequence;
    if (!success) {
      interval = std::min(baseline, interval * 2);
      successes = 0;
    } else if (++successes >= 3) {
      interval = std::max(minimum, interval / 2);
      successes = 0;
    }
  }

 private:
  const uint32_t baseline, minimum;
  uint32_t interval, lastAttempt = 0, sentSequence = 0;
  unsigned successes = 0;
  bool initialized = false;
};
