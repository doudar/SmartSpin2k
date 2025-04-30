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

class TestResults {
 public:
  struct Neighbor {
    unsigned int found : 1;
    unsigned int passedTest : 1;
    int8_t i;
    int8_t j;
    int16_t targetPosition;

    Neighbor() {
      found          = false;
      passedTest     = false;
      i              = INT8_MIN;
      j              = INT8_MIN;
      targetPosition = INT16_MIN;
    }
  };

 public:
  Neighbor leftNeighbor;
  Neighbor rightNeighbor;
  Neighbor topNeighbor;
  Neighbor bottomNeighbor;
  unsigned int allNeighborsFound : 1;
  unsigned int allNeighborsPassed : 1;

  TestResults() {
    allNeighborsFound  = false;
    allNeighborsPassed = false;
  }
};

class CubicSpline {
  public:
   void set_points(const float* x_vals, const float* y_vals, size_t n);
   float interpolate(float x_val) const; 
   float extrapolate(float x_val) const;
   bool shouldUseNaturalSpline(const float* x, const float* y, size_t n);
 
  private:
   std::vector<float> x, y, h, alpha, l, mu, z, c, b, d;
 };

class PTHelpers {
 public:
  TestResults testNeighbors(int i, int j, int value, PTData& ptData);
  int32_t lookup(int watts, int cad, PTData& ptData);
  void fillEmptyTable(int outerValue, const std::vector<int>& emptyIndices, const float* x, const float* y, size_t n, bool horizontal, bool useNaturalSpline, PTData& ptData);
  void extrapolateEmptyIndices(int outerIndex, const std::vector<int>& emptyIndices, const float* x, const float* y, size_t n, bool horizontal, bool naturalSpline, PTData& ptData);
  void extrapFillTableDirection(bool horizontal, PTData& ptData);
  float linearExtrapolate(const float* x, const float* y, size_t n, float j);
  float linearInterpolate(const float* x, const float* y, size_t n, float j);
  void findTableDirection(bool horizontal, PTData& ptData);
  void fillTable(PTData& ptData);
  int32_t lookupWatts(int cad, int32_t targetPosition, PTData& ptData);
  void extrapolateDiagonalEntries(const std::vector<std::pair<int, int>>& emptyIndices, const float* x, const float* y, size_t n, PTData& ptData);
  void extrapolateDiagonal(PTData& ptData);
};