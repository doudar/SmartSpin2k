/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <stdint.h>
#include <math.h>

// Hardware-independent policies, shared by firmware and native safety tests.
namespace ThermalSafety {
constexpr uint32_t POLL_INTERVAL_MS = 10000;
constexpr uint32_t TMC_COOLDOWN_MS  = 30000;
constexpr float S3_RADIO_LIMIT_C    = 70.0f;
constexpr float S3_RADIO_RECOVERY_C = 68.0f;
constexpr float S3_STOP_C           = 80.0f;
constexpr float S3_MOTOR_RECOVERY_C = 78.0f;

enum class TmcState { Normal, Reduced, Disabled };

struct TmcProtection {
  TmcState state    = TmcState::Normal;
  uint32_t hotSince = 0;

  void update(bool valid, bool hot, bool shutdown, uint32_t now) {
    if (valid && !hot && !shutdown) {
      state = TmcState::Normal;
    } else if (valid && shutdown) {
      state = TmcState::Disabled;
    } else if (valid && hot && state == TmcState::Normal) {
      hotSince = now;
      state    = TmcState::Reduced;
    }
    // Missing telemetry never clears a hot latch or restarts its deadline.
    if (state == TmcState::Reduced && uint32_t(now - hotSince) >= TMC_COOLDOWN_MS) state = TmcState::Disabled;
  }

  int percent() const { return state == TmcState::Normal ? 100 : 50; }
  bool disabled() const { return state == TmcState::Disabled; }
};

struct S3Protection {
  bool radiosReduced = false;
  bool motorStopped  = false;
  bool sensorValid   = false;
  int motorPercent   = 100;

  void update(float temperature) {
    sensorValid = isfinite(temperature);
    if (!sensorValid) return;  // Preserve thermal latches; disabled() also guards sensor failure.
    if (temperature >= S3_RADIO_LIMIT_C)
      radiosReduced = true;
    else if (temperature < S3_RADIO_RECOVERY_C)
      radiosReduced = false;
    if (temperature > S3_STOP_C)
      motorStopped = true;
    else if (temperature <= S3_MOTOR_RECOVERY_C)
      motorStopped = false;
    float percent = 100.0f - (temperature - S3_RADIO_LIMIT_C) * 50.0f / (S3_STOP_C - S3_RADIO_LIMIT_C);
    motorPercent  = percent >= 100 ? 100 : (percent <= 50 ? 50 : static_cast<int>(percent));
  }

  bool disabled() const { return !sensorValid || motorStopped; }
};

inline int limitedCurrent(int requested, int tmcPercent, int s3Percent) {
  int percent = tmcPercent < s3Percent ? tmcPercent : s3Percent;
  return requested > 0 ? static_cast<int>(int64_t(requested) * percent / 100) : 0;
}
}  // namespace ThermalSafety
