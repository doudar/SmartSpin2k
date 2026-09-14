/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "ByteUtils.h"

namespace RoadSimulation {

constexpr size_t MAX_GEARS = 26;
constexpr float BIKE_MASS_KG = 10.0f;
constexpr float WHEEL_CIRCUMFERENCE_M = 2.105f;
constexpr float DRIVETRAIN_EFFICIENCY = 0.97f;
constexpr uint32_t CADENCE_TIMEOUT_MS = 3000;
constexpr uint32_t UPDATE_INTERVAL_MS = 100;

// Ratios are chainring teeth / sprocket teeth, in thousandths. Sorted effective
// ratios accommodate 1x and 2x drivetrains without a front-shifter protocol.
struct Gears {
  uint8_t count = 24;
  // 50/34 chainrings, 11-12-13-14-15-17-19-21-24-28-30-34 cassette.
  uint16_t ratios[MAX_GEARS] = {1000, 1133, 1214, 1417, 1471, 1619, 1667, 1786, 1789, 2000, 2083, 2267,
                                2381, 2429, 2615, 2632, 2833, 2941, 3091, 3333, 3571, 3846, 4167, 4545};

  bool assign(const uint16_t* values, size_t size) {
    if (!values || size < 2 || size > MAX_GEARS) return false;
    for (size_t i = 0; i < size; ++i) {
      if (values[i] < 500 || values[i] > 6000 || (i && values[i] < values[i - 1])) return false;
    }
    // Validate the complete profile before changing the live copy.
    std::memmove(ratios, values, size * sizeof(uint16_t));
    std::fill(ratios + size, ratios + MAX_GEARS, 0);
    count = static_cast<uint8_t>(size);
    return true;
  }

  bool decode(const uint8_t* data, size_t length) {
    if (!data || length < 1 || data[0] > MAX_GEARS || length != 1u + 2u * data[0]) return false;
    uint16_t values[MAX_GEARS];
    for (size_t i = 0; i < data[0]; ++i) values[i] = get_le16(data + 1 + 2 * i);
    return assign(values, data[0]);
  }

  int clampGear(int gear) const { return std::max(1, std::min(gear, static_cast<int>(count))); }
  float ratio(int gear) const { return ratios[clampGear(gear) - 1] / 1000.0f; }
  bool operator==(const Gears& other) const { return count == other.count && std::memcmp(ratios, other.ratios, count * sizeof(uint16_t)) == 0; }
};

inline bool validRiderWeight(float kg) { return std::isfinite(kg) && kg >= 20.0f && kg <= 250.0f; }

struct Parameters {
  float gradePercent = 0.0f;
  float windSpeedMps = 0.0f;  // Positive is a headwind.
  float rollingResistance = 0.004f;
  float windResistance = 0.51f;  // FTMS Cw = rho * CdA, kg/m; drag includes a factor of 1/2.

  // FTMS opcode 0x11 followed by the six-byte Simulation Parameter Array.
  // Zeros are valid supplied coefficients, not missing-value sentinels.
  bool decode(const uint8_t* data, size_t length) {
    if (!data || length != 7 || data[0] != 0x11) return false;
    windSpeedMps = get_le16s(data + 1) * 0.001f;
    gradePercent = get_le16s(data + 3) * 0.01f;
    rollingResistance = data[5] * 0.0001f;
    windResistance = data[6] * 0.01f;
    return true;
  }
};

inline float roadSpeed(float cadence, float ratio) { return cadence * ratio * WHEEL_CIRCUMFERENCE_M / 60.0f; }

// Steady road load only: cadence arrives too slowly to infer useful inertia.
inline float brakeWatts(float cadence, float ratio, float speedMps, float riderKg, const Parameters& parameters) {
  if (!validRiderWeight(riderKg) || !std::isfinite(cadence) || cadence <= 0 || cadence >= 250 || !std::isfinite(ratio) || ratio < 0.5f || ratio > 6.0f ||
      !std::isfinite(speedMps) || speedMps < 0 || !std::isfinite(parameters.gradePercent) || !std::isfinite(parameters.windSpeedMps) ||
      !std::isfinite(parameters.rollingResistance) || parameters.rollingResistance < 0 || !std::isfinite(parameters.windResistance) || parameters.windResistance < 0) return 0;
  const float angle = std::atan(parameters.gradePercent / 100.0f);
  const float weight = (riderKg + BIKE_MASS_KG) * 9.80665f;
  const float airspeed = speedMps + parameters.windSpeedMps;
  const float force = weight * (std::sin(angle) + parameters.rollingResistance * std::cos(angle)) +
                      0.5f * parameters.windResistance * airspeed * std::fabs(airspeed);
  // F * r * G is crank torque; multiplying by crank angular velocity gives
  // this expression. Negative force means assistance, unavailable from a brake.
  return std::max(0.0f, std::min(4000.0f, force * roadSpeed(cadence, ratio) / DRIVETRAIN_EFFICIENCY));
}

// Smooth only the speed used for aerodynamic load. Hold it for the delayed
// cadence sample after a shift, then settle toward cadence-derived speed.
// This is not a momentum simulation and is never advertised as measured speed.
class SpeedFilter {
  bool initialized = false;
  float speed = 0;
  float previousRatio = 0;
  uint32_t lastUpdate = 0;
  uint32_t shiftedAt = 0;
  bool shifting = false;

 public:
  void reset() { initialized = false; }
  float update(float cadence, float ratio, uint32_t now) {
    const float requested = roadSpeed(cadence, ratio);
    if (!initialized) {
      initialized = true;
      speed = requested;
      previousRatio = ratio;
      lastUpdate = now;
      shifting = false;
    }
    const uint32_t elapsed = std::min<uint32_t>(now - lastUpdate, 1000);
    lastUpdate = now;
    if (ratio != previousRatio) {
      previousRatio = ratio;
      shiftedAt = now;
      shifting = true;
    }
    if (shifting && now - shiftedAt < 1000) return speed;
    shifting = false;
    speed += (requested - speed) * elapsed / (2000.0f + elapsed);
    return speed;
  }
};

// A bounded one-second shift sensation, not measured acceleration/inertia.
// Retrigger from the latest ratio; multiple quick shifts never stack impulses.
class ShiftFeel {
  float previousRatio = 0;
  float amount = 0;
  uint32_t shiftedAt = 0;

 public:
  void reset() { previousRatio = 0; amount = 0; }
  float update(float ratio, uint32_t now) {
    if (previousRatio > 0 && ratio != previousRatio) {
      amount = std::max(-0.05f, std::min(0.05f, (ratio / previousRatio - 1.0f) * 0.5f));
      shiftedAt = now;
    }
    previousRatio = ratio;
    const uint32_t elapsed = now - shiftedAt;
    if (elapsed >= 1000) amount = 0;
    return 1.0f + amount * (1.0f - std::min<uint32_t>(elapsed, 1000) / 1000.0f);
  }
};

enum class Status : uint8_t { Off, NoCadence, Unhomed, NoTable, Active };
inline const char* statusName(Status status) {
  switch (status) {
    case Status::Off: return "off";
    case Status::NoCadence: return "waiting for cadence";
    case Status::Unhomed: return "homing required";
    case Status::NoTable: return "power table required";
    case Status::Active: return "active";
  }
  return "off";
}

}  // namespace RoadSimulation
