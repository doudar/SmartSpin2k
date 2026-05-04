/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "SmartSpin_parameters.h"

#define PTDATA_LOG_TAG "PTData"

#define RETURN_ERROR               INT32_MIN

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

class ResistanceModel {
 private:
  // Coefficients (Normalizing makes these fit in float/double safely)
  // Model: Z = b0 + b1*x + b2*y + b3*x^2 + b4*y^2 + b5*x*y
  double b[6]      = {0};
  bool isQuadratic = false;
  bool isValid     = false;

  // Normalization bounds (to keep math stable)
  double minW = 0, maxW = 1;
  double minR = 0, maxR = 1;

  // Helper: Normalize a value to 0.0 - 1.0 range
  double normW(double w) { return (w - minW) / (maxW - minW); }
  double normR(double r) { return (r - minR) / (maxR - minR); }
  bool solveMatrix(double A[6][6], double B[6], int n);

 public:
  void fit(const PTData& data);
  int16_t predict(double watts, double rpm);
  int predictWatts(int32_t resistance, float cadence);
  bool getIsValid() { return isValid; }
};

class PTHelpers {
 public:
  ResistanceModel resistanceModel;
  int32_t lookup(int watts, int cad, PTData& ptData);
  int lookupWatts(int cad, int32_t targetPosition, PTData& ptData);
  int getTotalReadings(PTData& ptData);
  ptIndex calculateIndex(int watts, int cad);
  void enterData(PTData& ptData, ptIndex index, int pos);
  void clean(PTData& ptData);
  void fill(PTData& ptData);
  void fillGaps(PTData& ptData);
  bool fillAllWattColumns(PTData& ptData);
  bool fillAllCadenceLines(PTData& ptData);
};