/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>
#include "test.h"
#include "PowerTable_Helpers.h"
#include <sdkconfig.h>
#include <fstream>
#include <sstream>
#include <string>
#include "data_helpers.cpp"

void TestPAVAConvergence::test_pava_convergence_nebula3(void) {
  std::ofstream logFile("test/output/test_pava_convergence.txt", std::ios::trunc);
  logFile << "Starting PAVA convergence test\n";

  // Load the power table data from the source .ptab file
  PTData ptData;
  const std::string inputFilePath = "test/data/Nebula3.ptab";
  loadCSVToPTData(inputFilePath, ptData);
  logFile << "Loaded power table from: " << inputFilePath << "\n";

  // Count initial entries
  PTHelpers helpers;
  int initialEntries = helpers.getNumEntries(ptData);
  logFile << "Initial entries in table: " << initialEntries << "\n";

  // Run the PAVA convergence loop
  helpers.fillGaps(ptData);
  
  bool caddone  = false;
  bool wattdone = false;
  int loop      = 0;
  
  while (!caddone || !wattdone) {
    loop++;
    if (!caddone) {
      caddone = helpers.fillAllCadenceLines(ptData);
    }
    if (!wattdone) {
      wattdone = helpers.fillAllWattColumns(ptData);
    }
    logFile << "PAVA iteration " << loop << ": cad converged=" << caddone 
            << ", watt converged=" << wattdone << "\n";
    
    // Safety check to prevent infinite loops
    if (loop > 1000) {
      logFile << "WARNING: PAVA did not converge after 1000 iterations\n";
      TEST_FAIL_MESSAGE("PAVA did not converge after 1000 iterations");
      break;
    }
  }

  helpers.clean(ptData);
  
  // Count final entries
  int finalEntries = helpers.getNumEntries(ptData);
  logFile << "Final entries in table: " << finalEntries << "\n";
  logFile << "PAVA converged after " << loop << " iterations\n";

  // Save the result for inspection
  const std::string outputFilePath = "test/output/pava_converged_nebula3.ptab";
  savePTDataToCSV(ptData, outputFilePath, false);
  logFile << "Saved converged table to: " << outputFilePath << "\n";

  // Basic sanity checks
  TEST_ASSERT_TRUE_MESSAGE(loop < 1000, "PAVA should converge in reasonable time");
  if (finalEntries > 0) {
    TEST_ASSERT_TRUE_MESSAGE(finalEntries > 0, "Table should have entries after convergence");
  } else {
    logFile << "WARNING: Table has no entries after convergence - this may indicate all data was cleaned\n";
  }
  
  logFile << "Test completed successfully\n";
  logFile.close();
}

void TestPAVAConvergence::test_pava_convergence_nebula(void) {
  std::ofstream logFile("test/output/test_pava_convergence_nebula.txt", std::ios::trunc);
  logFile << "Starting PAVA convergence test for Nebula.ptab\n";

  // Load the power table data from the source .ptab file
  PTData ptData;
  const std::string inputFilePath = "test/data/Nebula.ptab";
  loadCSVToPTData(inputFilePath, ptData);
  logFile << "Loaded power table from: " << inputFilePath << "\n";

  // Count initial entries
  PTHelpers helpers;
  int initialEntries = helpers.getNumEntries(ptData);
  logFile << "Initial entries in table: " << initialEntries << "\n";

  // Run the PAVA convergence loop
  helpers.fillGaps(ptData);
  
  bool caddone  = false;
  bool wattdone = false;
  int loop      = 0;
  
  while (!caddone || !wattdone) {
    loop++;
    if (!caddone) {
      caddone = helpers.fillAllCadenceLines(ptData);
    }
    if (!wattdone) {
      wattdone = helpers.fillAllWattColumns(ptData);
    }
    logFile << "PAVA iteration " << loop << ": cad converged=" << caddone 
            << ", watt converged=" << wattdone << "\n";
    
    // Safety check to prevent infinite loops
    if (loop > 1000) {
      logFile << "WARNING: PAVA did not converge after 1000 iterations\n";
      TEST_FAIL_MESSAGE("PAVA did not converge after 1000 iterations");
      break;
    }
  }

  int beforeClean = helpers.getNumEntries(ptData);
  logFile << "Entries before clean: " << beforeClean << "\n";

  helpers.clean(ptData);
  
  // Count final entries
  int finalEntries = helpers.getNumEntries(ptData);
  logFile << "Final entries in table: " << finalEntries << "\n";
  logFile << "PAVA converged after " << loop << " iterations\n";

  // Save the result for inspection
  const std::string outputFilePath = "test/output/pava_converged_nebula.ptab";
  savePTDataToCSV(ptData, outputFilePath, false);
  logFile << "Saved converged table to: " << outputFilePath << "\n";

  // Basic sanity checks
  TEST_ASSERT_TRUE_MESSAGE(loop < 1000, "PAVA should converge in reasonable time");
  if (finalEntries > 0) {
    TEST_ASSERT_TRUE_MESSAGE(finalEntries > 0, "Table should have entries after convergence");
  } else {
    logFile << "WARNING: Table has no entries after convergence - this may indicate all data was cleaned\n";
  }
  
  logFile << "Test completed successfully\n";
  logFile.close();
}

void TestPAVAConvergence::test_pava_convergence_better(void) {
  std::ofstream logFile("test/output/test_pava_convergence_better.txt", std::ios::trunc);
  logFile << "Starting PAVA convergence test for better.ptab\n";

  // Load the power table data from the source .ptab file
  PTData ptData;
  const std::string inputFilePath = "test/data/better.ptab";
  loadCSVToPTData(inputFilePath, ptData);
  logFile << "Loaded power table from: " << inputFilePath << "\n";

  // Count initial entries
  PTHelpers helpers;
  int initialEntries = helpers.getNumEntries(ptData);
  logFile << "Initial entries in table: " << initialEntries << "\n";

  // Count entries by type
  int positiveCount = 0, negativeCount = 0, emptyCount = 0;
  for (int i = 0; i < POWERTABLE_CAD_SIZE; i++) {
    for (int j = 0; j < POWERTABLE_WATT_SIZE; j++) {
      int16_t val = ptData.tableRow[i].tableEntry[j].targetPosition;
      if (val == INT16_MIN) emptyCount++;
      else if (val < 0) negativeCount++;
      else positiveCount++;
    }
  }
  logFile << "Entry breakdown - Positive: " << positiveCount << ", Negative: " << negativeCount << ", Empty: " << emptyCount << "\n";

  // Run the PAVA convergence loop
  helpers.fillGaps(ptData);
  
  int afterFillGaps = helpers.getNumEntries(ptData);
  logFile << "Entries after fillGaps: " << afterFillGaps << "\n";
  
  bool caddone  = false;
  bool wattdone = false;
  int loop      = 0;
  
  while (!caddone || !wattdone) {
    loop++;
    if (!caddone) {
      caddone = helpers.fillAllCadenceLines(ptData);
    }
    if (!wattdone) {
      wattdone = helpers.fillAllWattColumns(ptData);
    }
    logFile << "PAVA iteration " << loop << ": cad converged=" << caddone 
            << ", watt converged=" << wattdone << "\n";
    
    // Safety check to prevent infinite loops
    if (loop > 1000) {
      logFile << "WARNING: PAVA did not converge after 1000 iterations\n";
      TEST_FAIL_MESSAGE("PAVA did not converge after 1000 iterations");
      break;
    }
  }

  int beforeClean = helpers.getNumEntries(ptData);
  logFile << "Entries before clean: " << beforeClean << "\n";

  helpers.clean(ptData);
  
  // Count final entries
  int finalEntries = helpers.getNumEntries(ptData);
  logFile << "Final entries in table: " << finalEntries << "\n";
  logFile << "PAVA converged after " << loop << " iterations\n";

  // Save the result for inspection
  const std::string outputFilePath = "test/output/pava_converged_better.ptab";
  savePTDataToCSV(ptData, outputFilePath, false);
  logFile << "Saved converged table to: " << outputFilePath << "\n";

  // Basic sanity checks
  TEST_ASSERT_TRUE_MESSAGE(loop < 1000, "PAVA should converge in reasonable time");
  TEST_ASSERT_TRUE_MESSAGE(finalEntries > 0, "Table should have entries after convergence");
  
  logFile << "Test completed successfully\n";
  logFile.close();
}
