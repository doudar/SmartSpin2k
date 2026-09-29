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
#include "ERG_Mode_Settings.h"

namespace ErgControl {

// Estimate row spacing from nearby pairs of measured cells at equal watts.
// A robust median of pairwise slopes allows gradual widening with power.
// Reject crossing rows and bound the result by the observed median spacing.
inline bool measuredCadenceSlope(const PTData& table, int watts, int cadence, double& slope) {
  constexpr int capacity     = CADENCE_CURVE_MAX_PAIRS;
  double distances[capacity] = {}, slopes[capacity] = {}, powers[capacity] = {};
  int count = 0;
  for (int row = 1; row < POWERTABLE_CAD_SIZE; ++row) {
    for (int column = 1; column < POWERTABLE_WATT_SIZE; ++column) {
      const auto& lower = table.tableRow[row - 1].tableEntry[column];
      const auto& upper = table.tableRow[row].tableEntry[column];
      if (lower.readings < 2 || upper.readings < 2 || lower.targetPosition == INT16_MIN || upper.targetPosition == INT16_MIN || upper.targetPosition >= lower.targetPosition) {
        continue;
      }
      const double distance = std::abs(column * POWERTABLE_WATT_INCREMENT - watts) / static_cast<double>(POWERTABLE_WATT_INCREMENT) +
                              CADENCE_CURVE_DISTANCE_WEIGHT * std::abs(MINIMUM_TABLE_CAD + (row - 0.5) * POWERTABLE_CAD_INCREMENT - cadence) / POWERTABLE_CAD_INCREMENT;
      int slot              = 0;
      while (slot < count && distances[slot] <= distance) ++slot;
      if (slot >= capacity) continue;
      for (int index = std::min(count, capacity - 1); index > slot; --index) {
        distances[index] = distances[index - 1];
        slopes[index]    = slopes[index - 1];
        powers[index]    = powers[index - 1];
      }
      distances[slot] = distance;
      slopes[slot]    = (upper.targetPosition - lower.targetPosition) * static_cast<double>(TABLE_DIVISOR) / POWERTABLE_CAD_INCREMENT;
      powers[slot]    = column * POWERTABLE_WATT_INCREMENT;
      count           = std::min(count + 1, capacity);
    }
  }
  if (count < CADENCE_CURVE_MIN_PAIRS) return false;
  double changes[capacity * (capacity - 1) / 2] = {};
  int changeCount                               = 0;
  for (int first = 0; first < count; ++first) {
    for (int second = first + 1; second < count; ++second) {
      if (powers[first] != powers[second]) changes[changeCount++] = (slopes[first] - slopes[second]) / (powers[first] - powers[second]);
    }
  }
  double widening = 0;
  if (changeCount >= CADENCE_CURVE_MIN_PAIRS) {
    std::sort(changes, changes + changeCount);
    widening = std::min(0.0, changes[changeCount / 2]);
  }
  double adjusted[capacity] = {};
  for (int index = 0; index < count; ++index) adjusted[index] = slopes[index] + widening * (watts - powers[index]);
  std::sort(slopes, slopes + count);
  std::sort(adjusted, adjusted + count);
  // Slopes are negative: the maximum magnitude is the lower numeric bound.
  slope = std::max(CADENCE_CURVE_MAX_SPACING_RATIO * slopes[count / 2], std::min(CADENCE_CURVE_MIN_SPACING_RATIO * slopes[count / 2], adjusted[count / 2]));
  return true;
}

// Extend the closest measured power curve using the observed cadence spacing.
inline int32_t constrainedTablePosition(const PTData& table, int watts, int cadence) {
  double cadenceSlope;
  if (!measuredCadenceSlope(table, watts, cadence, cadenceSlope)) return RETURN_ERROR;
  double bestDistance = std::numeric_limits<double>::max();
  double result       = 0;
  for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
    int lower = -1, upper = -1, previous = -1, last = -1;
    for (int column = 0; column < POWERTABLE_WATT_SIZE; ++column) {
      const auto& entry = table.tableRow[row].tableEntry[column];
      if (entry.readings < 2 || entry.targetPosition == INT16_MIN) continue;
      const int columnWatts = column * POWERTABLE_WATT_INCREMENT;
      if (columnWatts <= watts) lower = column;
      if (columnWatts >= watts && upper < 0) upper = column;
      previous = last;
      last     = column;
    }
    if (lower < 0 || previous < 0) continue;
    if (upper < 0) {
      lower = previous;
      upper = last;
    }
    const auto& first  = table.tableRow[row].tableEntry[lower];
    const auto& second = table.tableRow[row].tableEntry[upper];
    if (upper != lower && second.targetPosition <= first.targetPosition) continue;
    const double fraction = upper == lower ? 0 : static_cast<double>(watts - lower * POWERTABLE_WATT_INCREMENT) / ((upper - lower) * POWERTABLE_WATT_INCREMENT);
    const double position = (first.targetPosition + fraction * (second.targetPosition - first.targetPosition)) * TABLE_DIVISOR;
    const int rowCadence  = MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT;
    const double distance = CADENCE_CURVE_EXTRAPOLATION_WEIGHT * std::max(0, watts - last * POWERTABLE_WATT_INCREMENT) / POWERTABLE_WATT_INCREMENT +
                            std::abs(rowCadence - cadence) / static_cast<double>(POWERTABLE_CAD_INCREMENT);
    if (distance < bestDistance) {
      bestDistance = distance;
      result       = position + cadenceSlope * (cadence - rowCadence);
    }
  }
  if (bestDistance == std::numeric_limits<double>::max() || result <= INT32_MIN || result >= INT32_MAX) return RETURN_ERROR;
  return static_cast<int32_t>(std::round(result));
}

// Runtime validation is deliberately independent of TableEntry::readings:
// sample count describes how the table was built, while this score describes
// how accurately the completed surface predicts the bike right now.
class TableConfidence {
 public:
  // Clear the confidence score and revoke table trust.
  // Called by ErgMode::resetTableConfidence() when table confidence must be discarded, including table reset or loss of homing.
  void reset() { state = 0; }

  // Update confidence from prediction accuracy and return whether the table's trusted status changed.
  // Called by ErgMode::_scoreTable() when an eligible power/position observation provides evidence about table accuracy.
  bool update(bool accurate) {
    const bool wasTrusted = trusted();
    uint8_t currentScore  = score();
    if (accurate) {
      if (currentScore < TABLE_CONFIDENCE_MAX_SCORE) ++currentScore;
    } else {
      currentScore = currentScore > TABLE_CONFIDENCE_MISS_PENALTY ? currentScore - TABLE_CONFIDENCE_MISS_PENALTY : 0;
    }

    bool isTrusted = wasTrusted;
    if (!wasTrusted && currentScore >= TABLE_CONFIDENCE_TRUST_SCORE) isTrusted = true;
    if (wasTrusted && currentScore <= TABLE_CONFIDENCE_REVOKE_SCORE) isTrusted = false;
    state = currentScore | (isTrusted ? TRUSTED_FLAG : 0);
    return isTrusted != wasTrusted;
  }

  // Report whether confidence has earned table trust without subsequently reaching the revocation threshold.
  // Used to authorize trusted table seeks, select table-correction strength, and track or log trust transitions.
  bool trusted() const { return (state & TRUSTED_FLAG) != 0; }
  // Return the confidence score without the packed trust flag.
  // Used by update() when scoring new evidence and by _scoreTable() when logging a trust transition.
  uint8_t score() const { return state & SCORE_MASK; }

 private:
  // ****************************************************************************
  // Packed confidence state layout
  // Separates the trust flag from the score within the single state byte.
  // ****************************************************************************
  static constexpr uint8_t TRUSTED_FLAG = 0x80;
  static constexpr uint8_t SCORE_MASK   = 0x7f;
  // ****************************************************************************

  uint8_t state = 0;
};

struct RecordedTableBounds {
  bool valid     = false;
  int minWatts   = 0;
  int maxWatts   = 0;
  int minCadence = 0;
  int maxCadence = 0;

  // Check whether watts fall within the inclusive recorded range, rejecting invalid bounds.
  // Called by contains() for the watt-range portion of a combined watt/cadence bounds check.
  bool containsWatts(int watts) const { return valid && watts >= minWatts && watts <= maxWatts; }
  // Check whether cadence falls within the inclusive recorded range, rejecting invalid bounds.
  // Called by contains() after the watt-range check passes to complete the combined bounds check.
  bool containsCadence(int cadence) const { return valid && cadence >= minCadence && cadence <= maxCadence; }
  // Check whether both watts and cadence lie within the recorded bounds.
  // Used by _tableTargetIsWithinMeasuredBounds() to label extrapolated seeks in logs, and by replay tests to filter samples.
  bool contains(int watts, int cadence) const { return containsWatts(watts) && containsCadence(cadence); }
};

// Find watt and cadence bounds across rows with at least two reliable entries, returning invalid bounds if none qualify.
// Used by _tableTargetIsWithinMeasuredBounds() for seek-log annotations and by replay tests when checking recorded table coverage.
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

// Check whether the actual position lies between either ordering of the endpoints plus padding; reject negative padding.
// Used by _positionPredictionIsAccurate() to compare motor position with the table's watt-tolerance window when evaluating table confidence.
inline bool positionMatchesPowerWindow(int32_t actualPosition, int32_t lowPosition, int32_t highPosition, int32_t padding) {
  if (padding < 0) return false;
  const int32_t lower = std::min(lowPosition, highPosition);
  const int32_t upper = std::max(lowPosition, highPosition);
  return actualPosition >= lower - padding && actualPosition <= upper + padding;
}

// Detect power overshoot on an increasing table seek or undershoot on a decreasing seek beyond the allowed margin.
// Used during trusted table seeks and feedback acquisition waits to stop a seek or release the wait early after excessive overshoot.
inline bool tableSeekExceededPowerLimit(int targetWatts, int actualWatts, bool increasing) {
  if (increasing) return actualWatts > targetWatts + TABLE_SEEK_INCREASE_OVERSHOOT_WATTS;
  return actualWatts < targetWatts - TABLE_SEEK_DECREASE_UNDERSHOOT_WATTS;
}

// Keep finite positive sensitivity values and replace invalid or nonpositive values with 1.0.
// Used before ERG gain calculations, low-watt adjustments, final gain clamps, and table-correction scaling consume configured sensitivity.
inline double sanitizeSensitivity(double sensitivity) { return std::isfinite(sensitivity) && sensitivity > 0.0 ? sensitivity : 1.0; }

// Compute fallback gain from sensitivity, boosting it at low operating watts and reducing it at high operating watts.
// Called by scheduledErgGain() with target watts on every proportional correction, as the blend baseline or sole gain when no table slope is usable.
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

// Limit valid table gain relative to fallback gain, or use fallback gain when the table gain is invalid.
// Called by blendedTableGain() before mixing gains, limiting the table slope's influence on ERG proportional corrections.
inline double boundedTableGain(double localGain, double fallback) {
  if (!std::isfinite(localGain) || localGain <= 0.0) return fallback;
  const double minimumGain = fallback * TABLE_GAIN_MIN_FALLBACK_RATIO;
  const double maximumGain = fallback * TABLE_GAIN_MAX_FALLBACK_RATIO;
  return std::max(minimumGain, std::min(localGain, maximumGain));
}

// Blend bounded table gain with fallback gain using the configured table weight.
// Called by scheduledErgGain() on the ERG proportional-control path when lookupErgSlope() succeeds at the target watts and current cadence.
// If the slope lookup fails, that caller uses fallback gain directly; successful blends precede error scheduling and final sensitivity clamps.
inline double blendedTableGain(double localGain, double fallback) {
  const double bounded = boundedTableGain(localGain, fallback);
  return fallback + (bounded - fallback) * TABLE_GAIN_BLEND;
}

// Scale gain by absolute control error and whether the controller is maintaining the target.
// Used by _inSetpointState() on the proportional-control path after any table correction is unavailable or skipped.
// The error has already been reduced for an approaching power trend; gain comes from the table slope or fallback schedule.
// Non-MAINTAIN mode always uses the small-error multiplier; in MAINTAIN, errors from 50 through 100 W leave gain unchanged.
// The caller then applies any low-watt adjustment and the separate sensitivity-based minimum/maximum gain clamp.
inline double errorScheduledGain(double gain, int error, bool maintaining) {
  const int absoluteError = std::abs(error);
  if (absoluteError < SMALL_ERROR_WATTS || !maintaining) return gain * SMALL_ERROR_GAIN_MULTIPLIER;
  if (absoluteError < MEDIUM_ERROR_WATTS) return gain * MEDIUM_ERROR_GAIN_MULTIPLIER;
  if (absoluteError > LARGE_ERROR_WATTS) return gain * LARGE_ERROR_GAIN_MULTIPLIER;
  return gain;
}

// Reduce error by 1.5 seconds of projected power change when the trend approaches the target at least 2 W/s.
// Called at the start of _inSetpointState() before choosing a table correction or calculating proportional gain and movement.
// Brake an approach already visible in fresh meter reports. This never
// reverses the requested correction and has no steady-error dead band.
inline int approachingError(int error, double wattsPerSecond) {
  if (error * wattsPerSecond <= 0 || std::abs(wattsPerSecond) < 2.0) return error;
  const double remaining = std::max(0.0, std::abs(static_cast<double>(error)) - std::abs(wattsPerSecond) * 1.5);
  return static_cast<int>(std::round(error < 0 ? -remaining : remaining));
}

// Clamp final gain to the configured minimum and maximum multiples of sanitized ERG sensitivity.
// Called by _inSetpointState() after error scheduling and any low-watt adjustment, just before multiplying gain by control error.
inline double clampGain(double gain, double sensitivity) {
  sensitivity              = sanitizeSensitivity(sensitivity);
  const double minimumGain = sensitivity * GAIN_MIN_SENSITIVITY_RATIO;
  const double maximumGain = sensitivity * GAIN_MAX_SENSITIVITY_RATIO;
  return std::max(minimumGain, std::min(gain, maximumGain));
}

}  // namespace ErgControl
