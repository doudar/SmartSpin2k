/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <stdint.h>
#include "FtmsCalibration.h"

namespace FtmsHoming {
enum class Failure { None, Cancelled, Simulated, StaleReport, Timeout, InvalidResistance, NoProgress, WrongDirection, Motor, Boundary };

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
constexpr int MAX_PLATEAU_STEPS = 3000;
constexpr int MAX_PROBE_STEP = 600;
constexpr int REFERENCE_SPEED = 2500;

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
    timeout_ = END_TIMEOUT_MS;
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

  // Sample near the requested level, storing what the sensor actually reports.
  // A skipped integer or a moving analog boundary must not cause fine bisection.
  bool reference(int target, int32_t& position, uint8_t& level2, int stepsPerLevel = FINE_STEP) {
    struct StopOnExit { IO& io; ~StopOnExit() { io.stop(); } } stopOnExit{io_};
    started_ = io_.now();
    timeout_ = END_TIMEOUT_MS;
    failure_ = Failure::Boundary;
    io_.setBoundaryTarget(target);
    if (stepsPerLevel < 20) return fail(Failure::Boundary);
    if (!observationReady_ || observationPosition_ != io_.position() || io_.now() - valueTimestamp_ > FtmsCalibration::FRESH_MS) {
      if (!stationaryObservation()) return false;
    }
    int bestError = 201;
    int32_t progressPosition = io_.position();
    while (true) {
      const int observed = observationLevel2();
      int error = observed - 2 * target;
      if (error < 0) error = -error;
      if (error <= 4) {
        position = io_.position();
        level2 = static_cast<uint8_t>(observed);
        break;
      }
      if (error < bestError) { bestError = error; progressPosition = io_.position(); }
      const int64_t travel = static_cast<int64_t>(io_.position()) - progressPosition;
      if (travel >= MAX_PLATEAU_STEPS || travel <= -MAX_PLATEAU_STEPS) return fail(Failure::NoProgress);
      int64_t delta = static_cast<int64_t>(2 * target - observed) * stepsPerLevel / 2;
      if (delta > 6000) delta = 6000;
      if (delta < -6000) delta = -6000;
      const int64_t next = static_cast<int64_t>(io_.position()) + delta;
      if (next < INT32_MIN || next > INT32_MAX - FINE_STEP) return fail(Failure::Motor);
      if (delta > 0 && !move(static_cast<int32_t>(next) + FINE_STEP, REFERENCE_SPEED)) return false;
      if (!move(static_cast<int32_t>(next), REFERENCE_SPEED) || !stationaryObservation()) return false;
    }
    failure_ = Failure::None;
    return true;
  }

  bool recover(const FtmsCalibration::Map& map, int32_t& origin) {
    struct StopOnExit { IO& io; ~StopOnExit() { io.stop(); } } stopOnExit{io_};
    if (!map.valid()) return fail(Failure::Boundary);
    started_ = io_.now();
    // Twenty seconds is a performance target, not a reason to discard fresh
    // responsive feedback. No-response travel and the overall safety deadline
    // still bound recovery when the brake does not follow the requested moves.
    timeout_ = END_TIMEOUT_MS;
    if (!stationaryObservation()) return false;
    int32_t coordinate, uncertainty;
    // If booted outside the trustworthy middle, move toward 50 in bounded
    // chunks. Stop as soon as a stationary reading enters the measured map;
    // do not spend the remaining startup budget refining an unnecessary edge.
    int64_t unresponsiveTravel = 0;
    while (!map.estimateHalf(observationLevel2(), coordinate, uncertainty)) {
      const int before = observationLevel2();
      int64_t delta = static_cast<int64_t>(100 - before) * (map.position[FtmsCalibration::COUNT - 1] - map.position[0]) /
                      (map.level2[FtmsCalibration::COUNT - 1] - map.level2[0]);
      if (delta > 6000) delta = 6000;
      if (delta < -6000) delta = -6000;
      if (!moveBy(static_cast<int>(delta), REFERENCE_SPEED) || !stationaryObservation()) return false;
      const int progress = (observationLevel2() - before) * (delta > 0 ? 1 : -1);
      unresponsiveTravel += delta < 0 ? -delta : delta;
      if (progress > 2) unresponsiveTravel = 0;
      if (unresponsiveTravel >= MAX_PLATEAU_STEPS) return fail(Failure::NoProgress);
    }
    int64_t zero = static_cast<int64_t>(io_.position()) - coordinate;
    if (zero < INT32_MIN || zero > INT32_MAX) return fail(Failure::Boundary);
    origin = static_cast<int32_t>(zero);
    failure_ = Failure::None;
    return true;
  }

 private:
  IO& io_;
  uint32_t started_, lastReport_, lastChange_, valueTimestamp_;
  uint32_t timeout_ = END_TIMEOUT_MS;
  int resistance_ = 0;
  // Only describes the latest stationary observation, never moving reports.
  int stationaryLow_ = 0, stationaryHigh_ = 0;
  int attemptStartResistance_ = 0;
  bool stationaryBoundary_ = false;
  bool observationReady_ = false;
  int32_t observationPosition_ = 0;
  int observation2_ = 0;
  Failure failure_ = Failure::None;

  bool fail(Failure failure) {
    failure_ = failure;
    return false;
  }

  bool retryable() const {
    // One adjacent pair alone can be stationary analog noise, not a response
    // to commanded motion. Do not let that keep a disconnected motor spinning.
    if (failure_ == Failure::NoProgress) {
      int response = resistance_ - attemptStartResistance_;
      if (response < 0) response = -response;
      return response > 2 && io_.now() - lastChange_ < NO_PROGRESS_MS;
    }
    return failure_ == Failure::Boundary || failure_ == Failure::WrongDirection;
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
      lastReport_ = sample.timestamp;
    }
    if (sample.simulate) return fail(Failure::Simulated);
    if (io_.cancelled()) return fail(Failure::Cancelled);
    if (now - started_ >= timeout_) return fail(Failure::Timeout);
    if (now - lastReport_ >= REPORT_TIMEOUT_MS) return fail(Failure::StaleReport);
    if (fresh && (resistance_ < 0 || resistance_ > 100)) return fail(Failure::InvalidResistance);
    return true;
  }

  bool stoppedReading(bool boundaryProbe = false) {
    observationReady_ = false;
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
    int sampleSum = 0, sampleCount = 0;
    uint32_t windowTime = stopped;
    while (true) {
      if (!poll(fresh)) return false;
      if (fresh && static_cast<int32_t>(valueTimestamp_ - stopped) >= 0) {
        if (valueTimestamp_ - stopped >= minimumDwell) { sampleSum += resistance_; ++sampleCount; }
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
      if (io_.now() - stopped >= STABLE_READING_TIMEOUT_MS && sampleCount) {
        // Fresh but noisy feedback is usable evidence. Keep a bounded average
        // instead of repeatedly restarting the same stationary observation.
        resistance_ = (sampleSum + sampleCount / 2) / sampleCount;
        return true;
      }
    }
  }

  bool move(int32_t position, int speed) {
    observationReady_ = false;
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

  int observationLevel2() const { return observation2_; }

  // Two fresh stationary reports AFTER the acquisition guard. Repeated old
  // reports during the first two seconds are not evidence that the brake has
  // settled. Adjacent quantization noise is represented by a half level.
  bool stationaryObservation() {
    observationReady_ = false;
    io_.stop();
    bool fresh;
    do { if (!poll(fresh)) return false; } while (io_.moving());
    const uint32_t stopped = io_.now();
    uint32_t firstTime = 0;
    int first = 0;
    int sampleSum = 0, sampleCount = 0;
    bool haveFirst = false;
    while (true) {
      if (!poll(fresh)) return false;
      if (fresh && valueTimestamp_ - stopped >= 2 * SETTLE_MS) {
        sampleSum += resistance_;
        ++sampleCount;
        if (!haveFirst || resistance_ < first - 1 || resistance_ > first + 1) {
          first = resistance_;
          firstTime = valueTimestamp_;
          haveFirst = true;
        } else if (valueTimestamp_ - firstTime >= SETTLE_MS) {
          stationaryLow_ = first < resistance_ ? first : resistance_;
          stationaryHigh_ = first > resistance_ ? first : resistance_;
          stationaryBoundary_ = stationaryLow_ != stationaryHigh_;
          observation2_ = first + resistance_;
          observationPosition_ = io_.position();
          observationReady_ = true;
          return true;
        }
      }
      if (io_.now() - stopped >= STABLE_READING_TIMEOUT_MS && sampleCount) {
        observation2_ = (2 * sampleSum + sampleCount / 2) / sampleCount;
        observationPosition_ = io_.position();
        observationReady_ = true;
        return true;
      }
    }
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
    attemptStartResistance_ = resistance_;
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
        if ((resistance_ - bestResistance) * travelDirection < -2) return fail(Failure::WrongDirection);
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
    int32_t progressPosition = io_.position();
    int probeStep = FINE_STEP;
    while ((resistance_ - target) * direction < 0) {
      // Settling a short probe consumes seconds without testing much travel.
      // A wide quantized bin (notably level 4) must get a bounded distance test,
      // rather than fail after just 600 steps of movement and 10s of dwell.
      int distance = distanceToTarget();
      if (distance < bestDistance) {
        progressPosition = io_.position();
        probeStep = FINE_STEP;
      }
      int64_t travelled = (static_cast<int64_t>(io_.position()) - progressPosition) * direction;
      if (!progressing() && travelled >= MAX_PLATEAU_STEPS) return fail(Failure::NoProgress);
      if (travelled < MAX_PLATEAU_STEPS && probeStep > MAX_PLATEAU_STEPS - travelled) probeStep = static_cast<int>(MAX_PLATEAU_STEPS - travelled);
      before = io_.position();
      // Bounded probes grow only while a level remains unchanged; every probe
      // still gets a complete stationary reading before another move.
      int previousResistance = resistance_;
      if (!moveBy(direction * probeStep, FAST_SPEED) || !stoppedReading(true)) return false;
      if (resistance_ == previousResistance && probeStep < MAX_PROBE_STEP) probeStep *= 2;
      if (atBoundary(target, direction)) {
        result = io_.position();
        return true;
      }
      if ((resistance_ - bestResistance) * direction < -2) return fail(Failure::WrongDirection);
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
    // The bracket already crosses the requested threshold. Skipped integers
    // and shifted/noisy final reports do not justify another move and dwell.
    result = before + (after - before) / 2;
    return true;
  }
};
}  // namespace FtmsHoming
