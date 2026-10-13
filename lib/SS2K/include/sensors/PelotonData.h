/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "SensorData.h"

class PelotonData : public SensorData {
 public:
  PelotonData() : SensorData("PTON") {}

  bool hasHeartRate() override;
  bool hasCadence() override;
  bool hasPower() override;
  bool hasSpeed() override;
  bool hasResistance() override;
  int getHeartRate() override;
  float getCadence() override;
  int getPower() override;
  float getSpeed() override;
  int getResistance() override;
  void decode(uint8_t *data, size_t length) override;
  // Explicit clock keeps window boundaries and timer rollover deterministic in tests.
  void decode(uint8_t *data, size_t length, uint32_t now);

 private:
  static constexpr uint32_t averageWindowMs = 1000;
  struct Accumulator {
    uint64_t sum   = 0;
    uint32_t count = 0;
    void add(uint32_t value) {
      sum += value;
      ++count;
    }
    float average() const { return static_cast<float>(sum) / count; }
  };
  Accumulator pendingPower, pendingCadence, pendingResistance;
  uint32_t windowStart = 0;
  bool collecting = false;
  bool receivedCadence = false;
  bool receivedPower   = false;
  float cadence        = nanf("");
  int power            = INT_MIN;
  int resistance       = INT_MIN;
  bool receivedResistance = false;
};
