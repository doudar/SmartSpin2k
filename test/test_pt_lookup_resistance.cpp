/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>

#include <cstdio>

#include "PowerTable_Helpers.h"
#include "../src/PowerTable_Helpers.cpp"
#include "test.h"
#include "test_data_helpers.h"

void TestPTLookupResistance::test_cadence_collection_boundaries(void) {
  PTHelpers helpers;

  TEST_ASSERT_FALSE_MESSAGE(helpers.cadenceIsWithinTable(57), "57 RPM must remain below the 60 RPM collection bin");
  TEST_ASSERT_TRUE_MESSAGE(helpers.cadenceIsWithinTable(58), "58 RPM must be collected into the 60 RPM bin");
  TEST_ASSERT_EQUAL_INT8_MESSAGE(0, helpers.calculateIndex(0, 58).cadIndex, "58 RPM must map to the 60 RPM row");
  TEST_ASSERT_TRUE_MESSAGE(helpers.cadenceIsWithinTable(62), "62 RPM must be collected into the 60 RPM bin");
  TEST_ASSERT_EQUAL_INT8_MESSAGE(0, helpers.calculateIndex(0, 62).cadIndex, "62 RPM must map to the 60 RPM row");
  TEST_ASSERT_EQUAL_INT8_MESSAGE(1, helpers.calculateIndex(0, 63).cadIndex, "63 RPM must map to the 65 RPM row");

  TEST_ASSERT_TRUE_MESSAGE(helpers.cadenceIsWithinTable(103), "103 RPM must be collected into the 105 RPM bin");
  TEST_ASSERT_EQUAL_INT8_MESSAGE(9, helpers.calculateIndex(0, 103).cadIndex, "103 RPM must map to the 105 RPM row");
  TEST_ASSERT_TRUE_MESSAGE(helpers.cadenceIsWithinTable(107), "107 RPM must be collected into the 105 RPM bin");
  TEST_ASSERT_EQUAL_INT8_MESSAGE(9, helpers.calculateIndex(0, 107).cadIndex, "107 RPM must map to the 105 RPM row");
  TEST_ASSERT_FALSE_MESSAGE(helpers.cadenceIsWithinTable(108), "108 RPM must remain above the 105 RPM collection bin");
}

void TestPTLookupResistance::test_active_ride_forward_lookup(void) {
  PTData ptData;
  RideReplaySummary summary;
  TEST_ASSERT_TRUE_MESSAGE(replayActiveRideLog(ptData, summary), "active ride log could not be opened for forward lookup test");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, summary.invalidEntries, "active ride log contains invalid table entries");
  TEST_ASSERT_EQUAL_INT_MESSAGE(594, summary.entries, "forward lookup did not replay every active-log entry");

  PTHelpers helpers;
  TEST_ASSERT_EQUAL_INT32_MESSAGE(RETURN_ERROR, helpers.lookup(200, 0, ptData), "zero cadence must be rejected by forward lookup");
  TEST_ASSERT_EQUAL_INT32_MESSAGE(RETURN_ERROR, helpers.lookup(-1, 75, ptData), "negative watts must be rejected by forward lookup");
  TEST_ASSERT_NOT_EQUAL_MESSAGE(RETURN_ERROR, helpers.lookup(INT32_MAX, 1, ptData), "extreme forward lookup overflowed into RETURN_ERROR");

  char failure[220];
  for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
    int16_t previousPosition = INT16_MIN;
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      const TableEntry& entry = ptData.tableRow[row].tableEntry[col];
      if (entry.targetPosition == INT16_MIN) continue;
      std::snprintf(failure, sizeof(failure),
                    "stored table decreased with watts: cadence=%d watts=%d previous_position=%d position=%d",
                    MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT, col * POWERTABLE_WATT_INCREMENT,
                    previousPosition, entry.targetPosition);
      TEST_ASSERT_GREATER_OR_EQUAL_INT16_MESSAGE(previousPosition, entry.targetPosition, failure);
      previousPosition = entry.targetPosition;
    }
  }
  for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
    int16_t previousPosition = INT16_MAX;
    for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
      const TableEntry& entry = ptData.tableRow[row].tableEntry[col];
      if (entry.targetPosition == INT16_MIN) continue;
      std::snprintf(failure, sizeof(failure),
                    "stored table increased with cadence: watts=%d cadence=%d previous_position=%d position=%d",
                    col * POWERTABLE_WATT_INCREMENT, MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT,
                    previousPosition, entry.targetPosition);
      TEST_ASSERT_LESS_OR_EQUAL_INT16_MESSAGE(previousPosition, entry.targetPosition, failure);
      previousPosition = entry.targetPosition;
    }
  }

  for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
    int reliableSamples = 0;
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      const TableEntry& sample = ptData.tableRow[row].tableEntry[col];
      if (sample.targetPosition != INT16_MIN && sample.readings >= 2) ++reliableSamples;
    }
    if (reliableSamples < 2) continue;

    const int cadence = MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT;
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      const TableEntry& sample = ptData.tableRow[row].tableEntry[col];
      if (sample.targetPosition == INT16_MIN || sample.readings < 2) continue;
      const int watts = col * POWERTABLE_WATT_INCREMENT;
      const int32_t actual = helpers.lookup(watts, cadence, ptData);
      const int32_t expected = static_cast<int32_t>(sample.targetPosition * TABLE_DIVISOR);
      std::snprintf(failure, sizeof(failure), "measured forward sample mismatch: cadence=%d watts=%d expected_position=%ld actual_position=%ld",
                    cadence, watts, static_cast<long>(expected), static_cast<long>(actual));
      TEST_ASSERT_EQUAL_INT32_MESSAGE(expected, actual, failure);
    }
  }

  for (int cadence = 40; cadence <= 130; ++cadence) {
    int32_t previousPosition = INT32_MIN;
    for (int watts = 0; watts <= 1200; watts += 15) {
      const int32_t position = helpers.lookup(watts, cadence, ptData);
      std::snprintf(failure, sizeof(failure), "invalid forward lookup: cadence=%d watts=%d returned=%ld", cadence, watts, static_cast<long>(position));
      TEST_ASSERT_NOT_EQUAL_MESSAGE(RETURN_ERROR, position, failure);
      if (previousPosition != INT32_MIN) {
        std::snprintf(failure, sizeof(failure),
                      "forward lookup decreased with watts: cadence=%d previous_watts=%d previous_position=%ld watts=%d position=%ld",
                      cadence, watts - 15, static_cast<long>(previousPosition), watts, static_cast<long>(position));
        TEST_ASSERT_GREATER_OR_EQUAL_INT32_MESSAGE(previousPosition, position, failure);
      }
      previousPosition = position;
    }
  }

  // Monotonic enforcement must correct measured violations without filling
  // empty cells that were never observed in the active ride.
  PTData sparseTable;
  sparseTable.tableRow[2].tableEntry[1].targetPosition = 200;
  sparseTable.tableRow[2].tableEntry[1].readings = 2;
  sparseTable.tableRow[2].tableEntry[3].targetPosition = 100;
  sparseTable.tableRow[2].tableEntry[3].readings = 2;
  ptIndex newSample;
  newSample.cadIndex = 2;
  newSample.wattIndex = 5;
  helpers.enterData(sparseTable, newSample, 300);
  TEST_ASSERT_EQUAL_INT16_MESSAGE(150, sparseTable.tableRow[2].tableEntry[1].targetPosition, "power monotonic correction failed at lower sample");
  TEST_ASSERT_EQUAL_INT16_MESSAGE(INT16_MIN, sparseTable.tableRow[2].tableEntry[2].targetPosition, "power monotonic correction filled an unmeasured cell");
  TEST_ASSERT_EQUAL_INT16_MESSAGE(150, sparseTable.tableRow[2].tableEntry[3].targetPosition, "power monotonic correction failed at upper sample");
}

void TestPTLookupResistance::test_flat_row_extrapolation_uses_informative_rows(void) {
  PTHelpers helpers;
  PTData informative;
  for (int cadence : {90, 100}) {
    for (int watts : {90, 180, 270}) {
      auto& entry = informative.tableRow[(cadence - MINIMUM_TABLE_CAD) / POWERTABLE_CAD_INCREMENT].tableEntry[watts / POWERTABLE_WATT_INCREMENT];
      entry.targetPosition = (10000 + (watts - 90) * 100 / 9 - (cadence - 90) * 20) / TABLE_DIVISOR;
      entry.readings = 2;
    }
  }
  PTData combined = informative;
  // Captured shape from the 105 RPM / 700 W empty-table sprint: two reliable
  // 660/690 W cells share 18060 steps. They convey no extrapolation slope.
  for (int watts : {660, 690}) {
    auto& entry = combined.tableRow[9].tableEntry[watts / POWERTABLE_WATT_INCREMENT];
    entry.targetPosition = 18060 / TABLE_DIVISOR;
    entry.readings = 2;
  }
  TEST_ASSERT_EQUAL_INT32(9858, helpers.lookup(100, 105, informative));
  // Borrow the donor's change in position, not its absolute position. The
  // synthetic donor is linear at 1000 steps per 90 W in its 100 RPM row.
  // Thus the 105 RPM extension has 1000/90 * 100/105 steps per watt and
  // meets the measured plateau from both sides without an intercept jump.
  TEST_ASSERT_INT32_WITHIN(1, 12134, helpers.lookup(100, 105, combined));
  TEST_ASSERT_INT32_WITHIN(1, 20282, helpers.lookup(900, 105, combined));
  TEST_ASSERT_EQUAL_INT32(helpers.lookup(100, 100, informative), helpers.lookup(100, 100, combined));
  TEST_ASSERT_EQUAL_INT32(helpers.lookup(900, 100, informative), helpers.lookup(900, 100, combined));
  for (int cadence : {102, 105, 110}) {
    TEST_ASSERT_LESS_THAN_INT32(18060, helpers.lookup(100, cadence, combined));
    TEST_ASSERT_GREATER_THAN_INT32(18060, helpers.lookup(900, cadence, combined));
  }
}

void TestPTLookupResistance::test_flat_row_preserves_measured_lookup_and_fallback(void) {
  PTHelpers helpers;
  PTData flat;
  for (int watts : {660, 690}) {
    auto& entry = flat.tableRow[9].tableEntry[watts / POWERTABLE_WATT_INCREMENT];
    entry.targetPosition = 18060 / TABLE_DIVISOR;
    entry.readings = 2;
  }
  for (int watts : {660, 675, 690}) {
    TEST_ASSERT_EQUAL_INT32_MESSAGE(18060, helpers.lookup(watts, 105, flat), "measured points and flat interpolation must remain exact");
  }
  // There is no alternative slope: preserve the existing all-flat fallback
  // and reverse lookup's measured midpoint and safe bounds.
  TEST_ASSERT_EQUAL_INT32(18060, helpers.lookup(100, 105, flat));
  TEST_ASSERT_EQUAL_INT32(18060, helpers.lookup(900, 105, flat));
  TEST_ASSERT_EQUAL_INT(675, helpers.lookupWatts(105, 18060, flat));
  TEST_ASSERT_EQUAL_INT(0, helpers.lookupWatts(105, 18050, flat));
  TEST_ASSERT_EQUAL_INT(690, helpers.lookupWatts(105, 18070, flat));

  PTData positive = flat;
  positive.tableRow[9].tableEntry[23].targetPosition = 18260 / TABLE_DIVISOR;
  TEST_ASSERT_EQUAL_INT32_MESSAGE(18160, helpers.lookup(675, 105, positive), "positive interpolation changed");
  TEST_ASSERT_EQUAL_INT32_MESSAGE(14327, helpers.lookup(100, 105, positive), "informative positive extrapolation changed");

  PTData plateau = flat;
  plateau.tableRow[9].tableEntry[21].targetPosition = 17460 / TABLE_DIVISOR;
  plateau.tableRow[9].tableEntry[21].readings = 2;
  TEST_ASSERT_EQUAL_INT32_MESSAGE(18360, helpers.lookup(720, 105, plateau), "a flat endpoint must retain the wider positive span");

  // Informative neighboring rows must not suppress an actual measured
  // plateau or valid interpolation at its own cadence.
  for (int watts : {90, 180, 270}) {
    auto& entry = flat.tableRow[8].tableEntry[watts / POWERTABLE_WATT_INCREMENT];
    entry.targetPosition = 980 + (watts - 90) * 10 / 9;
    entry.readings = 2;
  }
  for (int watts : {660, 675, 690}) TEST_ASSERT_EQUAL_INT32(18060, helpers.lookup(watts, 105, flat));
}

void TestPTLookupResistance::test_flat_row_edges_remain_monotone(void) {
  PTHelpers helpers;
  const auto point = [](PTData& table, int cadence, int watts, int position) {
    auto& entry = table.tableRow[(cadence - MINIMUM_TABLE_CAD) / POWERTABLE_CAD_INCREMENT].tableEntry[watts / POWERTABLE_WATT_INCREMENT];
    entry.targetPosition = position / TABLE_DIVISOR;
    entry.readings = 2;
  };
  for (bool lowerEdgeConflict : {false, true}) {
    PTData table;
    for (int cadence : {90, 100}) for (int watts : {90, 180, 270}) {
      point(table, cadence, watts, (lowerEdgeConflict ? 18000 : 10000) + (watts - 90) * 100 / 9 - (cadence - 90) * 20);
    }
    for (int watts : {660, 690}) point(table, 105, watts, 18060);
    TEST_ASSERT_EQUAL_INT32(18060, helpers.lookup(660, 105, table));
    TEST_ASSERT_EQUAL_INT32(18060, helpers.lookup(690, 105, table));
    TEST_ASSERT_LESS_OR_EQUAL_INT32_MESSAGE(18060, helpers.lookup(659, 105, table), "lower edge may not jump down into the measured plateau");
    TEST_ASSERT_GREATER_OR_EQUAL_INT32_MESSAGE(18060, helpers.lookup(691, 105, table), "690 to 691 W may not drop below its measured endpoint");
    TEST_ASSERT_INT32_WITHIN_MESSAGE(12, 18060, helpers.lookup(659, 105, table), "lower edge must meet the measured plateau continuously");
    TEST_ASSERT_INT32_WITHIN_MESSAGE(12, 18060, helpers.lookup(691, 105, table), "upper edge must leave the measured plateau continuously");
    // Additional flat rows straddle different watt ranges. They must not
    // become replacement donors only when their own request enters a plateau.
    for (int watts : {300, 330}) point(table, 95, watts, 13000);
    for (int watts : {90, 120}) point(table, 85, watts, 11000);
    char message[160];
    for (int cadence : {80, 85, 87, 90, 92, 95, 97, 100, 102, 105, 110}) {
      int32_t previous = INT32_MIN;
      for (int watts = 0; watts <= 1100; ++watts) {
        const int32_t actual = helpers.lookup(watts, cadence, table);
        std::snprintf(message, sizeof(message), "flat edge/mixed cadence decreased: lowerConflict=%d %d W/%d RPM %ld -> %ld",
                      lowerEdgeConflict, watts, cadence, static_cast<long>(previous), static_cast<long>(actual));
        TEST_ASSERT_GREATER_OR_EQUAL_INT32_MESSAGE(previous, actual, message);
        // Maximum fixture slope is 12.5 steps/W at 80 RPM; rounding permits
        // 13. A 14-step bound covers every one-watt edge and cadence mixture,
        // including the three staggered flat rows, without allowing a jump.
        if (previous != INT32_MIN) TEST_ASSERT_LESS_OR_EQUAL_INT32_MESSAGE(14, actual - previous, message);
        previous = actual;
      }
      int previousWatts = 0;
      for (int position = 8000; position <= 22000; position += 100) {
        const int watts = helpers.lookupWatts(cadence, position, table);
        TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(previousWatts, watts, "inverse lookup decreased across flat-row edges");
        previousWatts = watts;
      }
    }
    // lookupWatts also applies its legacy cross-cadence envelope; conflicting
    // donor rows can lift a plateau's inverse above its local midpoint. The
    // all-flat-only midpoint is asserted separately, while these mixtures
    // must keep the public inverse nondecreasing and within its existing cap.
    TEST_ASSERT_LESS_OR_EQUAL_INT(4000, helpers.lookupWatts(105, 22000, table));
  }
}

void TestPTLookupResistance::test_flat_row_malformed_donor_is_not_reused(void) {
  PTHelpers helpers;
  PTData table;
  for (int watts : {90, 180, 270}) {
    auto& entry = table.tableRow[8].tableEntry[watts / POWERTABLE_WATT_INCREMENT];
    entry.targetPosition = 980 + (watts - 90) * 10 / 9;
    entry.readings = 2;
  }
  // The endpoints coincide but the interior is lower: a malformed imported
  // row has a global span while neither extrapolation end provides a slope.
  // Choosing it as both donor bounds would divide by zero in cadence blend.
  for (int watts : {630, 660, 690}) {
    auto& entry = table.tableRow[9].tableEntry[watts / POWERTABLE_WATT_INCREMENT];
    entry.targetPosition = watts == 660 ? 1805 : 1806;
    entry.readings = 2;
  }
  TEST_ASSERT_INT32_WITHIN(1, 12452, helpers.lookup(100, 105, table));
  TEST_ASSERT_INT32_WITHIN(1, 20282, helpers.lookup(900, 105, table));
  TEST_ASSERT_EQUAL_INT32(18050, helpers.lookup(660, 105, table));
  TEST_ASSERT_EQUAL_INT32(18055, helpers.lookup(675, 105, table));
  table.tableRow[8] = TableRow();
  TEST_ASSERT_EQUAL_INT32_MESSAGE(18060, helpers.lookup(100, 105, table), "no-donor fallback must remain finite below malformed endpoints");
  TEST_ASSERT_EQUAL_INT32_MESSAGE(18060, helpers.lookup(900, 105, table), "no-donor fallback must remain finite above malformed endpoints");
}

void TestPTLookupResistance::test_erg_slope_quality(void) {
  PTHelpers helpers;
  PTData slopeTable;
  for (int row = 0; row < 2; ++row) {
    for (int col = 0; col <= 4; ++col) {
      slopeTable.tableRow[row].tableEntry[col].targetPosition = 100 + col * 100 - row * 10;
      slopeTable.tableRow[row].tableEntry[col].readings = 2;
    }
  }

  double stepsPerWatt = 0.0;
  TEST_ASSERT_TRUE_MESSAGE(helpers.lookupSlope(60, 60, stepsPerWatt, slopeTable), "consistent interior cadence rows should provide an ERG slope");
  TEST_ASSERT_GREATER_THAN_FLOAT_MESSAGE(0.0f, static_cast<float>(stepsPerWatt), "accepted ERG slope must be positive and finite");
  TEST_ASSERT_FALSE_MESSAGE(helpers.lookupSlope(120, 60, stepsPerWatt, slopeTable), "right-edge extrapolation must not provide an ERG slope");
  TEST_ASSERT_FALSE_MESSAGE(helpers.lookupErgSlope(4, 60, stepsPerWatt, slopeTable), "a near-edge segment estimate must not provide an ERG slope");

  slopeTable.tableRow[1].tableEntry[3].targetPosition = 700;
  slopeTable.tableRow[1].tableEntry[4].targetPosition = 1000;
  TEST_ASSERT_FALSE_MESSAGE(helpers.lookupSlope(60, 60, stepsPerWatt, slopeTable), "nonparallel cadence rows must not provide an ERG slope");

  PTData activeTable;
  RideReplaySummary summary;
  TEST_ASSERT_TRUE_MESSAGE(replayActiveRideLog(activeTable, summary), "active ride log could not be opened for slope-quality test");
  TEST_ASSERT_FALSE_MESSAGE(helpers.lookupSlope(385, 93, stepsPerWatt, activeTable), "385 W at 93 RPM must reject extrapolated active-log edge data");
  TEST_ASSERT_FALSE_MESSAGE(helpers.lookupSlope(385, 94, stepsPerWatt, activeTable), "385 W at 94 RPM must reject extrapolated active-log edge data");
  // The shorter learning history exposes unequal edge slopes in this replay
  // (14 versus 29 stored units over 30W); do not relax the 2x quality guard.
  PowerTableSlopeStatus::Value status;
  TEST_ASSERT_FALSE(helpers.lookupErgSlope(340, 101, stepsPerWatt, activeTable, &status));
  TEST_ASSERT_EQUAL_INT(PowerTableSlopeStatus::InconsistentRows, status);

  PTData measuredEdge;
  for (int row = 8; row <= 9; ++row) {
    for (int col = 11; col <= 12; ++col) {
      measuredEdge.tableRow[row].tableEntry[col].targetPosition = 1400 - (row - 8) * 100 + (col - 11) * 30;
      measuredEdge.tableRow[row].tableEntry[col].readings       = 4;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(helpers.lookupErgSlope(340, 101, stepsPerWatt, measuredEdge), "340 W at 101 RPM should use consistent measured 330--360 W segments");
}

namespace {
void nearFlatPoint(PTData& table,int cadence,int watts,int storedPosition) {
  auto& e=table.tableRow[(cadence-60)/5].tableEntry[watts/30];
  e.targetPosition=storedPosition;e.readings=3;
}
PTData nearFlatFixture(int span,bool donors=true) {
  PTData table;
  if(donors)for(int cadence:{90,100})for(int watts:{90,180,270})
    nearFlatPoint(table,cadence,watts,1360+(watts-90)-2*(cadence-90));
  nearFlatPoint(table,105,450,1612);nearFlatPoint(table,105,480,1612+span);
  return table;
}
}

void TestPTLookupResistance::test_near_flat_repair_uses_measured_edge(void) {
  PTHelpers h;
  for(int span:{0,1,19,20,21}) {
    PTData table=nearFlatFixture(span);
    TEST_ASSERT_EQUAL_INT32(16120,h.lookup(450,105,table));
    TEST_ASSERT_EQUAL_INT32(16120+5*span,h.lookup(465,105,table));
    TEST_ASSERT_EQUAL_INT32(16120+10*span,h.lookup(480,105,table));
    if(span<20) {
      // The 100 RPM donor changes 10 steps/W, mapped at equal torque to
      // 105 RPM: 200/21 steps/W. Its intercept must not replace this row's
      // measured edge, nor may the discarded weak slope be added again.
      TEST_ASSERT_INT32_WITHIN(1,12787,h.lookup(100,105,table));
      TEST_ASSERT_INT32_WITHIN(1,20120+10*span,h.lookup(900,105,table));
      TEST_ASSERT_INT32_WITHIN(1,16110,h.lookup(449,105,table));
      TEST_ASSERT_INT32_WITHIN(1,16130+10*span,h.lookup(481,105,table));
    } else {
      // At the existing 20-unit threshold, the row supplies its own slope.
      TEST_ASSERT_INT32_WITHIN(1,std::lround(16120-350.0*span/3),h.lookup(100,105,table));
      TEST_ASSERT_INT32_WITHIN(1,16120+150*span,h.lookup(900,105,table));
    }
  }
}

void TestPTLookupResistance::test_near_flat_donor_threshold_and_fallback(void) {
  PTHelpers h;
  for(int donorSpan:{19,20}) {
    PTData table=nearFlatFixture(1,false);
    nearFlatPoint(table,100,90,1340);nearFlatPoint(table,100,180,1340+donorSpan);
    TEST_ASSERT_EQUAL_INT32(donorSpan==19?16003:15379,h.lookup(100,105,table));
    // Crossing the donor's measured edge cannot toggle its eligibility.
    int previous=h.lookup(185,105,table);
    for(int watts=186;watts<=194;++watts) {
      const int current=h.lookup(watts,105,table);
      TEST_ASSERT_GREATER_OR_EQUAL_INT32(previous,current);
      TEST_ASSERT_LESS_OR_EQUAL_INT32(3,current-previous);previous=current;
    }
  }
  for(int span:{0,1,19,20}) {
    PTData table=nearFlatFixture(span,false);
    for(int cadence:{90,102,105,110,130})for(int watts=0;watts<=1000;watts+=7) {
      const double rowWatts=watts*105.0/cadence;
      const int expected=std::lround(16120+(rowWatts-450)*span/3.0);
      TEST_ASSERT_INT32_WITHIN(1,expected,h.lookup(watts,cadence,table));
    }
    if(span>0)for(int watts:{100,449,450,465,480,481,900})
      TEST_ASSERT_INT32_WITHIN(2,watts,h.lookupWatts(105,h.lookup(watts,105,table),table));
  }
  // Two weak rows must not donate merely while their query is in range.
  PTData weak=nearFlatFixture(1,false);
  nearFlatPoint(weak,100,90,1340);nearFlatPoint(weak,100,270,1359);
  TEST_ASSERT_EQUAL_INT32(16003,h.lookup(100,105,weak));
}

void TestPTLookupResistance::test_near_flat_continuity_and_inverse(void) {
  PTHelpers h;
  for(int span:{1,19,20}) {
    PTData table=nearFlatFixture(span);
    for(int cadence:{85,90,95,100,101,102,103,104,105,108,110}) {
      int previous=h.lookup(0,cadence,table);
      const int bound=static_cast<int>(std::ceil(1000.0/cadence))+1;
      for(int watts=1;watts<=1000;++watts) {
        const int current=h.lookup(watts,cadence,table);
        TEST_ASSERT_GREATER_OR_EQUAL_INT32(previous,current);
        TEST_ASSERT_LESS_OR_EQUAL_INT32(bound,current-previous);
        previous=current;
      }
      int previousWatts=0;
      for(int position=11000;position<=23000;position+=10) {
        const int watts=h.lookupWatts(cadence,position,table);
        TEST_ASSERT_GREATER_OR_EQUAL_INT32(previousWatts,watts);
        TEST_ASSERT_LESS_OR_EQUAL_INT32(4000,watts);previousWatts=watts;
      }
    }
    for(int watts:{100,449,450,465,480,481,900}) {
      // Public reverse lookup retains its cumulative cadence envelope. The
      // unchanged 20-unit row predicts 100 W at 13787 steps, but the lower
      // cadence donor implies 129 W there. Preserve that legacy envelope;
      // the repaired weak rows are coherent and round-trip to within 2 W.
      const int expected=(span==20 && watts==100)?129:watts;
      TEST_ASSERT_INT32_WITHIN(2,expected,h.lookupWatts(105,h.lookup(watts,105,table),table));
    }
    // A second under-resolved row has its own measured edges; it cannot
    // become a donor at those boundaries and introduce a moving donor set.
    nearFlatPoint(table,95,630,1780);nearFlatPoint(table,95,660,1799);
    for(int cadence:{92,95,97,102,105}) {
      int previous=h.lookup(0,cadence,table);
      const int bound=static_cast<int>(std::ceil(1000.0/cadence))+1;
      for(int watts=1;watts<=1000;++watts) {
        const int current=h.lookup(watts,cadence,table);
        TEST_ASSERT_GREATER_OR_EQUAL_INT32(previous,current);
        TEST_ASSERT_LESS_OR_EQUAL_INT32(bound,current-previous);previous=current;
      }
    }
  }
}

void TestPTLookupResistance::test_near_flat_rejects_malformed_donors(void) {
  PTHelpers h;
  PTData table=nearFlatFixture(1,false);
  // Large overall span is insufficient when the candidate row is malformed.
  nearFlatPoint(table,100,90,1000);nearFlatPoint(table,100,180,1040);nearFlatPoint(table,100,270,1010);
  TEST_ASSERT_EQUAL_INT32(16003,h.lookup(100,105,table));
  for(int watts:{90,180,270})nearFlatPoint(table,90,watts,1360+(watts-90));
  TEST_ASSERT_EQUAL_INT32(13120,h.lookup(100,105,table));
  // A monotone donor with a weak final segment must select its wider span
  // outside the edge, rather than becoming a weak extrapolation donor.
  PTData wider=nearFlatFixture(1,false);
  nearFlatPoint(wider,100,90,1000);nearFlatPoint(wider,100,180,1020);nearFlatPoint(wider,100,270,1021);
  int previous=h.lookup(0,105,wider);
  for(int watts=1;watts<=900;++watts) {
    const int current=h.lookup(watts,105,wider);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(previous,current);
    TEST_ASSERT_LESS_OR_EQUAL_INT32(4,current-previous);previous=current;
  }
  // This recipient has a 40-unit span elsewhere but a weak selected upper
  // endpoint pair. Its nonmonotonicity excludes self-donation and 0/0.
  PTData malformed;
  for(int watts:{90,180,270})nearFlatPoint(malformed,100,watts,1340+(watts-90));
  nearFlatPoint(malformed,105,90,1700);nearFlatPoint(malformed,105,180,1740);
  nearFlatPoint(malformed,105,270,1701);nearFlatPoint(malformed,105,360,1701);
  TEST_ASSERT_EQUAL_INT32(17010,h.lookup(360,105,malformed));
  TEST_ASSERT_EQUAL_INT32(17296,h.lookup(390,105,malformed));
  malformed.tableRow[8]=TableRow();
  TEST_ASSERT_EQUAL_INT32(17011,h.lookup(390,105,malformed));
}
