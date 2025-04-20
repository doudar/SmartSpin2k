/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "SmartSpin_parameters.h"
#include <vector>

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

  class PTHelpers {
   public:
   TestResults testNeighbors(int i, int j, int value);
  };