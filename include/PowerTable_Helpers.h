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

class PowerTableSlopeStatus {
 public:
  enum Value : uint8_t {
    Trusted,
    InvalidRequest,
    InsufficientRows,
    MissingLocalSupport,
    InconsistentRows,
  };

  static const char* name(Value status) {
    switch (status) {
      case Trusted: return "trusted";
      case InvalidRequest: return "invalid request";
      case InsufficientRows: return "one supporting row";
      case MissingLocalSupport: return "missing local segment";
      case InconsistentRows: return "inconsistent rows";
    }
    return "unknown";
  }
};

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
 private:
  void clean(PTData& ptData);
  bool enforceMonotonicAcrossCadence(PTData& ptData);
  bool enforceMonotonicAcrossPower(PTData& ptData);
  int32_t invertForwardSurface(int cad, int32_t targetPosition, PTData& ptData);

 public:
  int32_t lookup(int watts, int cad, PTData& ptData);
  bool lookupSlope(int watts, int cad, double& stepsPerWatt, PTData& ptData, PowerTableSlopeStatus::Value* status = nullptr);
  bool lookupErgSlope(int watts, int cad, double& stepsPerWatt, PTData& ptData, PowerTableSlopeStatus::Value* status = nullptr);
  int32_t lookupWatts(int cad, int32_t targetPosition, PTData& ptData);
  int getTotalReadings(PTData& ptData);
  ptIndex calculateIndex(int watts, int cad);
  bool cadenceIsWithinTable(int cad);
  void enterData(PTData& ptData, ptIndex index, int pos);
};
