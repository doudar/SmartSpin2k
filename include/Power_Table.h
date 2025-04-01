/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "settings.h"
#include "SmartSpin_parameters.h"
#include <vector> 

#define POWERTABLE_LOG_TAG   "PTable"
#define RETURN_ERROR         INT32_MIN

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

class PowerTable {
 public:
  bool saveFlag = false;
  bool _hasBeenLoadedThisSession = false;
  TableRow tableRow[POWERTABLE_CAD_SIZE];

  // Pick up new power value and put them into the power table
  void processPowerValue(PowerBuffer& powerBuffer, int cadence, Measurement power);

  // Sets stepper min/max value from power table
  void setStepperMinMax();

  // Catalogs a new entry into the power table.
  void newEntry(PowerBuffer& powerBuffer);

  // enters data into power table 
  void enterData(int i, int j, int pos); 

  void downVoteData(int i, int j, float target, int neighbor); 

  // returns incline for wattTarget. Null if not found.
  int32_t lookup(int watts, int cad);

  // returns 
  int32_t splineLookup(int watts, int cad); 

  // returns watts for given cadence and target position. Returns RETURN_ERROR if not found.
  int32_t lookupWatts(int cad, int32_t targetPosition);

  // automatically load or save the Power Table
  bool _manageSaveState(bool canSkipReliabilityChecks = false);

  // save powertable from littlefs
  bool _save();

  // Reset the active power table and delete the saved power table.
  bool reset();

  // return number of readings in the table.
  int getNumReadings();

  // Display power table in log
  void toLog();

  void fillEmptyTable(int outerValue, const std::vector<int>& emptyIndices, const float* x, const float* y, size_t n, bool horizontal, bool useNaturalSpline);

  void findTableDirection(bool horizontal);

  void extrapFillTableDirection(bool horizontal); 

  void extrapolateEmptyIndices(int outerIndex, const std::vector<int>& emptyIndices, const float* x, const float* y, size_t n, bool horizontal, bool naturalSpline);

  void extrapolateDiagonalEntries(const std::vector<std::pair<int, int>>& emptyIndices, const float* x, const float* y, size_t n);

  float calculatePosition(float watts, float cad, float targetPos, int i, int k);

  float linearInterpolate(const float* x, const float* y, size_t n, float j);

  float linearExtrapolate(const float* x, const float* y, size_t n, float j);

  void processNeighbor(int k, int i, float targetPosition, int neighbor_i, int neighbor_j, int neighbor_targetPosition,
    int oppositeNeighbor_i, int oppositeNeighbor_j, int oppositeNeighbor_targetPosition, float rangeFactor);  

 private:
  unsigned long lastSaveTime     = millis();
  TestResults testNeighbors(int i, int j, int value);
  void fillTable();
  void extrapFillTable();
  void extrapolateDiagonal();
  int getNumEntries();
  // remove entries with < 1 readings
  void clean();
};

class CubicSpline {
  public:
      void set_points(const float* x_vals, const float* y_vals, size_t n, bool natural);
  
      float interpolate(float x_val) const;
  
      float extrapolate(float x_val) const;
      
      bool shouldUseNaturalSpline(const float* x, const float* y, size_t n);
  
  private:
      std::vector<float> x, y, h, alpha, l, mu, z, c, b, d;
  };

extern PowerTable* powerTable;