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
  bool freezeFour = false;
  bool loggedCurve = false, skipThirty = false;
  int movingNoise = 0;
  int stuckValue = 50;
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
  int wideFourSteps = 0;
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
      pending = stuck ? stuckValue : static_cast<int>(std::floor((reverseResponse ? 100 * stepsPerUnit - reportedPosition : reportedPosition) / stepsPerUnit + 0.5));
      if (loggedCurve && !stuck) {
        // Stationary readings from the 2026-09-19 10:52 log, not moving
        // notifications (those lag the physical position by several seconds).
        const double x[] = {2323, 4959, 7485, 10214};
        int i = reportedPosition < x[1] ? 0 : (reportedPosition < x[2] ? 1 : 2);
        pending = static_cast<int>(std::round(40 + 10 * i + 10 * (reportedPosition - x[i]) / (x[i + 1] - x[i])));
      }
      if (movingNoise) pending += ((nextReport / 1000) % 2 ? movingNoise : -movingNoise);
      if (skipThirty && pending == 30) pending = 31;
      if (wideFourSteps && !stuck) {
        if (reportedPosition >= 4.5 * stepsPerUnit + wideFourSteps) pending = static_cast<int>(std::floor((reportedPosition - wideFourSteps) / stepsPerUnit + 0.5));
        else if (reportedPosition >= 3.5 * stepsPerUnit) pending = 4;
      }
      if (freezeFour && pending == 4) { stuck = true; stuckValue = 4; }
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
      if (shiftCrossing && !moving() && (pending == 10 || pending == 11)) {
        if (firstCrossingPosition == INT32_MIN) firstCrossingPosition = position();
        else crossingShifted = true;
      }
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
    if (fault == 3) {
      TEST_ASSERT_TRUE(search.endpoint(false, zero)); // A skipped level still has a bounded crossing.
      TEST_ASSERT_INT32_WITHIN(40, 50, zero);
      TEST_ASSERT_FALSE(bike.moving());
      continue;
    }
    TEST_ASSERT_FALSE(search.endpoint(false, zero));
    TEST_ASSERT_FALSE(bike.moving());
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, zero);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(FtmsHoming::END_TIMEOUT_MS, bike.clock);
    if (fault < 2) TEST_ASSERT_LESS_OR_EQUAL_UINT32(bike.stopReports + FtmsHoming::REPORT_TIMEOUT_MS, bike.clock);
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
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FtmsHoming::Failure::NoProgress), static_cast<int>(unstableSearch.failure()));
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, zero);
  TEST_ASSERT_FALSE(unsettled.moving());
  TEST_ASSERT_LESS_THAN_UINT32(FtmsHoming::END_TIMEOUT_MS, unsettled.clock);
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
    if (!missingLevel) TEST_ASSERT_GREATER_OR_EQUAL_UINT32(12000, bike.clock);
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
  TEST_ASSERT_UINT32_WITHIN(10, 1100, bike.firstMoveTime);
  TEST_ASSERT_EQUAL_INT(300, bike.speed);
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, endpoint);
  TEST_ASSERT_FALSE(bike.moving());
}

void TestFtmsHoming::test_wide_resistance_four() {
  for (int width : {1200, 2400}) {
    Bike bike;
    bike.wideFourSteps = width;
    bike.pos = bike.target = 5000 + width;
    bike.acceleration = 3000;
    bike.reportLagMs = 1300;
    FtmsHoming::Search<Bike> search(bike);
    int32_t origin;
    TEST_ASSERT_TRUE_MESSAGE(search.endpoint(false, origin), FtmsHoming::failureName(search.failure()));
    TEST_ASSERT_INT32_WITHIN(35, 50 - width / 4, origin);
    TEST_ASSERT_FALSE(bike.moving());
    TEST_ASSERT_LESS_THAN_UINT32(FtmsHoming::END_TIMEOUT_MS, bike.clock);
    printf("Wide R4 (%d extra steps): %u ms\n", width, bike.clock);
  }
  Bike frozen;
  frozen.freezeFour = true;
  FtmsHoming::Search<Bike> failed(frozen);
  int32_t unchanged = INT32_MIN;
  TEST_ASSERT_FALSE(failed.endpoint(false, unchanged));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FtmsHoming::Failure::NoProgress), static_cast<int>(failed.failure()));
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, unchanged);
  TEST_ASSERT_FALSE(frozen.moving());
  TEST_ASSERT_LESS_THAN_UINT32(FtmsHoming::END_TIMEOUT_MS, frozen.clock);
}

void TestFtmsHoming::test_calibrated_startup_and_map() {
  Bike calibration;
  calibration.acceleration = 3000;
  calibration.reportLagMs = 1300;
  calibration.pos = calibration.target = 9900;
  FtmsHoming::Search<Bike> search(calibration);
  FtmsCalibration::Map map;
  map.source = FtmsCalibration::identity("Grupetto 32", false);
  map.maximum = 9900;
  for (int i = FtmsCalibration::COUNT - 1; i >= 0; --i) {
    int32_t reference;
    TEST_ASSERT_TRUE_MESSAGE(search.reference(FtmsCalibration::FIRST + i * FtmsCalibration::GAP, reference, map.level2[i], map.maximum / 100), FtmsHoming::failureName(search.failure()));
    map.position[i] = reference - 50;
    TEST_ASSERT_INT32_WITHIN(50, map.level2[i] * 50 - 50, map.position[i]);
  }
  printf("Three FTMS map observations: %u ms\n", calibration.clock);
  TEST_ASSERT_TRUE(map.valid());
  uint8_t bytes[FtmsCalibration::WIRE_SIZE];
  map.encode(bytes);
  FtmsCalibration::Map loaded;
  TEST_ASSERT_TRUE(loaded.decode(bytes, sizeof(bytes)));
  TEST_ASSERT_TRUE(loaded.matches(map.source, map.maximum));
  TEST_ASSERT_FALSE(loaded.matches(map.source + 1, map.maximum));
  TEST_ASSERT_FALSE(loaded.matches(map.source, map.maximum + 1));
  TEST_ASSERT_FALSE(loaded.decode(bytes, sizeof(bytes) - 1));
  bytes[15] ^= 1;
  TEST_ASSERT_FALSE(loaded.decode(bytes, sizeof(bytes)));
  for (int start : {3500, 3640, 4960, 5040, 6300, 6500}) {
    for (uint32_t phase : {800u, 1250u}) {
      Bike bike;
      bike.pos = bike.target = start;
      bike.nextReport = phase;
      bike.reportLagMs = 1300;
      FtmsHoming::Search<Bike> recovery(bike);
      int32_t origin = INT32_MIN;
      TEST_ASSERT_TRUE_MESSAGE(recovery.recover(map, origin), FtmsHoming::failureName(recovery.failure()));
      // A stationary quantized reading has bin-width uncertainty, not the
      // narrow repeatability of deliberately measuring its transition.
      TEST_ASSERT_INT32_WITHIN(140, 50, origin);
      TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, bike.firstMoveTime);
      TEST_ASSERT_LESS_THAN_UINT32(5000, bike.clock);
    }
  }
  for (int fault = 0; fault < 4; ++fault) {
    Bike bike;
    bike.stuck = fault == 3;
    if (fault == 0) bike.simulated = true;
    if (fault == 1) bike.stopReports = 0;
    if (fault == 2) bike.abortAt = 500;
    // A constant in-range sensor cannot prove a live motor, but recovery issues
    // no motion. Missing/simulated/cancelled feedback must never be accepted.
    FtmsHoming::Search<Bike> recovery(bike);
    int32_t origin = INT32_MIN;
    TEST_ASSERT_EQUAL_INT(fault == 3, recovery.recover(map, origin));
    TEST_ASSERT_FALSE(bike.moving());
    TEST_ASSERT_LESS_THAN_UINT32(20000, bike.clock);
  }
  for (int scale : {100, 300}) {
    FtmsCalibration::Map larger = map;
    larger.maximum = 99 * scale;
    for (int i = 0; i < FtmsCalibration::COUNT; ++i) larger.position[i] = larger.level2[i] * scale / 2 - scale / 2;
    for (int level : {1, 20, 80, 99}) {
      Bike bike;
      bike.stepsPerUnit = scale;
      bike.pos = bike.target = level * scale;
      bike.reportLagMs = 1300;
      bike.acceleration = 3000;
      FtmsHoming::Search<Bike> recovery(bike);
      int32_t origin;
      bool success = recovery.recover(larger, origin);
      printf("FTMS reference startup R%d scale%d: %u ms, %s\n", level, scale, bike.clock, FtmsHoming::failureName(recovery.failure()));
      TEST_ASSERT_TRUE_MESSAGE(success, FtmsHoming::failureName(recovery.failure()));
      TEST_ASSERT_INT32_WITHIN(scale / 2 + 40, scale / 2, origin);
      TEST_ASSERT_LESS_THAN_UINT32(20000, bike.clock);
    }
  }
}

void TestFtmsHoming::test_stationary_drift_guard() {
  FtmsCalibration::Map map;
  map.source = 123;
  map.maximum = 30000;
  for (int i = 0; i < FtmsCalibration::COUNT; ++i) map.position[i] = map.level2[i] * 150;
  int32_t center, uncertainty;
  TEST_ASSERT_TRUE(map.estimateHalf(100, center, uncertainty));
  TEST_ASSERT_FALSE(map.estimateHalf(8, center, uncertainty));
  // Start cooldown through a real correction, including across clock rollover.
  auto primeCooldown = [&](FtmsCalibration::DriftGuard& guard, uint32_t end) {
    for (uint32_t elapsed = 0; elapsed <= FtmsCalibration::STABLE_MS; elapsed += 1000) {
      const uint32_t time = end - FtmsCalibration::STABLE_MS + elapsed;
      const int correction = guard.correction(map, time, time, 50, center - 500, true);
      TEST_ASSERT_EQUAL_INT(elapsed == FtmsCalibration::STABLE_MS ? 500 : 0, correction);
    }
  };
  for (int scenario = 0; scenario < 9; ++scenario) {
    FtmsCalibration::DriftGuard guard;
    primeCooldown(guard, 1000);
    int total = 0;
    for (uint32_t time = 1000; time <= 70000; time += 1000) {
      bool eligible = scenario != 1;
      int resistance = scenario == 2 ? 50 + (time / 1000) % 2 : (scenario == 6 ? 4 : 50);
      int32_t position = center - 500;
      if (scenario == 3) position += time;
      if (scenario == 4) position = center;
      if (scenario == 5) position = center - 5000;
      uint32_t stamp = scenario == 7 ? 1 : time;
      if (scenario == 8 && time >= 59000 && time <= 66000) eligible = false;
      total += guard.correction(map, time, stamp, resistance, position, eligible);
    }
    TEST_ASSERT_EQUAL_INT(scenario == 0 ? 500 : (scenario == 2 ? 650 : (scenario == 5 ? 5000 : 0)), total);
  }
  FtmsCalibration::DriftGuard wrapped;
  uint32_t start = UINT32_MAX - 5000;
  primeCooldown(wrapped, start);
  int total = 0;
  for (uint32_t elapsed = 0; elapsed <= 70000; elapsed += 1000)
    total += wrapped.correction(map, start + elapsed, start + elapsed, 50, center + 500, true);
  TEST_ASSERT_EQUAL_INT(-500, total);
}

void TestFtmsHoming::test_sparse_noisy_observations() {
  for (int noise : {0, 2}) {
    for (uint32_t lag : {1300u, 2500u}) {
      Bike bike;
      bike.loggedCurve = true;
      bike.movingNoise = noise;
      bike.reportLagMs = lag;
      bike.acceleration = 3000;
      bike.pos = bike.target = 17083;
      FtmsHoming::Search<Bike> search(bike);
      FtmsCalibration::Map map;
      map.source = 1;
      map.maximum = 24502;
      for (int i = FtmsCalibration::COUNT - 1; i >= 0; --i) {
        int32_t position;
        TEST_ASSERT_TRUE_MESSAGE(search.reference(33 + 17 * i, position, map.level2[i], 245), FtmsHoming::failureName(search.failure()));
        map.position[i] = position + 6836;
      }
      TEST_ASSERT_TRUE(map.valid());
      TEST_ASSERT_LESS_THAN_UINT32(40000, bike.clock);
      printf("Log-shaped sparse map, lag=%u noise=%d: %u ms\n", lag, noise, bike.clock);
      int32_t center, uncertainty;
      TEST_ASSERT_TRUE(map.estimateHalf(100, center, uncertainty));
      TEST_ASSERT_INT32_WITHIN(2 * 264, 4959 + 6836, center);
      TEST_ASSERT_FALSE(bike.moving());
    }
  }
  // Startup remains usable with responsive noisy feedback even if bounded
  // acquisition takes it beyond the ideal <20 second performance target.
  Bike noisyBoot;
  noisyBoot.stepsPerUnit = 300;
  noisyBoot.pos = noisyBoot.target = 29700;
  noisyBoot.movingNoise = 2;
  noisyBoot.reportLagMs = 2500;
  noisyBoot.acceleration = 3000;
  FtmsCalibration::Map bootMap;
  bootMap.source = 1;
  bootMap.maximum = 29700;
  for (int i = 0; i < FtmsCalibration::COUNT; ++i) bootMap.position[i] = bootMap.level2[i] * 150 - 150;
  FtmsHoming::Search<Bike> bootSearch(noisyBoot);
  int32_t zero;
  TEST_ASSERT_TRUE_MESSAGE(bootSearch.recover(bootMap, zero), FtmsHoming::failureName(bootSearch.failure()));
  TEST_ASSERT_INT32_WITHIN(600, 150, zero);
  TEST_ASSERT_LESS_THAN_UINT32(30000, noisyBoot.clock);
  printf("Noisy R99 startup with 2.5s feedback lag: %u ms\n", noisyBoot.clock);
  // The old exact-transition search failed when the log jumped 29 -> 31.
  Bike skipped;
  skipped.skipThirty = true;
  skipped.pos = skipped.target = 4000;
  skipped.acceleration = 3000;
  skipped.reportLagMs = 2500;
  FtmsHoming::Search<Bike> skippedSearch(skipped);
  int32_t position;
  uint8_t level2 = 0;
  TEST_ASSERT_TRUE(skippedSearch.reference(30, position, level2, 100));
  TEST_ASSERT_EQUAL_INT(62, level2);
  TEST_ASSERT_LESS_THAN_UINT32(15000, skipped.clock);

  // No motion is needed just to make a noisy, already-nearby reading perfect.
  Bike noisy;
  noisy.jitter = noisy.stuck = true;
  noisy.jitterSpan = 4;
  noisy.jitterStartMs = 0;
  FtmsHoming::Search<Bike> noisySearch(noisy);
  TEST_ASSERT_TRUE(noisySearch.reference(52, position, level2, 100));
  TEST_ASSERT_INT_WITHIN(2, 104, level2);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, noisy.firstMoveTime);
  TEST_ASSERT_LESS_THAN_UINT32(6000, noisy.clock);

  // Fresh timestamps alone must not let a non-responsive sensor drive forever.
  Bike frozen;
  frozen.stuck = true;
  FtmsHoming::Search<Bike> frozenSearch(frozen);
  TEST_ASSERT_FALSE(frozenSearch.reference(33, position, level2, 250));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FtmsHoming::Failure::NoProgress), static_cast<int>(frozenSearch.failure()));
  TEST_ASSERT_GREATER_OR_EQUAL_INT(3000, std::abs(frozen.position() - 5000));
  TEST_ASSERT_FALSE(frozen.moving());
  TEST_ASSERT_LESS_THAN_UINT32(20000, frozen.clock);
}

void TestFtmsHoming::test_manual_knob_resync() {
  // Reproduce the 11:40 log: R48/P11520, shift to P9100 with R38/39,
  // then manually turn the knob to R32 while the motor counter stays 9100.
  // The log omits the saved map; use a 255-step/level map consistent with
  // those two observed positions, not a claimed reconstruction of its PTAB.
  for (int firstLevel2 : {62, 66, 70}) {
    FtmsCalibration::Map map;
    map.source = 1;
    map.maximum = 24413;
    map.level2[0] = firstLevel2;
    for (int i = 0; i < FtmsCalibration::COUNT; ++i) map.position[i] = map.level2[i] * 255 / 2 - 720;
    FtmsCalibration::DriftGuard guard;
    int32_t position = 11520;
    uint32_t correctedAt = 0;
    for (uint32_t time = 105000; time <= 207000; time += 1000) {
      if (time == 128000 || time == 129000) {
        position -= 1210;
        TEST_ASSERT_EQUAL_INT(0, guard.correction(map, time, time, 48, position, false));
        continue;
      }
      int resistance = time < 130000 ? 48 : (time < 158000 ? 38 + (time / 1000) % 2 : (time == 158000 ? 37 : (time == 159000 ? 33 : 32)));
      int correction = guard.correction(map, time, time, resistance, position, true);
      if (time < 160000) TEST_ASSERT_EQUAL_INT(0, correction);
      if (time == 165000) TEST_ASSERT_TRUE(guard.uncertain());
      if (correction) { position += correction; correctedAt = time; }
    }
    TEST_ASSERT_EQUAL_INT32(7440, position);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(169000, correctedAt);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(190000, correctedAt);
    TEST_ASSERT_FALSE(guard.uncertain());
    int32_t center, uncertainty;
    TEST_ASSERT_TRUE(map.estimateForSync(64, center, uncertainty));
    TEST_ASSERT_EQUAL_INT32(7440, center);
    TEST_ASSERT_FALSE(map.estimateForSync(58, center, uncertainty));
    TEST_ASSERT_FALSE(map.estimateForSync(142, center, uncertainty));
    TEST_ASSERT_FALSE(map.estimateHalf(60, center, uncertainty)); // Startup support remains strict.
  }
  // Once the average is within deadband, an individual edge of adjacent
  // jitter must not keep suspending otherwise valid watts collection.
  FtmsCalibration::Map jitterMap;
  jitterMap.source = 1;
  jitterMap.maximum = 30000;
  for (int i = 0; i < FtmsCalibration::COUNT; ++i) jitterMap.position[i] = jitterMap.level2[i] * 150;
  FtmsCalibration::DriftGuard jitterGuard;
  for (uint32_t t = 0; t <= 20000; t += 1000) {
    TEST_ASSERT_EQUAL_INT(0, jitterGuard.correction(jitterMap, t, t, 50 + (t / 1000) % 2, 14850, true));
    if (t >= 10000) TEST_ASSERT_FALSE(jitterGuard.uncertain());
  }
  // Interrupting observation restarts confirmation, not the correction timer.
  FtmsCalibration::Map map;
  map.source = 1;
  map.maximum = 30000;
  for (int i = 0; i < FtmsCalibration::COUNT; ++i) map.position[i] = map.level2[i] * 150;
  FtmsCalibration::DriftGuard guard;
  int correction = 0;
  for (uint32_t t = 1000; t <= 11000; t += 1000) correction += guard.correction(map, t, t, 50, 14000, true);
  TEST_ASSERT_EQUAL_INT(1000, correction);
  guard.interrupt(); // e.g. a brief driver-lock conflict must not erase cooldown.
  for (uint32_t t = 12000; t < 71000; t += 1000) TEST_ASSERT_EQUAL_INT(0, guard.correction(map, t, t, 50, 14000, true));
  TEST_ASSERT_EQUAL_INT(1000, guard.correction(map, 71000, 71000, 50, 14000, true));
}
