/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "ByteUtils.h"

// A small, immutable map in the PTAB coordinate frame, with a repeatable middle crossing.
// Never learn this map from drifting ride positions or extrapolate into the ends.
namespace FtmsCalibration {
constexpr int COUNT = 3;
constexpr int FIRST = 33;
constexpr int GAP = 17;
constexpr size_t WIRE_SIZE = 32;
constexpr int REFERENCE_LEVEL = 50;
constexpr uint8_t REFERENCE_LEVEL2 = 2 * REFERENCE_LEVEL + 1;  // Downward 51 -> 50 boundary.
constexpr uint32_t MAGIC = 0x334d5446;  // FTM3: fixed, downward middle reference.
constexpr uint32_t STABLE_MS = 10000;
constexpr uint32_t OFFSET_STABLE_MS = 30000;
constexpr uint32_t INTERVAL_MS = 60000;
constexpr uint32_t FRESH_MS = 2500;

inline uint32_t checksum(const uint8_t* bytes, size_t size) {
  uint32_t hash = 2166136261u;
  while (size--) hash = (hash ^ *bytes++) * 16777619u;
  return hash;
}

inline uint32_t identity(const char* name, bool direction) {
  uint32_t hash = 2166136261u;
  while (*name) {
    uint8_t c = static_cast<uint8_t>(*name++);
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    hash = (hash ^ c) * 16777619u;
  }
  return (hash ^ static_cast<uint8_t>(direction)) * 16777619u;
}

struct Map {
  uint32_t source = 0;
  int32_t maximum = 0;
  int32_t position[COUNT] = {};
  uint8_t level2[COUNT] = {66, REFERENCE_LEVEL2, 134};  // Twice resistance; crossings lie between levels.

  bool valid() const {
    if (!source || maximum <= 0 || level2[1] != REFERENCE_LEVEL2) return false;
    for (int i = 0; i < COUNT; ++i) {
      if (position[i] <= 0 || position[i] >= maximum) return false;
      const int nominal = 2 * (FIRST + i * GAP);
      if (level2[i] < nominal - 4 || level2[i] > nominal + 4) return false;
      if (i && static_cast<int64_t>(position[i]) - position[i - 1] < (level2[i] - level2[i - 1]) * 10) return false;
    }
    return true;
  }
  bool matches(uint32_t device, int32_t range) const { return valid() && source == device && maximum == range; }

  // Interpolate only inside measured support. A full local level covers the
  // two quantized observations; 40 extra steps is a policy allowance for noise.
  bool estimateHalf(int resistance2, int32_t& center, int32_t& uncertainty) const {
    return estimatePosition(resistance2, center, uncertainty, false);
  }
  // Ride-time correction includes a short extension to 30--70. Samples can
  // land at 31--35 / 65--69; an R32 reading must not silently disable syncing.
  // Startup uses the fixed middle crossing; the unreliable ends stay out of sync.
  bool estimateForSync(int resistance2, int32_t& center, int32_t& uncertainty) const {
    return estimatePosition(resistance2, center, uncertainty, true);
  }
  bool estimatePosition(int resistance2, int32_t& center, int32_t& uncertainty, bool extend) const {
    if (!valid() || resistance2 < 0 || resistance2 > 200) return false;
    int outside = resistance2 < level2[0] ? level2[0] - resistance2 :
                  (resistance2 > level2[COUNT - 1] ? resistance2 - level2[COUNT - 1] : 0);
    if (outside && (!extend || resistance2 < 60 || resistance2 > 140 || outside > 10)) return false;
    int index = resistance2 > level2[1] ? 1 : 0;
    int64_t span = static_cast<int64_t>(position[index + 1]) - position[index];
    int gap = level2[index + 1] - level2[index];
    center = static_cast<int32_t>(position[index] + span * (resistance2 - level2[index]) / gap);
    uncertainty = static_cast<int32_t>(span * 2 / gap + 40);
    // Increase the deadband by half a local bin per extrapolated level.
    uncertainty += static_cast<int32_t>(span * outside / (2 * gap));
    return center > uncertainty && static_cast<int64_t>(center) + uncertainty < maximum;
  }

  void encode(uint8_t* bytes) const {
    put_le32(bytes, MAGIC);
    put_le32(bytes + 4, source);
    put_le32s(bytes + 8, maximum);
    for (int i = 0; i < COUNT; ++i) put_le32s(bytes + 12 + 4 * i, position[i]);
    for (int i = 0; i < COUNT; ++i) bytes[24 + i] = level2[i];
    bytes[27] = 0;
    put_le32(bytes + 28, checksum(bytes, 28));
  }
  bool decode(const uint8_t* bytes, size_t size) {
    *this = Map{};
    if (size != WIRE_SIZE || get_le32(bytes) != MAGIC || bytes[27] || get_le32(bytes + 28) != checksum(bytes, 28)) return false;
    Map candidate;
    candidate.source = get_le32(bytes + 4);
    candidate.maximum = get_le32s(bytes + 8);
    for (int i = 0; i < COUNT; ++i) candidate.position[i] = get_le32s(bytes + 12 + 4 * i);
    for (int i = 0; i < COUNT; ++i) candidate.level2[i] = bytes[24 + i];
    if (!candidate.valid()) return false;
    *this = candidate;
    return true;
  }
};

class DriftGuard {
 public:
  enum class State { Ineligible, Stale, Settling, OutsideMap, Deadband, ConfirmingOffset, Cooldown, Corrected };
  bool uncertain() const { return uncertain_; }
  const char* reason() const {
    switch (state_) {
      case State::Ineligible: return "motion/control interlock";
      case State::Stale: return "stale resistance";
      case State::Settling: return "confirming stationary feedback";
      case State::OutsideMap: return "outside 30-70 map support";
      case State::Deadband: return "within sensor deadband";
      case State::ConfirmingOffset: return "confirming larger offset (30s)";
      case State::Cooldown: return "waiting for correction interval";
      case State::Corrected: return "corrected";
    }
    return "unknown";
  }
  void interrupt() { tracking_ = false; uncertain_ = false; state_ = State::Ineligible; }
  // Feed every maintenance pass. Interruptions discard stationary evidence,
  // but do not restart the minute timer or forget a recently applied correction.
  int correction(const Map& map, uint32_t now, uint32_t sampleTime, int resistance, int32_t position, bool eligible) {
    if (!eligible || resistance < 0 || resistance > 100 || position <= 0 || position >= map.maximum) { interrupt(); return 0; }
    if (now - sampleTime > FRESH_MS) { interrupt(); state_ = State::Stale; return 0; }
    int32_t center, uncertainty;
    uncertain_ = map.estimateForSync(2 * resistance, center, uncertainty) &&
                 (static_cast<int64_t>(center) - position > uncertainty || static_cast<int64_t>(position) - center > uncertainty);
    if (!tracking_ || resistance < high_ - 1 || resistance > low_ + 1 ||
        static_cast<int64_t>(position) - position_ > 5 || static_cast<int64_t>(position_) - position > 5) {
      tracking_ = true;
      since_ = now;
      low_ = high_ = resistance;
      position_ = position;
      reports_ = 1;
      sum_ = resistance;
      timestamp_ = sampleTime;
      state_ = State::Settling;
      return 0;
    }
    if (sampleTime != timestamp_) {
      timestamp_ = sampleTime;
      if (resistance < low_) low_ = resistance;
      if (resistance > high_) high_ = resistance;
      if (reports_ < UINT16_MAX) { ++reports_; sum_ += resistance; }
    }
    if (now - since_ < STABLE_MS || reports_ < 9) { state_ = State::Settling; return 0; }
    const int resistance2 = static_cast<int>((2 * sum_ + reports_ / 2) / reports_);
    if (!map.estimateForSync(resistance2, center, uncertainty)) { state_ = State::OutsideMap; return 0; }
    int64_t error = static_cast<int64_t>(center) - position;
    if (error <= uncertainty && error >= -uncertainty) { uncertain_ = false; state_ = State::Deadband; return 0; }
    uncertain_ = true;
    // A lifted/reseated knob can create a larger offset without moving the
    // stepper. Confirm it longer, then rebase to the map instead of rejecting
    // it forever or spending many minutes correcting 100 steps at a time.
    if ((error > 3LL * uncertainty || error < -3LL * uncertainty) && (now - since_ < OFFSET_STABLE_MS || reports_ < 25)) {
      state_ = State::ConfirmingOffset;
      return 0;
    }
    if (haveCorrection_ && now - lastCorrection_ < INTERVAL_MS) { state_ = State::Cooldown; return 0; }
    lastCorrection_ = now;
    haveCorrection_ = true;
    tracking_ = false;
    uncertain_ = false;
    state_ = State::Corrected;
    return static_cast<int>(error); // Both coordinates are positive, bounded int32 values.
  }
 private:
  bool tracking_ = false, haveCorrection_ = false, uncertain_ = false;
  State state_ = State::Ineligible;
  uint32_t since_ = 0, lastCorrection_ = 0, timestamp_ = 0, sum_ = 0;
  int low_ = 0, high_ = 0;
  int32_t position_ = 0;
  uint16_t reports_ = 0;
};
}  // namespace FtmsCalibration
