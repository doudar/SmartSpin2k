
/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>
#include "test.h"
#include "PowerTable_Helpers.h"
// Doesn't need to be included again, since is't already in test_pt_lookup_resistance.cpp
//#include "../src/PowerTable_Helpers.cpp"
#include <sdkconfig.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "data_helpers.cpp"

void TestPTLookupWatts::test_pt_lookup_watts(void) {
  printf("Starting lookup test\n");

  // Create a test power table with simple values
  PTData ptData;

  // Load the power table data from the .ptab file
  const std::string filePath = "test/data/power_table.ptab";
  loadCSVToPTData(filePath, ptData);

  // Create helpers object for lookup
  PTHelpers helpers;

  // Lambda function for reusable lookup and logging
  auto performLookup = [&](int cad, int resistance) {
    printf("Calling lookup CAD %d, with Resistance: %d", cad, resistance);
    int32_t result = helpers.lookupWatts(cad, resistance, ptData);
    printf("Lookup returned: %dw\n", result);
    return result;
  };

  // Test cadence from 10-130 using resistance range from -DEFAULT_STEPPER_TRAVEL to +DEFAULT_STEPPER_TRAVEL. For each cadence, as resistance increases, check that the output watt values are increasing. When a new
  // cadence is reached, the watt values should be higher than the previous cadence or the test will fail.
  
  // Define test cadence range
  int minCadence = 30;
  int maxCadence = 130;
  int cadenceStep = 10;
  
  #define MIN_TEST_RANGE 0
  #define MAX_TEST_RANGE 30000
  #define POINTS_PER_CADENCE 10
  // Define resistance test points (using a smaller range for testing efficiency)
  int resistancePoints = POINTS_PER_CADENCE;
  int resistanceStep = MAX_TEST_RANGE / POINTS_PER_CADENCE;
  
  int32_t previousMaxWatts = INT32_MIN;  // Track max watts from previous cadence
  
  printf("Testing cadence range %d-%d with %d resistance points\n", minCadence, maxCadence, resistancePoints);
  
  // Iterate through each cadence value
  for (int cadence = minCadence; cadence <= maxCadence; cadence += cadenceStep) {
    printf("\n--- Testing cadence %d ---\n", cadence);
    
    int32_t previousWatts = INT32_MIN;  // Track previous watts within this cadence
    int32_t maxWattsForCadence = INT32_MIN;  // Track max watts for this cadence
    
    // Test resistance points from negative to positive values
    for (int32_t resistance = 0;
         resistance <= MAX_TEST_RANGE;
         resistance += resistanceStep) {
      
      int32_t watts = performLookup(cadence, resistance);
      
      // Check that watts increase as resistance increases for the same cadence
      if (previousWatts != INT32_MIN) {
        TEST_ASSERT_TRUE_MESSAGE(
          watts >= previousWatts,
          "Watts should increase or stay the same as resistance increases"
        );
      }
      
      // Update max watts for this cadence
      if (watts > maxWattsForCadence) {
        maxWattsForCadence = watts;
      }
      
      previousWatts = watts;
    }
    
    // Check that max watts increase as cadence increases
    if (previousMaxWatts != INT32_MIN && cadence > minCadence) {
      TEST_ASSERT_TRUE_MESSAGE(
        maxWattsForCadence > previousMaxWatts,
        "Max watts should increase with higher cadence"
      );
      
      printf("Max watts increased from %d to %d when cadence changed from %d to %d\n",
             previousMaxWatts, maxWattsForCadence, cadence - cadenceStep, cadence);
    }
    
    previousMaxWatts = maxWattsForCadence;
  }

  // Print additional debug info
  printf("Test completed\n");
}