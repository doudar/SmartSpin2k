/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>
#include <cmath>
#include <deque>
#include <initializer_list>
#include <stdio.h>
#include <thread>
#include "FtmsHoming.h"
#include "SmartSpin_parameters.h"
#include "test.h"

namespace {
struct Bike {
  Measurement::ValueSample feedback{0, 0, false};
  uint32_t clock = 0, nextReport = 1000, stopReports = UINT32_MAX, abortAt = UINT32_MAX;
  double pos = 5000;
  int32_t target = 5000;
  int speed = 0;
  uint32_t firstMoveTime = UINT32_MAX;
  int acceleration = 0;
  double velocity = 0;
  int pending = 50;
  bool legacy = false, stuck = false, skipTwo = false;
  bool usedFast = false;
  bool simulated = false;
  bool reverseResponse = false;
  bool jitter = false;
  int jitterSpan = 1;
  uint32_t jitterStartMs = 4000;
  uint32_t jitterEndMs = UINT32_MAX, skipTwoUntilMs = UINT32_MAX;
  bool boundaryNoise = false;
  int noisyReports = 0, noisyEdgeReports = 0;
  bool motorFailure = false;
  bool shiftCrossing = false, crossingShifted = false;
  int32_t firstCrossingPosition = INT32_MIN;
  bool spikeOnStop = false, spikePending = false;
  uint32_t stoppedAt = 0;
  int spikes = 0;
  bool publishDuringRead = false;
  uint32_t observedTimestamp = 0;
  uint32_t clockOffset = 0;
  uint32_t reportLagMs = 200;
  int stepsPerUnit = 100;
  std::deque<std::pair<uint32_t, double>> positions;
  int delayedCrossings = 0;
  std::deque<int> boundaryTargets;
  void setBoundaryTarget(int target) { boundaryTargets.push_back(target); }
  uint32_t now() { return clock + clockOffset; }
  Measurement::ValueSample sample() {
    // Emulate the sensor task preempting homing between reading millis() and
    // acquiring the measurement snapshot. Its timestamp is then newer than
    // that earlier clock read, despite being a perfectly fresh report.
    if (publishDuringRead && feedback.timestamp != observedTimestamp) {
      clock += 5;
      feedback.timestamp = now();
      observedTimestamp = feedback.timestamp;
    }
    return feedback;
  }
  int32_t position() { return static_cast<int32_t>(std::round(pos)); }
  bool moving() { return std::abs(pos - target) > 0.001; }
  bool cancelled() { return clock >= abortAt; }
  void stop() {
    if ((feedback.value == 11 && pos < 10.5 * stepsPerUnit) || (feedback.value == 89 && pos >= 89.5 * stepsPerUnit)) ++delayedCrossings;
    target = position();
    pos = target;
    velocity = 0;
    if (shiftCrossing && firstCrossingPosition != INT32_MIN && target == firstCrossingPosition) crossingShifted = true;
    stoppedAt = clock;
    spikePending = spikeOnStop;
  }
  bool moveTo(int32_t value, int hz) {
    if (motorFailure) return false;
    if (firstMoveTime == UINT32_MAX) firstMoveTime = clock;
    spikePending = false;
    target = value;
    speed = hz;
    usedFast |= hz == FtmsHoming::FAST_SPEED;
    return true;
  }
  void poll() {
    if (positions.empty()) positions.push_back({clock, pos});
    clock += 5;
    if (acceleration) {
      double distance = target - pos;
      double desired = std::fmin(speed, std::sqrt(2 * acceleration * std::abs(distance)));
      if (distance < 0) desired = -desired;
      double previous = velocity;
      double change = acceleration * 0.005;
      velocity += std::fmax(-change, std::fmin(change, desired - velocity));
      double delta = (previous + velocity) * 0.005 / 2;
      if (delta * distance >= 0 && std::abs(delta) >= std::abs(distance)) {
        pos = target;
        velocity = 0;
      } else {
        pos += delta;
      }
    } else {
      double delta = speed * 0.005;
      if (pos < target) pos = std::fmin(pos + delta, target);
      else if (pos > target) pos = std::fmax(pos - delta, target);
    }
    positions.push_back({clock, pos});
    while (positions.size() > 1 && positions[1].first + reportLagMs <= clock) positions.pop_front();
    if (clock >= nextReport) {
      // Preserve acquisition delay after the motor stops, as in the user's
      // 2026-09-17 log: R=6 at rest later becomes R=5 at that same position.
      double reportedPosition = positions.front().second + (crossingShifted ? 80 : 0);
      pending = stuck ? 50 : static_cast<int>(std::floor((reverseResponse ? 100 * stepsPerUnit - reportedPosition : reportedPosition) / stepsPerUnit + 0.5));
      if (jitter && clock >= jitterStartMs && clock < jitterEndMs) pending = 50 + jitterSpan * ((nextReport / 1000) % 2);
      if (boundaryNoise && !moving()) {
        double units = reportedPosition / stepsPerUnit;
        int lower = static_cast<int>(std::floor(units));
        if (std::abs(units - lower - 0.5) * stepsPerUnit <= 12) {
          pending = lower + ((nextReport / 1000) % 2);
          ++noisyReports;
          if (lower == 2 || lower == 97) ++noisyEdgeReports;
        }
      }
      if (shiftCrossing && firstCrossingPosition == INT32_MIN && !moving() && pending == 10) firstCrossingPosition = position();
      if (pending < (legacy ? 1 : 0)) pending = legacy ? 1 : 0;
      if (pending > (legacy ? 99 : 100)) pending = legacy ? 99 : 100;
      if (skipTwo && clock < skipTwoUntilMs && pending == 2) pending = 1;
      if (spikePending && !moving() && clock - stoppedAt >= 1000 && pending >= 3 && pending <= 13) {
        pending -= 2;  // A wider outlier is not adjacent boundary dithering.
        spikePending = false;
        ++spikes;
      }
      if (clock < stopReports) feedback = {pending, now(), simulated};
      nextReport += 1000;
    }
  }
};
}  // namespace

void TestFtmsHoming::test_repeatable_startup() {
  int32_t first = INT32_MIN;
  uint32_t fastest = UINT32_MAX, slowest = 0;
  for (bool legacy : {false, true}) {
    for (int start : {0, 150, 550, 5000, 10000}) {
      for (uint32_t phase : {800u, 1250u}) {
        Bike bike;
        bike.legacy = legacy;
        bike.pos = bike.target = start;
        bike.nextReport = phase;
        FtmsHoming::Search<Bike> search(bike);
        int32_t zero;
        bool success = search.endpoint(false, zero);
        char context[120];
        snprintf(context, sizeof(context), "legacy=%d start=%d phase=%u time=%u pos=%d", legacy, start, phase, bike.clock, bike.position());
        TEST_ASSERT_TRUE_MESSAGE(success, context);
        // Rounded reports put the 3->2 edge at 250 and 11->10 at 1050.
        // Extrapolated virtual zero is 50, not an assertion of physical zero.
        TEST_ASSERT_INT32_WITHIN(25, 50, zero);
        if (first == INT32_MIN) first = zero;
        TEST_ASSERT_INT32_WITHIN(25, first, zero);
        TEST_ASSERT_FALSE(bike.moving());
        TEST_ASSERT_LESS_THAN_UINT32(90000, bike.clock);
        if (bike.clock < fastest) fastest = bike.clock;
        if (bike.clock > slowest) slowest = bike.clock;
        if (start == 10000) TEST_ASSERT_TRUE(bike.usedFast);
      }
    }
  }
  printf("FTMS simulated startup: %u-%u ms (100 steps/unit, delayed 1 Hz reports)\n", fastest, slowest);
}

void TestFtmsHoming::test_both_ends_and_legacy() {
  for (bool legacy : {false, true}) {
    Bike bike;
    bike.legacy = legacy;
    FtmsHoming::Search<Bike> search(bike);
    int32_t zero, maximum;
    TEST_ASSERT_TRUE(search.endpoint(false, zero));
    TEST_ASSERT_TRUE(search.endpoint(true, maximum));
    TEST_ASSERT_INT32_WITHIN(25, 50, zero);
    TEST_ASSERT_INT32_WITHIN(25, 9950, maximum);
    TEST_ASSERT_INT32_WITHIN(50, 9900, maximum - zero);
    TEST_ASSERT_EQUAL_UINT32(4, bike.boundaryTargets.size());
    TEST_ASSERT_EQUAL_INT(10, bike.boundaryTargets[0]);
    TEST_ASSERT_EQUAL_INT(2, bike.boundaryTargets[1]);
    TEST_ASSERT_EQUAL_INT(90, bike.boundaryTargets[2]);
    TEST_ASSERT_EQUAL_INT(98, bike.boundaryTargets[3]);
  }
}

void TestFtmsHoming::test_missing_stuck_and_skipped_reports() {
  for (int fault = 0; fault < 7; ++fault) {
    Bike bike;
    if (fault == 0) bike.stopReports = 0;
    if (fault == 1) bike.stopReports = 5000;
    if (fault == 2) bike.stuck = true;
    if (fault == 3) bike.skipTwo = true;
    if (fault == 4) bike.simulated = true;
    if (fault == 5) bike.jitter = bike.stuck = true;
    if (fault == 6) bike.motorFailure = true;
    FtmsHoming::Search<Bike> search(bike);
    int32_t zero = INT32_MIN;
    TEST_ASSERT_FALSE(search.endpoint(false, zero));
    TEST_ASSERT_FALSE(bike.moving());
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, zero);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(FtmsHoming::END_TIMEOUT_MS, bike.clock);
    if (fault < 2) TEST_ASSERT_LESS_OR_EQUAL_UINT32(bike.stopReports + FtmsHoming::REPORT_TIMEOUT_MS, bike.clock);
    if (fault == 3) TEST_ASSERT_EQUAL_INT(static_cast<int>(FtmsHoming::Failure::Timeout), static_cast<int>(search.failure()));
    if (fault == 6) TEST_ASSERT_EQUAL_INT(static_cast<int>(FtmsHoming::Failure::Motor), static_cast<int>(search.failure()));
    if (fault == 5) {
      TEST_ASSERT_EQUAL_INT(static_cast<int>(FtmsHoming::Failure::NoProgress), static_cast<int>(search.failure()));
      TEST_ASSERT_LESS_THAN_UINT32(15000, bike.clock);
    }
  }
}

void TestFtmsHoming::test_abort_and_feedback() {
  Bike aborted;
  aborted.abortAt = 20000;
  FtmsHoming::Search<Bike> search(aborted);
  int32_t zero = INT32_MIN;
  TEST_ASSERT_FALSE(search.endpoint(false, zero));
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, zero);
  TEST_ASSERT_EQUAL_UINT32(20000, aborted.clock);

  // Crossing millis() rollover must not turn fresh reports into stale ones.
  Bike wrapped;
  wrapped.clockOffset = UINT32_MAX - 10000;
  FtmsHoming::Search<Bike> wrappedSearch(wrapped);
  TEST_ASSERT_TRUE(wrappedSearch.endpoint(false, zero));
  TEST_ASSERT_INT32_WITHIN(25, 50, zero);
}

void TestFtmsHoming::test_measurement_value_timer() {
  Measurement measurement;
  TEST_ASSERT_EQUAL_UINT32(0, measurement.getValueTimestamp());
  measurement.setValue(5, false);
  auto first = measurement.getValueSample();
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  measurement.setTarget(50);
  TEST_ASSERT_NOT_EQUAL(first.timestamp, static_cast<uint32_t>(measurement.getTimestamp()));
  TEST_ASSERT_EQUAL_UINT32(first.timestamp, measurement.getValueTimestamp());
  measurement.setSimulate(true);
  TEST_ASSERT_EQUAL_UINT32(first.timestamp, measurement.getValueTimestamp());
  TEST_ASSERT_TRUE(measurement.getValueSample().simulate);
  // Equal values are still new reports; source changes travel with the value.
  measurement.setValue(5, false);
  auto second = measurement.getValueSample();
  TEST_ASSERT_NOT_EQUAL(first.timestamp, second.timestamp);
  TEST_ASSERT_EQUAL_INT(5, second.value);
  TEST_ASSERT_FALSE(second.simulate);
  TEST_ASSERT_EQUAL_UINT32(second.timestamp, measurement.getTimestamp());
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  measurement.setValue(6);
  TEST_ASSERT_NOT_EQUAL(second.timestamp, measurement.getValueTimestamp());
  TEST_ASSERT_FALSE(measurement.getValueSample().simulate);
  measurement.setValue(7, true);
  TEST_ASSERT_TRUE(measurement.getValueSample().simulate);
  TEST_ASSERT_EQUAL_INT(7, measurement.getValueSample().value);
}

void TestFtmsHoming::test_report_published_during_read() {
  Bike bike;
  bike.publishDuringRead = true;
  FtmsHoming::Search<Bike> search(bike);
  int32_t zero = INT32_MIN;
  TEST_ASSERT_TRUE_MESSAGE(search.endpoint(false, zero), FtmsHoming::failureName(search.failure()));
  TEST_ASSERT_INT32_WITHIN(25, 50, zero);
}

void TestFtmsHoming::test_wrong_direction_stops_motor() {
  Bike bike;
  bike.reverseResponse = true;
  FtmsHoming::Search<Bike> search(bike);
  int32_t zero = INT32_MIN;
  TEST_ASSERT_FALSE(search.endpoint(false, zero));
  TEST_ASSERT_TRUE(search.failure() == FtmsHoming::Failure::Timeout || search.failure() == FtmsHoming::Failure::NoProgress);
  TEST_ASSERT_FALSE(bike.moving());
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(FtmsHoming::END_TIMEOUT_MS, bike.clock);
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, zero);

  TEST_ASSERT_TRUE(FtmsHoming::supportsRange(0, 100));
  TEST_ASSERT_TRUE(FtmsHoming::supportsRange(1, 100));
  TEST_ASSERT_TRUE(FtmsHoming::supportsRange(1, 99));
  TEST_ASSERT_FALSE(FtmsHoming::supportsRange(0, 10));
  TEST_ASSERT_FALSE(FtmsHoming::supportsRange(2, 98));
}

void TestFtmsHoming::test_delayed_crossing_after_stop() {
  int crossings = 0;
  for (bool upper : {false, true}) {
    for (int scale : {100, 250}) {
      Bike bike;
      bike.stepsPerUnit = scale;
      bike.pos = bike.target = (upper ? 69 : 31) * scale;
      bike.reportLagMs = 1300;
      FtmsHoming::Search<Bike> search(bike);
      int32_t endpoint = INT32_MIN;
      bool success = search.endpoint(upper, endpoint);
      char context[180];
      snprintf(context, sizeof(context), "upper=%d steps/unit=%d reason=%s time=%u position=%d R=%d crossings=%d", upper, scale,
               FtmsHoming::failureName(search.failure()), bike.clock, bike.position(), bike.feedback.value, bike.delayedCrossings);
      TEST_ASSERT_TRUE_MESSAGE(success, context);
      TEST_ASSERT_INT32_WITHIN(25, (upper ? 99 * scale : 0) + scale / 2, endpoint);
      TEST_ASSERT_FALSE(bike.moving());
      crossings += bike.delayedCrossings;
    }
  }
  TEST_ASSERT_GREATER_THAN_INT(0, crossings);
}

void TestFtmsHoming::test_stationary_reading_confirmation() {
  Bike transient;
  transient.spikeOnStop = true;
  FtmsHoming::Search<Bike> search(transient);
  int32_t zero = INT32_MIN;
  TEST_ASSERT_TRUE(search.endpoint(false, zero));
  TEST_ASSERT_GREATER_THAN_INT(0, transient.spikes);
  TEST_ASSERT_INT32_WITHIN(25, 50, zero);

  Bike unsettled;
  unsettled.jitter = unsettled.stuck = true;
  unsettled.jitterSpan = 2;
  unsettled.jitterStartMs = 0;
  FtmsHoming::Search<Bike> unstableSearch(unsettled);
  zero = INT32_MIN;
  TEST_ASSERT_FALSE(unstableSearch.endpoint(false, zero));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FtmsHoming::Failure::Timeout), static_cast<int>(unstableSearch.failure()));
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, zero);
  TEST_ASSERT_FALSE(unsettled.moving());
  TEST_ASSERT_EQUAL_UINT32(FtmsHoming::END_TIMEOUT_MS, unsettled.clock);
}

void TestFtmsHoming::test_adjacent_boundary_noise() {
  for (bool upper : {false, true}) {
    for (bool legacy : {false, true}) {
      for (int scale : {100, 250}) {
        for (uint32_t phase : {800u, 1250u}) {
          Bike bike;
          bike.boundaryNoise = true;
          bike.legacy = legacy;
          bike.stepsPerUnit = scale;
          // Includes starting right on the desired 10/11 or 89/90 boundary.
          bike.pos = bike.target = (upper ? 89 : 10) * scale + scale / 2;
          bike.nextReport = phase;
          FtmsHoming::Search<Bike> search(bike);
          int32_t endpoint = INT32_MIN;
          TEST_ASSERT_TRUE_MESSAGE(search.endpoint(upper, endpoint), FtmsHoming::failureName(search.failure()));
          TEST_ASSERT_INT32_WITHIN(35, (upper ? 99 * scale : 0) + scale / 2, endpoint);
          // The stationary starting report now participates in confirmation.
          TEST_ASSERT_GREATER_OR_EQUAL_INT(2, bike.noisyReports);
          TEST_ASSERT_GREATER_OR_EQUAL_INT(2, bike.noisyEdgeReports);
          TEST_ASSERT_FALSE(bike.moving());
        }
      }
    }
  }

  // Adjacent noise far from the requested anchors is not homing success, and
  // must not keep resetting the progress timer while the motor is spinning.
  Bike stuck;
  stuck.jitter = stuck.stuck = true;
  stuck.jitterStartMs = 0;
  FtmsHoming::Search<Bike> search(stuck);
  int32_t endpoint = INT32_MIN;
  TEST_ASSERT_FALSE(search.endpoint(false, endpoint));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FtmsHoming::Failure::NoProgress), static_cast<int>(search.failure()));
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, endpoint);
  TEST_ASSERT_LESS_THAN_UINT32(16000, stuck.clock);
  TEST_ASSERT_FALSE(stuck.moving());
}

void TestFtmsHoming::test_shifted_crossing_retries() {
  int shifted = 0;
  for (int start : {5000, 5025, 5050, 5075, 5100}) {
    for (uint32_t phase : {800u, 1000u, 1250u}) {
      Bike bike;
      bike.pos = bike.target = start;
      bike.nextReport = phase;
      bike.shiftCrossing = true;
      FtmsHoming::Search<Bike> search(bike);
      int32_t endpoint = INT32_MIN;
      TEST_ASSERT_TRUE_MESSAGE(search.endpoint(false, endpoint), FtmsHoming::failureName(search.failure()));
      shifted += bike.crossingShifted;
      TEST_ASSERT_INT32_WITHIN(25, bike.crossingShifted ? -30 : 50, endpoint);
      TEST_ASSERT_FALSE(bike.moving());
    }
  }
  TEST_ASSERT_GREATER_THAN_INT(0, shifted);
}

void TestFtmsHoming::test_responsive_retries_until_deadline() {
  for (bool missingLevel : {false, true}) {
    Bike bike;
    if (missingLevel) {
      bike.skipTwo = true;
      bike.skipTwoUntilMs = 60000;
    } else {
      bike.jitter = true;
      bike.jitterSpan = 2;
      bike.jitterStartMs = 0;
      bike.jitterEndMs = 12000;
    }
    FtmsHoming::Search<Bike> search(bike);
    int32_t endpoint = INT32_MIN;
    TEST_ASSERT_TRUE_MESSAGE(search.endpoint(false, endpoint), FtmsHoming::failureName(search.failure()));
    TEST_ASSERT_INT32_WITHIN(25, 50, endpoint);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(missingLevel ? 60000 : 12000, bike.clock);
    TEST_ASSERT_LESS_THAN_UINT32(FtmsHoming::END_TIMEOUT_MS, bike.clock);
    TEST_ASSERT_FALSE(bike.moving());
  }
}

void TestFtmsHoming::test_accelerated_probe_repeatability() {
  // Short 1500 Hz probes never reach that speed with the real 3000 steps/s^2
  // ramp. Exercise their actual stopping/settling with delayed, noisy reports.
  for (bool upper : {false, true}) {
    for (int scale : {100, 250}) {
      for (uint32_t phase : {800u, 1250u}) {
        Bike bike;
        bike.acceleration = 3000;
        bike.stepsPerUnit = scale;
        bike.pos = bike.target = (upper ? 69 : 31) * scale;
        bike.reportLagMs = 1300;
        bike.boundaryNoise = true;
        bike.nextReport = phase;
        FtmsHoming::Search<Bike> search(bike);
        int32_t endpoint = INT32_MIN;
        TEST_ASSERT_TRUE_MESSAGE(search.endpoint(upper, endpoint), FtmsHoming::failureName(search.failure()));
        TEST_ASSERT_INT32_WITHIN(35, (upper ? 99 * scale : 0) + scale / 2, endpoint);
        TEST_ASSERT_FALSE(bike.moving());
        TEST_ASSERT_LESS_THAN_UINT32(FtmsHoming::END_TIMEOUT_MS, bike.clock);
      }
    }
  }
}

void TestFtmsHoming::test_one_second_startup_check() {
  Bike bike;
  bike.pos = bike.target = 1400;
  bike.feedback = {14, 1, false};
  bike.nextReport = 1100;
  bike.abortAt = 1500;
  FtmsHoming::Search<Bike> search(bike);
  int32_t endpoint = INT32_MIN;
  TEST_ASSERT_FALSE(search.endpoint(false, endpoint));
  // Use the real reading already present when homing starts, and the first
  // fresh report after a stationary second. Do not add a second blind wait.
  TEST_ASSERT_EQUAL_UINT32(1100, bike.firstMoveTime);
  TEST_ASSERT_EQUAL_INT(300, bike.speed);
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, endpoint);
  TEST_ASSERT_FALSE(bike.moving());
}
