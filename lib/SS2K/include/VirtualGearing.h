/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "ByteUtils.h"

namespace VirtualGearing {
constexpr size_t MAX_GEARS = 26;
constexpr uint8_t CUSTOM_TEETH = 0;
constexpr uint8_t ZWIFT_MIXED_TERRAIN = 1;

// Tooth pairs are front * 100 + rear (5332 = 53x32). The named virtual preset
// supplies ratios without teeth. Both use thousandths for the motor mapping.
struct Gears {
  // An empty profile selects the default unlimited, fixed-spacing mode.
  uint8_t count = 0;
  uint8_t preset = CUSTOM_TEETH;
  uint16_t ratios[MAX_GEARS] = {};
  uint16_t teeth[MAX_GEARS] = {};

  struct Selection {
    uint8_t front = 0, rear = 0, frontCount = 0, rearCount = 0;
  };

 private:
  uint16_t medianGapTwice = 0;
  void updateSpacing() {
    uint16_t gaps[MAX_GEARS - 1];
    size_t n = 0;
    for (size_t i = 1; i < count; ++i) {
      const uint16_t gap = ratios[i] - ratios[i - 1];
      if (gap) gaps[n++] = gap;
    }
    std::sort(gaps, gaps + n);
    medianGapTwice = n == 0 ? 0 : (n % 2 ? 2 * gaps[n / 2] : gaps[n / 2 - 1] + gaps[n / 2]);
  }

 public:
  Gears() = default;

  bool assignPreset(uint16_t id) {
    if (id == CUSTOM_TEETH) return assign(nullptr, 0);
    if (id != ZWIFT_MIXED_TERRAIN) return false;
    Gears next;
    // Exact decimal ratios from Zwift's gearing/GEAR24MAN.xml (2026-09-26).
    // A synthetic 1x24 cassette has no physical tooth pairs to invent/store.
    static const uint16_t mixed[] = {750, 870, 990, 1110, 1230, 1380, 1530, 1680, 1860, 2040, 2220, 2400,
                                    2610, 2820, 3030, 3240, 3490, 3740, 3990, 4240, 4540, 4840, 5140, 5490};
    next.count = sizeof(mixed) / sizeof(mixed[0]);
    next.preset = ZWIFT_MIXED_TERRAIN;
    std::copy(mixed, mixed + next.count, next.ratios);
    next.updateSpacing();
    *this = next;
    return true;
  }

  // Zero gaps are excluded from the median; duplicate gears share a position.
  // Calculate from gear 1 each time so repeated shifts cannot accumulate rounding.
  int32_t offsetSteps(int gear, int shiftStep) const {
    if (unlimited()) {
      const int64_t steps = static_cast<int64_t>(gear) * shiftStep;
      return static_cast<int32_t>(std::max<int64_t>(INT32_MIN, std::min<int64_t>(INT32_MAX, steps)));
    }
    if (!medianGapTwice) return 0;
    const int64_t numerator = static_cast<int64_t>(ratios[clampGear(gear) - 1] - ratios[0]) * shiftStep * 2;
    const int64_t steps = numerator >= 0 ? (numerator + medianGapTwice / 2) / medianGapTwice : -((-numerator + medianGapTwice / 2) / medianGapTwice);
    return static_cast<int32_t>(std::max<int64_t>(INT32_MIN, std::min<int64_t>(INT32_MAX, steps)));
  }

  bool assign(const uint16_t* values, size_t size) {
    if (size == 0) {
      *this = Gears{};
      return true;
    }
    if (!values || size < 2 || size > MAX_GEARS) return false;
    Gears next;
    for (size_t i = 0; i < size; ++i) {
      const unsigned front = values[i] / 100, rear = values[i] % 100;
      if (!front || front > 99 || !rear) return false;
      const unsigned ratio = (front * 1000 + rear / 2) / rear;
      if (ratio < 500 || ratio > 6000) return false;
      for (size_t j = 0; j < i; ++j) if (values[j] == values[i]) return false;
      next.teeth[i] = values[i];
    }
    next.count = static_cast<uint8_t>(size);
    // Stable, deterministic ordering for equal rounded ratios: smaller ring first.
    std::sort(next.teeth, next.teeth + size, [](uint16_t a, uint16_t b) {
      const unsigned ar = ((a / 100) * 1000 + (a % 100) / 2) / (a % 100);
      const unsigned br = ((b / 100) * 1000 + (b % 100) / 2) / (b % 100);
      return ar == br ? a < b : ar < br;
    });
    for (size_t i = 0; i < size; ++i) {
      const unsigned front = next.teeth[i] / 100, rear = next.teeth[i] % 100;
      next.ratios[i] = (front * 1000 + rear / 2) / rear;
    }
    next.updateSpacing();
    *this = next;
    return true;
  }

  Selection selection(int gear) const {
    Selection selected;
    if (gear < 1 || gear > count) return selected;
    if (preset == ZWIFT_MIXED_TERRAIN) {
      selected.front = selected.frontCount = 1;
      selected.rear = gear;
      selected.rearCount = count;
      return selected;
    }
    bool fronts[100] = {}, rears[100] = {};
    for (size_t i = 0; i < count; ++i) {
      fronts[teeth[i] / 100] = true;
      rears[teeth[i] % 100] = true;
    }
    const unsigned front = teeth[gear - 1] / 100, rear = teeth[gear - 1] % 100;
    for (unsigned i = 1; i < 100; ++i) {
      if (fronts[i]) { ++selected.frontCount; if (i <= front) ++selected.front; }
      if (rears[i]) { ++selected.rearCount; if (i >= rear) ++selected.rear; }
    }
    return selected;
  }

  bool decode(const uint8_t* data, size_t length) {
    if (!data || length < 1 || data[0] > MAX_GEARS || length != 1u + 2u * data[0]) return false;
    uint16_t values[MAX_GEARS];
    for (size_t i = 0; i < data[0]; ++i) values[i] = get_le16(data + 1 + 2 * i);
    return assign(values, data[0]);
  }

  bool unlimited() const { return count == 0; }
  uint32_t ratioX10000(int gear) const {
    if (gear < 1 || gear > count) return 0;
    const uint16_t pair = teeth[gear - 1];
    return preset == CUSTOM_TEETH ? static_cast<uint32_t>(pair / 100) * 10000 / (pair % 100) : ratios[gear - 1] * 10u;
  }
  // Absolute app ratio -> local selection. Preserve the current gear on ties
  // (two tooth pairs may have the same ratio); otherwise prefer the lower gear.
  // Return zero for Unlimited, which has no ratio table to synchronize against.
  int closestGear(uint32_t ratioX10000, int currentGear) const {
    if (unlimited() || !ratioX10000) return 0;
    auto distance = [&](int gear) -> uint64_t {
      const uint32_t ratio = this->ratioX10000(gear);
      return ratioX10000 > ratio ? static_cast<uint64_t>(ratioX10000) - ratio : static_cast<uint64_t>(ratio) - ratioX10000;
    };
    int best = currentGear >= 1 && currentGear <= count ? currentGear : 1;
    uint64_t bestDistance = distance(best);
    for (int gear = 1; gear <= count; ++gear) {
      const uint64_t candidate = distance(gear);
      if (candidate < bestDistance) { best = gear; bestDistance = candidate; }
    }
    return best;
  }
  int startGear() const { return unlimited() ? 8 : std::max(1, count / 3); }
  int clampGear(int gear) const { return unlimited() ? gear : std::max(1, std::min(gear, static_cast<int>(count))); }
  bool operator==(const Gears& other) const {
    return preset == other.preset && count == other.count && std::memcmp(teeth, other.teeth, count * sizeof(uint16_t)) == 0;
  }
};

}  // namespace VirtualGearing
