/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace ErgControl {

constexpr int LOW_GAIN_WATTS                   = 120;
constexpr int HIGH_GAIN_WATTS                  = 400;
constexpr int MIN_SCHEDULE_WATTS               = 30;
constexpr double TABLE_GAIN_MIN_FALLBACK_RATIO = 0.5;
constexpr double TABLE_GAIN_MAX_FALLBACK_RATIO = 2.0;
constexpr double GAIN_MIN_SENSITIVITY_RATIO     = 0.25;
constexpr double GAIN_MAX_SENSITIVITY_RATIO     = 4.0;
constexpr double SLOPE_CONTROL_DIVISOR          = 10.0;

inline double sanitizeSensitivity(double sensitivity) {
  return std::isfinite(sensitivity) && sensitivity > 0.0 ? sensitivity : 1.0;
}

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

inline double errorScheduledGain(double gain, int error, bool maintaining) {
  const int absoluteError = std::abs(error);
  if (absoluteError < 10 || !maintaining) return gain * 0.25;
  if (absoluteError < 50) return gain * 0.75;
  if (absoluteError > 100) return gain * 1.25;
  return gain;
}

inline double clampGain(double gain, double sensitivity) {
  sensitivity             = sanitizeSensitivity(sensitivity);
  const double minimumGain = sensitivity * GAIN_MIN_SENSITIVITY_RATIO;
  const double maximumGain = sensitivity * GAIN_MAX_SENSITIVITY_RATIO;
  return std::max(minimumGain, std::min(gain, maximumGain));
}

}  // namespace ErgControl
