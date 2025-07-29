/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "SmartSpin_parameters.h"
#include <vector>

#define PTDATA_LOG_TAG "PTData"

#define RETURN_ERROR               INT32_MIN
#define FREE_HEAP_FOR_COMPLEX_MATH 30000
#define COMPUTATION_TIMEOUT_MS 25

class PowerEntry {
 public:
  int watts;
  int resistance;
  int32_t targetPosition;
  int cad;
  int readings;

  PowerEntry() {
    this->watts          = 0;
    this->targetPosition = 0;
    this->cad            = 0;
    this->readings       = 0;
    this->resistance     = 0;
  }
};

struct ptIndex {
  int8_t wattIndex;
  int8_t cadIndex;
  ptIndex() {
    this->wattIndex = INT8_MIN;
    this->cadIndex  = INT8_MIN;
  }

  bool operator==(const ptIndex& other) const { return wattIndex == other.wattIndex && cadIndex == other.cadIndex; }
};

class PowerBuffer {
 public:
  PowerEntry powerEntry[POWER_SAMPLES];
  void set(int);
  void reset();
  int getReadings();
};

// Simplifying the table to save memory since we no longer need watts and cad.
class TableEntry {
 public:
  int16_t targetPosition;
  int8_t readings;

  TableEntry() {
    this->targetPosition = INT16_MIN;
    this->readings       = 0;
  }
};

// Combine Entries to make a row.
class TableRow {
 public:
  TableEntry tableEntry[POWERTABLE_WATT_SIZE];
};

// Combine rows to make a table.
class PTData {
 public:
  TableRow tableRow[POWERTABLE_CAD_SIZE];
};

class PTHelpers {
 public:
  int32_t lookup(int watts, int cad, PTData& ptData);
  float linearExtrapolate(std::pair<std::vector<float>, std::vector<float>> xy, size_t n, float j);
  int32_t lookupWatts(int cad, int32_t targetPosition, PTData& ptData);
  int32_t extrapolateCadenceWatts(int cad, float targetPosition, PTData& ptData);
  int extrapolateWattsFromCadence(int cad, int32_t targetPosition, PTData& ptData);
    // return number of readings in the table. If minReadings is set, it will only count entries with at least that many readings.
  int getNumEntries(PTData& ptData, int minReadings = 0);
  int getTotalReadings(PTData& ptData);
  ptIndex calculateIndex(int watts, int cad);
  void enterData(PTData& ptData,ptIndex index, int pos);
  void clean(PTData& ptData);
  bool fillAllWattColumns(PTData& ptData);
  bool fillAllCadenceLines(PTData& ptData);
  std::pair<std::vector<float>, std::vector<float>> getRow(int row, PTData& ptData);
  std::pair<std::vector<float>, std::vector<float>> getColumn(int column, PTData& ptData);
};