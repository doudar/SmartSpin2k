/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <stdint.h>
#include "FtmsCalibration.h"
#include "ResistanceControl.h"

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
  explicit Search(IO& io, int shiftStep = 1200, float sensitivity = 3) : io_(io), started_(io.now()), lastReport_(io.now()), lastChange_(io.now()),
                                                                     shiftStep_(shiftStep), sensitivity_(sensitivity) {
    auto sample = io_.sample();
    valueTimestamp_ = sample.timestamp;
    resistance_ = sample.value;
  }

  bool endpoint(bool upper, int32_t& endpoint) {
    // Every exit stops the motor, including a failed read while it is moving.
    StopOnExit stopOnExit{io_};
    failure_ = Failure::Boundary;
    started_ = io_.now();
    int direction = upper ? 1 : -1;
    while (true) {
      if (!stoppedReading()) return false;
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

  // Outer points sample near the requested level; the middle is the shared
  // downward crossing used to recover coordinates on every startup.
  bool reference(int target, int32_t& position, uint8_t& level2, int stepsPerLevel) {
    StopOnExit stopOnExit{io_};
    started_ = io_.now();
    failure_ = Failure::Boundary;
    if (target == FtmsCalibration::REFERENCE_LEVEL) {
      if (!middleReference(position, stepsPerLevel)) return false;
      level2 = FtmsCalibration::REFERENCE_LEVEL2;
      failure_ = Failure::None;
      return true;
    }
    io_.setBoundaryTarget(target);
    if (stepsPerLevel < 20) return fail(Failure::Boundary);
    if (!observationReady_ || observationPosition_ != io_.position() || io_.now() - valueTimestamp_ > FtmsCalibration::FRESH_MS) {
      if (!stationaryObservation()) return false;
    }
    int lastObservation = observationLevel2();
    int64_t unresponsiveTravel = 0;
    while (true) {
      const int observed = observationLevel2();
      int error = observed - 2 * target;
      if (error < 0) error = -error;
      if (error <= 4) {
        position = io_.position();
        level2 = static_cast<uint8_t>(observed);
        break;
      }
      const int32_t previous = io_.position();
      const int32_t next = resistanceController_.update(previous, (observed + 1) / 2, target, valueTimestamp_, io_.now(), 6000, sensitivity_, stepsPerLevel);
      if (!move(next, REFERENCE_SPEED) || !stationaryObservation()) return false;
      unresponsiveTravel += std::abs(static_cast<int64_t>(io_.position()) - previous);
      if (std::abs(observationLevel2() - lastObservation) > 2) {
        unresponsiveTravel = 0;
        lastObservation = observationLevel2();
      }
      if (unresponsiveTravel >= MAX_PLATEAU_STEPS) return fail(Failure::NoProgress);
    }
    failure_ = Failure::None;
    return true;
  }

  bool recover(const FtmsCalibration::Map& map, int32_t& origin) {
    StopOnExit stopOnExit{io_};
    if (!map.valid()) return fail(Failure::Boundary);
    const int scale = static_cast<int>(2LL * (map.position[2] - map.position[0]) / (map.level2[2] - map.level2[0]));
    int32_t crossing;
    uint8_t level2;
    // Use the identical search as full calibration, even when booted in-map.
    // The returned bracket midpoint can differ from the final motor position.
    if (!reference(FtmsCalibration::REFERENCE_LEVEL, crossing, level2, scale)) return false;
    int64_t zero = static_cast<int64_t>(crossing) - map.position[1];
    if (zero < INT32_MIN || zero > INT32_MAX) return fail(Failure::Boundary);
    origin = static_cast<int32_t>(zero);
    failure_ = Failure::None;
    return true;
  }

 private:
  struct StopOnExit {
    IO& io;
    ~StopOnExit() { io.stop(); }
  };

  IO& io_;
  uint32_t started_, lastReport_, lastChange_, valueTimestamp_;
  int shiftStep_;
  float sensitivity_;
  ResistanceControl::Controller resistanceController_;
  int resistance_ = 0;
  // Only describes the latest stationary observation, never moving reports.
  int stationaryLow_ = 0, stationaryHigh_ = 0;
  int attemptStartResistance_ = 0;
  bool stationaryBoundary_ = false;
  bool observationReady_ = false;
  int32_t observationPosition_ = 0;
  int observation2_ = 0;
  Failure failure_ = Failure::None;
  int lastMoveDirection_ = 0;
  bool directionalReference_ = false;
  int referenceScale_ = 0;

  bool middleReference(int32_t& position, int stepsPerLevel) {
    // Clear the reference by several levels before approaching downward. This
    // also takes up play when startup begins below (or exactly at) the crossing.
    int32_t staging;
    uint8_t observed;
    if (!reference(FtmsCalibration::REFERENCE_LEVEL + 8, staging, observed, stepsPerLevel)) return false;
    directionalReference_ = true;
    referenceScale_ = stepsPerLevel;
    const int backoff = static_cast<int>(std::min<int64_t>(6000, 5LL * stepsPerLevel));
    const bool found = stoppedReading(true) && boundary(FtmsCalibration::REFERENCE_LEVEL, -1, position, ANCHOR_TOLERANCE, backoff);
    directionalReference_ = false;
    return found;
  }

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
    if (now - started_ >= END_TIMEOUT_MS) return fail(Failure::Timeout);
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
    if (position != io_.position()) lastMoveDirection_ = position > io_.position() ? 1 : -1;
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
    return stationaryBoundary_ && stationaryLow_ == low && stationaryHigh_ == low + 1 &&
           (!directionalReference_ || lastMoveDirection_ == direction);
  }

  int observationLevel2() const { return observation2_; }

  // Hold the motor for at least two seconds and confirm fresh stationary feedback.
  // Adjacent quantization noise is represented by a half level. Larger changes
  // restart confirmation; persistently noisy feedback still gets an average.
  bool stationaryObservation() {
    observationReady_ = false;
    io_.stop();
    bool fresh;
    do { if (!poll(fresh)) return false; } while (io_.moving());
    const uint32_t stopped = io_.now();
    uint32_t firstTime = 0, windowTime = 0;
    int first = 0, low = 0, high = 0, changes = 0;
    int sampleSum = 0, sampleCount = 0;
    int recent[2] = {};
    bool haveFirst = false;
    while (true) {
      if (!poll(fresh)) return false;
      if (fresh && static_cast<int32_t>(valueTimestamp_ - stopped) >= 0) {
        sampleSum -= recent[sampleCount % 2];
        recent[sampleCount % 2] = resistance_;
        sampleSum += resistance_;
        ++sampleCount;
        if (!haveFirst || resistance_ < high - 1 || resistance_ > low + 1) {
          low = high = resistance_;
          changes = 0;
          windowTime = valueTimestamp_;
        } else if (resistance_ != first) {
          low = std::min(low, resistance_);
          high = std::max(high, resistance_);
          ++changes;
        }
        if (!haveFirst || resistance_ != first) {
          first = resistance_;
          firstTime = valueTimestamp_;
          haveFirst = true;
        }
        const bool stable = valueTimestamp_ - firstTime >= SETTLE_MS;
        const bool adjacentJitter = changes >= 2 && valueTimestamp_ - windowTime >= SETTLE_MS;
        if ((stable || adjacentJitter) && valueTimestamp_ - stopped >= 2 * SETTLE_MS) {
          stationaryLow_ = stable ? resistance_ : low;
          stationaryHigh_ = stable ? resistance_ : high;
          stationaryBoundary_ = stationaryLow_ != stationaryHigh_;
          observation2_ = stationaryLow_ + stationaryHigh_;
          observationPosition_ = io_.position();
          observationReady_ = true;
          return true;
        }
      }
      if (io_.now() - stopped >= STABLE_READING_TIMEOUT_MS && sampleCount) {
        observation2_ = (2 * sampleSum + std::min(sampleCount, 2) / 2) / std::min(sampleCount, 2);
        observationPosition_ = io_.position();
        observationReady_ = true;
        return true;
      }
    }
  }

  bool seekResistance(int target, int speed) {
    observationReady_ = false;
    stationaryBoundary_ = false;
    const int initialError = target - resistance_;
    int progressResistance = resistance_;
    int32_t progressPosition = io_.position();
    uint32_t lastCommand = io_.now() - 10;
    while (true) {
      bool fresh;
      if (!poll(fresh)) return false;
      const int error = target - resistance_;
      if ((error == 0) || (initialError > 0 && error < 0) || (initialError < 0 && error > 0)) {
        io_.stop();
        return true;
      }
      if (fresh && std::abs(resistance_ - progressResistance) > 2) {
        progressResistance = resistance_;
        progressPosition = io_.position();
      }
      const int64_t travel = static_cast<int64_t>(io_.position()) - progressPosition;
      if (travel >= MAX_PLATEAU_STEPS || travel <= -MAX_PLATEAU_STEPS) {
        if (!stationaryObservation()) return false;
        if (std::abs(resistance_ - progressResistance) <= 2) return fail(Failure::NoProgress);
        progressResistance = resistance_;
        progressPosition = io_.position();
        continue;
      }
      // Match normal maintenance's control cadence, rather than waiting for a
      // complete stop and dwell after every incremental motor target.
      if (io_.now() - lastCommand >= 10) {
        const int32_t next = resistanceController_.update(io_.position(), resistance_, target, valueTimestamp_, io_.now(), shiftStep_, sensitivity_);
        if (next != io_.position()) lastMoveDirection_ = next > io_.position() ? 1 : -1;
        if (!io_.moveTo(next, std::abs(error) <= 3 ? std::min(speed, FINE_SPEED) : speed)) return fail(Failure::Motor);
        lastCommand = io_.now();
      }
    }
  }

  bool boundary(int target, int direction, int32_t& result, int tolerance, int backoffSteps = FINE_STEP) {
    io_.setBoundaryTarget(target);
    // A quantized analog crossing can shift between probes. Reacquire it instead
    // of rejecting the whole calibration on one final reading. Keep trying
    // while resistance responds, within the shared endpoint deadline.
    while (true) {
      failure_ = Failure::Boundary;
      if (boundaryAttempt(target, direction, result, tolerance, backoffSteps)) return true;
      if (!retryable()) return false;
      if (!stoppedReading(directionalReference_)) return false;
    }
  }

  bool boundaryAttempt(int target, int direction, int32_t& result, int tolerance, int backoffSteps) {
    attemptStartResistance_ = resistance_;
    if (atBoundary(target, direction)) {
      result = io_.position();
      return true;
    }
    // Use the same live resistance controller to reach the interior side.
    // Only the final transition bracket needs precision probes.
    const int approach = target - 2 * direction;
    int approachSpeed = FAST_SPEED;
    int progressResistance = resistance_;
    int32_t approachProgressPosition = io_.position();
    while ((resistance_ - approach) * direction < 0 || (resistance_ - target) * direction >= 0) {
      if (directionalReference_) {
        // Moving reports lag the brake. This reference uses only completed
        // moves and two-second stationary observations, including its approach.
        const int64_t estimate = static_cast<int64_t>(approach - resistance_) * referenceScale_;
        const int delta = static_cast<int>(std::max<int64_t>(-6000, std::min<int64_t>(6000, estimate)));
        if (!moveBy(delta, REFERENCE_SPEED) || !stoppedReading(true)) return false;
        if (std::abs(resistance_ - progressResistance) > 2) {
          progressResistance = resistance_;
          approachProgressPosition = io_.position();
        } else if (std::abs(static_cast<int64_t>(io_.position()) - approachProgressPosition) >= MAX_PLATEAU_STEPS) {
          return fail(Failure::NoProgress);
        }
      } else if (!seekResistance(approach, approachSpeed) || !stoppedReading(true)) return false;
      if (atBoundary(target, direction)) { result = io_.position(); return true; }
      approachSpeed = std::max(TOLERANCE, approachSpeed / 2);
    }
    int bestResistance = resistance_;
    uint32_t progress = io_.now();
    int bestDistance = std::abs(resistance_ - target);
    auto distanceToTarget = [&]() { return std::abs(resistance_ - target); };
    auto progressing = [&]() {
      int distance = distanceToTarget();
      if (distance < bestDistance) { bestDistance = distance; progress = io_.now(); }
      return io_.now() - progress < NO_PROGRESS_MS;
    };

    int32_t before = io_.position();
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
    int64_t backoff = static_cast<int64_t>(before) - direction * backoffSteps;
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
