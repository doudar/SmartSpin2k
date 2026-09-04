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

#include "PowerTable_Helpers.h"
#include "settings.h"

namespace ErgControl {

constexpr int LOW_GAIN_WATTS                   = 120;
constexpr int HIGH_GAIN_WATTS                  = 400;
constexpr int MIN_SCHEDULE_WATTS               = 30;
constexpr double TABLE_GAIN_MIN_FALLBACK_RATIO = 0.5;
constexpr double TABLE_GAIN_MAX_FALLBACK_RATIO = 1.25;
constexpr double TABLE_GAIN_BLEND              = 0.5;
constexpr double GAIN_MIN_SENSITIVITY_RATIO    = 0.25;
constexpr double GAIN_MAX_SENSITIVITY_RATIO    = 4.0;
constexpr double SLOPE_CONTROL_DIVISOR         = 10.0;
// lookup() safely extends cadence using equal-torque scaling against the
// nearest measured row. Limit trusted seeks to two table rows beyond the
// recorded edge so a short cadence surge remains covered without turning a
// sparse table into an unlimited extrapolator.
constexpr int TABLE_SEEK_CADENCE_MARGIN_RPM         = POWERTABLE_CAD_INCREMENT * 2;
constexpr int TABLE_SEEK_INCREASE_OVERSHOOT_WATTS   = ERG_MODE_PID_WINDOW;
constexpr int TABLE_SEEK_DECREASE_UNDERSHOOT_WATTS = ERG_MODE_PID_WINDOW * 2;

// Runtime validation is deliberately independent of TableEntry::readings:
// sample count describes how the table was built, while this score describes
// how accurately the completed surface predicts the bike right now.
class TableConfidence {
 public:
  static constexpr uint8_t MAX_SCORE    = 24;
  static constexpr uint8_t TRUST_SCORE  = 16;
  static constexpr uint8_t REVOKE_SCORE = 8;
  static constexpr uint8_t MISS_PENALTY = 2;

  void reset() { state = 0; }

  bool update(bool accurate) {
    const bool wasTrusted = trusted();
    uint8_t currentScore  = score();
    if (accurate) {
      if (currentScore < MAX_SCORE) ++currentScore;
    } else {
      currentScore = currentScore > MISS_PENALTY ? currentScore - MISS_PENALTY : 0;
    }

    bool isTrusted = wasTrusted;
    if (!wasTrusted && currentScore >= TRUST_SCORE) isTrusted = true;
    if (wasTrusted && currentScore <= REVOKE_SCORE) isTrusted = false;
    state = currentScore | (isTrusted ? TRUSTED_FLAG : 0);
    return isTrusted != wasTrusted;
  }

  bool trusted() const { return (state & TRUSTED_FLAG) != 0; }
  uint8_t score() const { return state & SCORE_MASK; }

 private:
  static constexpr uint8_t TRUSTED_FLAG = 0x80;
  static constexpr uint8_t SCORE_MASK   = 0x7f;

  uint8_t state = 0;
};

struct RecordedTableBounds {
  bool valid     = false;
  int minWatts   = 0;
  int maxWatts   = 0;
  int minCadence = 0;
  int maxCadence = 0;

  bool containsWatts(int watts) const { return valid && watts >= minWatts && watts <= maxWatts; }
  bool containsCadence(int cadence) const { return valid && cadence >= minCadence && cadence <= maxCadence; }
  bool contains(int watts, int cadence) const { return containsWatts(watts) && containsCadence(cadence); }
  bool containsWithCadenceMargin(int watts, int cadence, int margin) const {
    return margin >= 0 && containsWatts(watts) && cadence >= minCadence - margin && cadence <= maxCadence + margin;
  }
};

inline RecordedTableBounds recordedTableBounds(const PTData& table) {
  RecordedTableBounds bounds;
  int minimumWatts   = std::numeric_limits<int>::max();
  int maximumWatts   = std::numeric_limits<int>::min();
  int minimumCadence = std::numeric_limits<int>::max();
  int maximumCadence = std::numeric_limits<int>::min();

  for (int cadenceIndex = 0; cadenceIndex < POWERTABLE_CAD_SIZE; ++cadenceIndex) {
    int reliableEntries = 0;
    int rowMinimumWatts = std::numeric_limits<int>::max();
    int rowMaximumWatts = std::numeric_limits<int>::min();
    for (int wattIndex = 0; wattIndex < POWERTABLE_WATT_SIZE; ++wattIndex) {
      const TableEntry& entry = table.tableRow[cadenceIndex].tableEntry[wattIndex];
      if (entry.targetPosition == INT16_MIN || entry.readings < 2) continue;
      ++reliableEntries;
      const int watts = wattIndex * POWERTABLE_WATT_INCREMENT;
      rowMinimumWatts = std::min(rowMinimumWatts, watts);
      rowMaximumWatts = std::max(rowMaximumWatts, watts);
    }

    // The forward lookup also ignores rows that cannot establish a slope.
    if (reliableEntries < 2) continue;
    const int cadence = MINIMUM_TABLE_CAD + cadenceIndex * POWERTABLE_CAD_INCREMENT;
    minimumWatts      = std::min(minimumWatts, rowMinimumWatts);
    maximumWatts      = std::max(maximumWatts, rowMaximumWatts);
    minimumCadence    = std::min(minimumCadence, cadence);
    maximumCadence    = std::max(maximumCadence, cadence);
  }

  if (minimumWatts > maximumWatts || minimumCadence > maximumCadence) return bounds;
  bounds.valid      = true;
  bounds.minWatts   = minimumWatts;
  bounds.maxWatts   = maximumWatts;
  bounds.minCadence = minimumCadence;
  bounds.maxCadence = maximumCadence;
  return bounds;
}

inline bool positionMatchesPowerWindow(int32_t actualPosition, int32_t lowPosition, int32_t highPosition, int32_t padding) {
  if (padding < 0) return false;
  const int32_t lower = std::min(lowPosition, highPosition);
  const int32_t upper = std::max(lowPosition, highPosition);
  return actualPosition >= lower - padding && actualPosition <= upper + padding;
}

inline bool tableSeekExceededPowerLimit(int targetWatts, int actualWatts, bool increasing) {
  if (increasing) return actualWatts > targetWatts + TABLE_SEEK_INCREASE_OVERSHOOT_WATTS;
  return actualWatts < targetWatts - TABLE_SEEK_DECREASE_UNDERSHOOT_WATTS;
}

inline double sanitizeSensitivity(double sensitivity) { return std::isfinite(sensitivity) && sensitivity > 0.0 ? sensitivity : 1.0; }

inline double fallbackGain(double sensitivity, int operatingWatts) {
  sensitivity = sanitizeSensitivity(sensitivity);
  if (operatingWatts < LOW_GAIN_WATTS) {
    return sensitivity * LOW_GAIN_WATTS / std::max(operatingWatts, MIN_SCHEDULE_WATTS);
  }
  if (operatingWatts > HIGH_GAIN_WATTS) {
    return sensitivity * HIGH_GAIN_WATTS / operatingWatts;
  }
  return sensitivity;
}

inline double boundedTableGain(double localGain, double fallback) {
  if (!std::isfinite(localGain) || localGain <= 0.0) return fallback;
  const double minimumGain = fallback * TABLE_GAIN_MIN_FALLBACK_RATIO;
  const double maximumGain = fallback * TABLE_GAIN_MAX_FALLBACK_RATIO;
  return std::max(minimumGain, std::min(localGain, maximumGain));
}

inline double blendedTableGain(double localGain, double fallback) {
  const double bounded = boundedTableGain(localGain, fallback);
  return fallback + (bounded - fallback) * TABLE_GAIN_BLEND;
}

inline double errorScheduledGain(double gain, int error, bool maintaining) {
  const int absoluteError = std::abs(error);
  if (absoluteError < 10 || !maintaining) return gain * 0.25;
  if (absoluteError < 50) return gain * 0.75;
  if (absoluteError > 100) return gain * 1.25;
  return gain;
}

inline double clampGain(double gain, double sensitivity) {
  sensitivity              = sanitizeSensitivity(sensitivity);
  const double minimumGain = sensitivity * GAIN_MIN_SENSITIVITY_RATIO;
  const double maximumGain = sensitivity * GAIN_MAX_SENSITIVITY_RATIO;
  return std::max(minimumGain, std::min(gain, maximumGain));
}

}  // namespace ErgControl
