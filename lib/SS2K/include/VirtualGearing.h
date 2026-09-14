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

// Sorted chainring / sprocket ratios, stored in thousandths.
struct Gears {
  // An empty profile selects the default unlimited, fixed-spacing mode.
  uint8_t count = 0;
  uint16_t ratios[MAX_GEARS] = {};

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
  Gears() { updateSpacing(); }

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
      count = 0;
      medianGapTwice = 0;
      std::fill(ratios, ratios + MAX_GEARS, 0);
      return true;
    }
    if (!values || size < 2 || size > MAX_GEARS) return false;
    for (size_t i = 0; i < size; ++i) {
      if (values[i] < 500 || values[i] > 6000 || (i && values[i] < values[i - 1])) return false;
    }
    // Validate the complete profile before changing the live copy.
    std::memmove(ratios, values, size * sizeof(uint16_t));
    std::fill(ratios + size, ratios + MAX_GEARS, 0);
    count = static_cast<uint8_t>(size);
    updateSpacing();
    return true;
  }

  bool decode(const uint8_t* data, size_t length) {
    if (!data || length < 1 || data[0] > MAX_GEARS || length != 1u + 2u * data[0]) return false;
    uint16_t values[MAX_GEARS];
    for (size_t i = 0; i < data[0]; ++i) values[i] = get_le16(data + 1 + 2 * i);
    return assign(values, data[0]);
  }

  bool unlimited() const { return count == 0; }
  int startGear() const { return unlimited() ? 0 : 1; }
  int clampGear(int gear) const { return unlimited() ? gear : std::max(1, std::min(gear, static_cast<int>(count))); }
  bool operator==(const Gears& other) const { return count == other.count && std::memcmp(ratios, other.ratios, count * sizeof(uint16_t)) == 0; }
};

}  // namespace VirtualGearing
