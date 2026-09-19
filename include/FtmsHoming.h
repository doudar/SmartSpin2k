/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <stdint.h>

namespace FtmsHoming {
enum class Failure { None, Cancelled, Simulated, StaleReport, Timeout, InvalidResistance, NoProgress, WrongDirection, Motor, Boundary, UnstableResistance };

inline const char* failureName(Failure failure) {
  switch (failure) {
    case Failure::None: return "none";
    case Failure::Cancelled: return "cancelled by shifter";
    case Failure::Simulated: return "resistance is simulated";
    case Failure::StaleReport: return "no fresh resistance report";
    case Failure::Timeout: return "search timeout";
    case Failure::InvalidResistance: return "resistance outside 0-100";
    case Failure::NoProgress: return "resistance stopped changing";
    case Failure::WrongDirection: return "resistance moved opposite the command";
    case Failure::Motor: return "motor command failed";
    case Failure::Boundary: return "could not confirm resistance boundary";
    case Failure::UnstableResistance: return "resistance did not settle at a stationary position";
  }
  return "unknown";
}

constexpr int FAST_SPEED = 1500;
constexpr int FINE_SPEED = 300;
constexpr int FINE_STEP = 150;
constexpr int TOLERANCE = 20;
// Zero = 1.25 * near anchor - 0.25 * far anchor. An 80-step far
// bracket contributes at most 10 steps of uncertainty to the result.
constexpr int ANCHOR_TOLERANCE = 4 * TOLERANCE;
constexpr uint32_t SETTLE_MS = 1000;
constexpr uint32_t STABLE_READING_TIMEOUT_MS = 5000;
constexpr uint32_t REPORT_TIMEOUT_MS = 3500;
constexpr uint32_t NO_PROGRESS_MS = 10000;
constexpr uint32_t END_TIMEOUT_MS = 120000;

inline bool supportsRange(int minimum, int maximum) { return minimum >= 0 && minimum <= 1 && maximum >= 99 && maximum <= 100; }

// IO owns the motor and clock. Keeping the search independent of ESP32 hardware
// allows replay against quantized sensors with delayed 1 Hz reports.
template <class IO> class Search {
 public:
  explicit Search(IO& io) : io_(io), started_(io.now()), lastReport_(io.now()), lastChange_(io.now()) {
    auto sample = io_.sample();
    valueTimestamp_ = sample.timestamp;
    resistance_ = sample.value;
  }

  bool endpoint(bool upper, int32_t& endpoint) {
    // Every exit stops the motor, including a failed read while it is moving.
    struct StopOnExit {
      IO& io;
      ~StopOnExit() { io.stop(); }
    } stopOnExit{io_};
    failure_ = Failure::Boundary;
    started_ = io_.now();
    int direction = upper ? 1 : -1;
    while (true) {
      if (!stoppedReading()) {
        if (retryable()) continue;
        return false;
      }
      int32_t anchor, edge;
      if (!boundary(upper ? 90 : 10, direction, anchor, ANCHOR_TOLERANCE) || !boundary(upper ? 98 : 2, direction, edge, TOLERANCE)) return false;
      int64_t span = static_cast<int64_t>(edge) - anchor;
      // Average same-direction transition spacing over eight levels to reduce
      // fuzzy-boundary error, then extrapolate two without needing 0/100.
      int64_t result = edge + span * 2 / 8;
      if (span * direction <= 8 * TOLERANCE || result < INT32_MIN || result > INT32_MAX) continue;
      endpoint = static_cast<int32_t>(result);
      failure_ = Failure::None;
      return true;
    }
  }

  Failure failure() const { return failure_; }

 private:
  IO& io_;
  uint32_t started_, lastReport_, lastChange_, valueTimestamp_;
  int resistance_ = 0;
  // Only describes the latest stationary observation, never moving reports.
  int stationaryLow_ = 0, stationaryHigh_ = 0;
  int attemptLow_ = 0, attemptHigh_ = 0;
  bool stationaryBoundary_ = false;
  Failure failure_ = Failure::None;

  bool fail(Failure failure) {
    failure_ = failure;
    return false;
  }

  bool retryable() const {
    // One adjacent pair alone can be stationary analog noise, not a response
    // to commanded motion. Do not let that keep a disconnected motor spinning.
    if (failure_ == Failure::NoProgress) return attemptHigh_ - attemptLow_ > 1 && io_.now() - lastChange_ < NO_PROGRESS_MS;
    return failure_ == Failure::Boundary || failure_ == Failure::UnstableResistance || failure_ == Failure::WrongDirection;
  }

  bool poll(bool& fresh) {
    io_.poll();
    auto sample = io_.sample();
    // Take the clock AFTER the snapshot. The sensor task may publish a report
    // while we wait for its mutex; subtracting it from an earlier clock wraps.
    uint32_t now = io_.now();
    fresh = !sample.simulate && sample.timestamp != valueTimestamp_;
    if (fresh) {
      valueTimestamp_ = sample.timestamp;
      if (resistance_ != sample.value) lastChange_ = sample.timestamp;
      resistance_ = sample.value;
      if (resistance_ < attemptLow_) attemptLow_ = resistance_;
      if (resistance_ > attemptHigh_) attemptHigh_ = resistance_;
      lastReport_ = sample.timestamp;
    }
    if (sample.simulate) return fail(Failure::Simulated);
    if (io_.cancelled()) return fail(Failure::Cancelled);
    if (now - started_ >= END_TIMEOUT_MS) return fail(Failure::Timeout);
    if (now - lastReport_ >= REPORT_TIMEOUT_MS) return fail(Failure::StaleReport);
    if (fresh && (resistance_ < 0 || resistance_ > 100)) return fail(Failure::InvalidResistance);
    return true;
  }

  bool stoppedReading(bool boundaryProbe = false) {
    stationaryBoundary_ = false;
    io_.stop();
    bool fresh;
    do {
      if (!poll(fresh)) return false;
    } while (io_.moving());
    uint32_t stopped = io_.now();
    // Reports can describe motion from 1.3 seconds earlier. Only measurements
    // used to locate an anchor need the longer acquisition guard.
    uint32_t minimumDwell = boundaryProbe ? 2 * SETTLE_MS : SETTLE_MS;
    // Observe during the dwell instead of waiting a second before starting
    // confirmation. A fresh report can confirm an unchanged level after one
    // stationary second. A changed level needs its own confirmation window;
    // back-and-forth between adjacent levels is still valid boundary evidence.
    int candidate = resistance_;
    uint32_t candidateTime = stopped;
    int low = resistance_, high = resistance_, changes = 0;
    uint32_t windowTime = stopped;
    while (true) {
      if (!poll(fresh)) return false;
      if (fresh && static_cast<int32_t>(valueTimestamp_ - stopped) >= 0) {
        if (resistance_ < high - 1 || resistance_ > low + 1) {
          low = high = resistance_;
          changes = 0;
          windowTime = valueTimestamp_;
        } else if (resistance_ != candidate) {
          if (resistance_ < low) low = resistance_;
          if (resistance_ > high) high = resistance_;
          ++changes;
        }
        if (changes >= 2 && valueTimestamp_ - windowTime >= SETTLE_MS && valueTimestamp_ - stopped >= minimumDwell) {
          stationaryLow_ = low;
          stationaryHigh_ = high;
          stationaryBoundary_ = true;
          return true;
        }
        if (resistance_ != candidate) {
          candidate = resistance_;
          candidateTime = valueTimestamp_;
        } else if (valueTimestamp_ - candidateTime >= SETTLE_MS && valueTimestamp_ - stopped >= minimumDwell) {
          return true;
        }
      }
      if (io_.now() - stopped >= STABLE_READING_TIMEOUT_MS) return fail(Failure::UnstableResistance);
    }
  }

  bool move(int32_t position, int speed) {
    stationaryBoundary_ = false;
    if (!io_.moveTo(position, speed)) return fail(Failure::Motor);
    bool fresh;
    do {
      if (!poll(fresh)) return false;
    } while (io_.moving());
    return true;
  }

  bool moveBy(int delta, int speed) {
    int64_t target = static_cast<int64_t>(io_.position()) + delta;
    return target >= INT32_MIN && target <= INT32_MAX && move(static_cast<int32_t>(target), speed);
  }

  bool atBoundary(int target, int direction) const {
    int low = direction < 0 ? target : target - 1;
    return stationaryBoundary_ && stationaryLow_ == low && stationaryHigh_ == low + 1;
  }

  bool boundary(int target, int direction, int32_t& result, int tolerance) {
    io_.setBoundaryTarget(target);
    // A quantized analog crossing can shift between probes. Reacquire it instead
    // of rejecting the whole calibration on one final reading. Keep trying
    // while resistance responds, within the shared endpoint deadline.
    while (true) {
      failure_ = Failure::Boundary;
      if (boundaryAttempt(target, direction, result, tolerance)) return true;
      if (!retryable()) return false;
      do {
        if (stoppedReading()) break;
        if (!retryable()) return false;
      } while (true);
    }
  }

  bool boundaryAttempt(int target, int direction, int32_t& result, int tolerance) {
    attemptLow_ = attemptHigh_ = resistance_;
    if (atBoundary(target, direction)) {
      result = io_.position();
      return true;
    }
    // Approach from two levels inside the range even if startup is at an end.
    // Far travel is continuous; fresh reports bound each decision to ~1 Hz.
    int approach = target - 2 * direction;
    int lastDirection = 0;
    int bestResistance = resistance_;
    bool settled = true;
    int speedLimit = FAST_SPEED;
    int progressTarget = approach;
    auto distanceToTarget = [&]() {
      int distance = resistance_ - progressTarget;
      return distance < 0 ? -distance : distance;
    };
    int bestDistance = distanceToTarget();
    uint32_t progress = io_.now();
    auto progressing = [&]() {
      int distance = distanceToTarget();
      if (distance < bestDistance) {
        bestDistance = distance;
        progress = io_.now();
      }
      return io_.now() - progress < NO_PROGRESS_MS;
    };
    while (true) {
      while ((resistance_ - approach) * direction < 0 || (resistance_ - target) * direction >= 0) {
        if (!progressing()) return fail(Failure::NoProgress);
        int travelDirection = resistance_ < approach ? 1 : -1;
        if (lastDirection && lastDirection != travelDirection) {
          // Drain the sensor's view of the old motion before reversing. A late
          // report from that motion must not look like wrong-way new motion.
          if (!settled && !stoppedReading()) return false;
          if (atBoundary(target, direction)) {
            result = io_.position();
            return true;
          }
          settled = true;
          speedLimit /= 2;
          if (speedLimit < TOLERANCE) speedLimit = TOLERANCE;
          lastDirection = 0;
          continue;  // Recompute direction from the settled resistance.
        }
        if (lastDirection != travelDirection) bestResistance = resistance_;
        lastDirection = travelDirection;
        int error = resistance_ - approach;
        if (error < 0) error = -error;
        // Brake before the next 1 Hz report can carry us across the anchor.
        int speed = error > 3 ? (error - 2) * FINE_STEP / 2 : FINE_SPEED;
        if (speed < FINE_SPEED) speed = FINE_SPEED;
        if (speed > speedLimit) speed = speedLimit;
        int64_t next = static_cast<int64_t>(io_.position()) + travelDirection * speed * 2;
        if (next < INT32_MIN || next > INT32_MAX || !io_.moveTo(static_cast<int32_t>(next), speed)) return fail(Failure::Motor);
        settled = false;
        stationaryBoundary_ = false;
        bool fresh;
        do {
          if (!poll(fresh)) return false;
        } while (!fresh);
        if ((resistance_ - bestResistance) * travelDirection < -1) return fail(Failure::WrongDirection);
        if ((resistance_ - bestResistance) * travelDirection > 0) bestResistance = resistance_;
      }
      if (!settled && !stoppedReading(true)) return false;
      if (atBoundary(target, direction)) {
        result = io_.position();
        return true;
      }
      settled = true;
      if ((resistance_ - target) * direction < 0) break;
      // The real bike reported 6 while stopping, then 5 at the same position.
      // Back out and confirm an interior position instead of failing or treating
      // that late sample as an accurately located transition. Keep the existing
      // timeout/progress budget and reduce speed on reversal.
    }

    int32_t before = io_.position();
    bestResistance = resistance_;
    progress = io_.now();
    progressTarget = target;
    bestDistance = distanceToTarget();
    while ((resistance_ - target) * direction < 0) {
      if (!progressing()) return fail(Failure::NoProgress);
      before = io_.position();
      // Unlike the continuous approach, this move is capped at 150 steps and
      // always followed by a complete stationary reading. Finish it promptly
      // instead of spending a second crawling before each settling pause.
      if (!moveBy(direction * FINE_STEP, FAST_SPEED) || !stoppedReading(true)) return false;
      if (atBoundary(target, direction)) {
        result = io_.position();
        return true;
      }
      if ((resistance_ - bestResistance) * direction < -1) return fail(Failure::WrongDirection);
      if ((resistance_ - bestResistance) * direction > 0) bestResistance = resistance_;
    }
    int32_t after = io_.position();
    // Re-approach each trial from the original interior position so backlash
    // is taken up in the same direction for every measured crossing.
    int64_t backoff = static_cast<int64_t>(before) - direction * FINE_STEP;
    if (backoff < INT32_MIN || backoff > INT32_MAX) return false;
    const int32_t interior = static_cast<int32_t>(backoff);
    // These are bounded repositioning moves inside an already measured region.
    // Reporting latency is handled by stopping and settling at every probe.
    while ((static_cast<int64_t>(after) - before) * direction > tolerance) {
      int32_t middle = before + (after - before) / 2;
      if (!move(interior, FAST_SPEED) || !move(middle, FAST_SPEED) || !stoppedReading(true)) return false;
      if (atBoundary(target, direction)) {
        result = io_.position();
        return true;
      }
      if ((resistance_ - target) * direction >= 0) after = middle;
      else before = middle;
    }
    if (io_.position() != after && (!move(interior, FAST_SPEED) || !move(after, FAST_SPEED) || !stoppedReading(true))) return false;
    if (atBoundary(target, direction)) {
      result = io_.position();
      return true;
    }
    if (resistance_ != target) return false;  // Reacquire a shifted crossing; never invent a skipped level.
    result = before + (after - before) / 2;
    return true;
  }
};
}  // namespace FtmsHoming
