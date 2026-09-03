/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>

#include <cmath>
#include <fstream>
#include <limits>
#include <regex>
#include <string>
#include <vector>

#include "ERG_Mode_Utils.h"
#include "PowerTable_Helpers.h"
#include "test.h"
#include "test_data_helpers.h"

namespace {

struct ErgSample {
  int timestamp;
  int watts;
  int target;
  double gain;
};

struct ErgInterval {
  int target;
  int start;
  int end;
  double sensitivity;
  std::vector<ErgSample> samples;
};

int errorSign(int error) { return (error > 0) - (error < 0); }

int signChanges(const ErgInterval& interval) {
  int changes      = 0;
  int previousSign = 0;
  for (const ErgSample& sample : interval.samples) {
    const int sign = errorSign(sample.target - sample.watts);
    if (sign != 0 && previousSign != 0 && sign != previousSign) changes++;
    if (sign != 0) previousSign = sign;
  }
  return changes;
}

int maxAbsoluteError(const ErgInterval& interval) {
  int result = 0;
  for (const ErgSample& sample : interval.samples) result = std::max(result, std::abs(sample.target - sample.watts));
  return result;
}

double maxGain(const ErgInterval& interval) {
  double result = 0.0;
  for (const ErgSample& sample : interval.samples) result = std::max(result, sample.gain);
  return result;
}

}  // namespace

void TestErgLogReplay::test_active_ride_log_and_gain_limits(void) {
  std::ifstream input(ACTIVE_RIDE_LOG_PATH);
  TEST_ASSERT_TRUE_MESSAGE(input.is_open(), "active ride log must be available to the ERG replay test");

  const std::regex targetPattern("\\[([0-9]+)\\].*ERG Mode Target: ([0-9]+)");
  const std::regex samplePattern("\\[([0-9]+)\\].*ERG_Mode\\): ([0-9]+)w, Target ([0-9]+)w, Kp: ([0-9.]+) \\(table\\)");
  const std::regex sensitivityPattern("\\[([0-9]+)\\].*4B:801F([0-9A-Fa-f]{2})00");

  std::vector<ErgInterval> intervals;
  double sensitivity = 0.0;
  std::string line;
  std::smatch match;
  while (std::getline(input, line)) {
    if (std::regex_search(line, match, sensitivityPattern)) {
      sensitivity = static_cast<double>(std::stoi(match[2].str(), nullptr, 16)) / 10.0;
    }

    if (std::regex_search(line, match, targetPattern)) {
      const int timestamp = std::stoi(match[1].str());
      if (!intervals.empty()) intervals.back().end = timestamp;
      intervals.push_back({std::stoi(match[2].str()), timestamp, timestamp, sensitivity, {}});
      continue;
    }

    if (!intervals.empty() && std::regex_search(line, match, samplePattern)) {
      const int timestamp = std::stoi(match[1].str());
      intervals.back().samples.push_back({timestamp, std::stoi(match[2].str()), std::stoi(match[3].str()), std::stod(match[4].str())});
      intervals.back().end = timestamp;
    }
  }

  std::vector<const ErgInterval*> repeatedTargetIntervals;
  for (const ErgInterval& interval : intervals) {
    if (interval.target == 155 && interval.samples.size() >= 5) repeatedTargetIntervals.push_back(&interval);
  }

  TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(2, repeatedTargetIntervals.size(), "ride log should contain the early and late table-controlled 155 W intervals");
  const ErgInterval& unstable = *repeatedTargetIntervals.front();
  const ErgInterval& stable   = *repeatedTargetIntervals.back();

  TEST_ASSERT_EQUAL_INT(155, unstable.target);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 5.0, unstable.sensitivity);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(18, unstable.samples.size());
  TEST_ASSERT_GREATER_OR_EQUAL_INT(100, maxAbsoluteError(unstable));
  TEST_ASSERT_GREATER_OR_EQUAL_INT(3, signChanges(unstable));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.5f, static_cast<float>(maxGain(unstable)));

  TEST_ASSERT_EQUAL_INT(155, stable.target);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 5.0, stable.sensitivity);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(20, stable.samples.size());
  TEST_ASSERT_LESS_OR_EQUAL_INT(20, maxAbsoluteError(stable));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.5f, static_cast<float>(maxGain(stable)));

  // Sensitivity remains a user-controlled multiplier. When table geometry is
  // rejected, sensitivity 5 follows the historical sensitivity-5 gain path.
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 5.0f, static_cast<float>(ErgControl::sanitizeSensitivity(5.0)));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 10.0f, static_cast<float>(ErgControl::sanitizeSensitivity(10.0)));

  const double fallback = ErgControl::fallbackGain(5.0, unstable.target);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 5.0f, static_cast<float>(fallback));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 6.25f, static_cast<float>(ErgControl::boundedTableGain(1000.0, fallback)));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.5f, static_cast<float>(ErgControl::boundedTableGain(0.01, fallback)));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 5.625f, static_cast<float>(ErgControl::blendedTableGain(1000.0, fallback)));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.75f, static_cast<float>(ErgControl::blendedTableGain(0.01, fallback)));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, static_cast<float>(fallback), static_cast<float>(ErgControl::boundedTableGain(std::numeric_limits<double>::quiet_NaN(), fallback)));

  double worstReplayedMove = 0.0;
  for (const ErgSample& sample : unstable.samples) {
    const double oldGain = ErgControl::errorScheduledGain(5.0, sample.target - sample.watts, true);
    double gain          = ErgControl::errorScheduledGain(fallback, sample.target - sample.watts, true);
    gain                 = ErgControl::clampGain(gain, 5.0);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, static_cast<float>(oldGain), static_cast<float>(gain));
    worstReplayedMove = std::max(worstReplayedMove, std::abs(gain * (sample.target - sample.watts)));
  }

  std::ofstream report("test/output/active_erg_replay_audit.txt", std::ios::trunc);
  report << "Active ERG ride-log audit\n"
         << "unstable interval: target=" << unstable.target << " sensitivity=" << unstable.sensitivity << " samples=" << unstable.samples.size()
         << " max_error=" << maxAbsoluteError(unstable) << " sign_changes=" << signChanges(unstable) << " max_gain=" << maxGain(unstable) << "\n"
         << "stable interval: target=" << stable.target << " sensitivity=" << stable.sensitivity << " samples=" << stable.samples.size()
         << " max_error=" << maxAbsoluteError(stable) << " sign_changes=" << signChanges(stable) << " max_gain=" << maxGain(stable) << "\n"
         << "uncapped configured sensitivity: " << ErgControl::sanitizeSensitivity(5.0) << "\n"
         << "trusted-table base-gain bounds at 155 W: " << fallback * ErgControl::TABLE_GAIN_MIN_FALLBACK_RATIO << ".." << fallback * ErgControl::TABLE_GAIN_MAX_FALLBACK_RATIO
         << "\n"
         << "worst historical-fallback correction replayed over unstable samples: " << worstReplayedMove << " steps\n";
}

void TestErgLogReplay::test_active_ride_new_gain_replay(void) {
  std::ifstream input(ACTIVE_RIDE_LOG_PATH);
  TEST_ASSERT_TRUE_MESSAGE(input.is_open(), "active ride log must be available to the ERG replay test");

  const std::regex statusPattern(R"(\[([0-9]+)\].*\(Main\): W=(-?[0-9]+) C=(-?[0-9]+))");
  const std::regex entryPattern(R"(\[([0-9]+)\].*\(PTable\): Averaged Entry: watts=([0-9.\-]+), cad=([0-9.\-]+), targetPosition=([0-9.\-]+), \(([0-9]+)\)\(([0-9]+)\))");
  const std::regex samplePattern(R"(\[([0-9]+)\].*ERG_Mode\): ([0-9]+)w, Target ([0-9]+)w, Kp: ([0-9.]+) \((table|fallback)\))");
  const std::regex sensitivityPattern("\\[([0-9]+)\\].*4B:801F([0-9A-Fa-f]{2})00");

  PTData table;
  PTHelpers helpers;
  int cadence                    = 0;
  double sensitivity             = 5.0;
  int samples                    = 0;
  int historicalFallbacks        = 0;
  int newlyTrusted               = 0;
  int newTableSamples            = 0;
  int rejectedAtEdge             = 0;
  double maximumHistoricalMove   = 0.0;
  double maximumNewGain          = 0.0;
  double maximumNewMove          = 0.0;
  double maximumNewlyTrustedMove = 0.0;
  std::string line;
  std::smatch match;

  while (std::getline(input, line)) {
    if (std::regex_search(line, match, sensitivityPattern)) {
      sensitivity = static_cast<double>(std::stoi(match[2].str(), nullptr, 16)) / 10.0;
      continue;
    }
    if (std::regex_search(line, match, statusPattern)) {
      cadence = std::stoi(match[3].str());
      continue;
    }
    if (std::regex_search(line, match, entryPattern)) {
      ptIndex index;
      index.cadIndex  = static_cast<int8_t>(std::stoi(match[5].str()));
      index.wattIndex = static_cast<int8_t>(std::stoi(match[6].str()));
      helpers.enterData(table, index, static_cast<int>(std::stod(match[4].str())));
      continue;
    }
    if (!std::regex_search(line, match, samplePattern) || cadence <= 0) continue;

    const int watts                          = std::stoi(match[2].str());
    const int target                         = std::stoi(match[3].str());
    const double historicalGain              = std::stod(match[4].str());
    const bool oldTable                      = match[5].str() == "table";
    const int error                          = target - watts;
    const double fallback                    = ErgControl::fallbackGain(sensitivity, target);
    double localStepsPerWatt                 = 0.0;
    PowerTableSlopeStatus::Value slopeStatus = PowerTableSlopeStatus::InvalidRequest;
    const bool useTable                      = helpers.lookupErgSlope(target, cadence, localStepsPerWatt, table, &slopeStatus);
    double gain                              = useTable ? ErgControl::blendedTableGain(localStepsPerWatt * sensitivity / ErgControl::SLOPE_CONTROL_DIVISOR, fallback) : fallback;
    gain                                     = ErgControl::errorScheduledGain(gain, error, true);
    gain                                     = ErgControl::clampGain(gain, sensitivity);

    ++samples;
    if (!oldTable) ++historicalFallbacks;
    if (!oldTable && useTable) ++newlyTrusted;
    if (useTable) ++newTableSamples;
    if (slopeStatus == PowerTableSlopeStatus::MissingLocalSupport) ++rejectedAtEdge;
    maximumNewGain        = std::max(maximumNewGain, gain);
    maximumNewMove        = std::max(maximumNewMove, std::abs(gain * error));
    maximumHistoricalMove = std::max(maximumHistoricalMove, std::abs(historicalGain * error));
    if (!oldTable && useTable) maximumNewlyTrustedMove = std::max(maximumNewlyTrustedMove, std::abs(gain * error));
  }

  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(300, samples, "ride log did not yield enough ERG samples for replay");
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, newlyTrusted, "new ERG slope selection did not recover any historical fallback samples");
  // The blended 5.625 base-gain ceiling is intentionally allowed one 1.25x
  // error-scheduling multiplier for errors above 100 W.
  TEST_ASSERT_LESS_OR_EQUAL_FLOAT_MESSAGE(7.03125f, static_cast<float>(maximumNewGain), "new ERG gain exceeded the conservative scheduled cap at sensitivity 5");

  std::ofstream report("test/output/active_erg_new_gain_replay.txt", std::ios::trunc);
  TEST_ASSERT_TRUE_MESSAGE(report.is_open(), "failed to write new ERG replay audit");
  report << "Chronological active-ride ERG replay\n"
         << "samples=" << samples << " historical_fallbacks=" << historicalFallbacks << " newly_trusted=" << newlyTrusted << " new_table_samples=" << newTableSamples
         << " edge_or_missing_segment_rejections=" << rejectedAtEdge << '\n'
         << "maximum_logged_correction_steps=" << maximumHistoricalMove << " maximum_new_gain=" << maximumNewGain << " maximum_new_correction_steps=" << maximumNewMove
         << " maximum_newly_trusted_correction_steps=" << maximumNewlyTrustedMove << '\n'
         << "Table state is rebuilt in log order; each ERG sample uses the last logged Main cadence and only PTable entries already seen.\n";
  TEST_ASSERT_TRUE_MESSAGE(report.good(), "failed while writing new ERG replay audit");
}

void TestErgLogReplay::test_table_position_confidence(void) {
  ErgControl::TableConfidence confidence;

  TEST_ASSERT_FALSE(confidence.trusted());
  TEST_ASSERT_EQUAL_UINT8(0, confidence.score());
  for (int hit = 0; hit < ErgControl::TableConfidence::TRUST_SCORE; ++hit) confidence.update(true);
  TEST_ASSERT_TRUE(confidence.trusted());
  TEST_ASSERT_EQUAL_UINT8(ErgControl::TableConfidence::TRUST_SCORE, confidence.score());

  // Trust is hysteretic: one miss does not disable a proven table, but misses
  // remove confidence twice as quickly as successful observations add it.
  confidence.update(false);
  TEST_ASSERT_TRUE(confidence.trusted());
  confidence.update(false);
  TEST_ASSERT_FALSE(confidence.trusted());
  TEST_ASSERT_EQUAL_UINT8(ErgControl::TableConfidence::REVOKE_SCORE, confidence.score());

  TEST_ASSERT_TRUE(ErgControl::positionMatchesPowerWindow(1000, 900, 1100, 10));
  TEST_ASSERT_TRUE(ErgControl::positionMatchesPowerWindow(1110, 1100, 900, 10));
  TEST_ASSERT_FALSE(ErgControl::positionMatchesPowerWindow(1111, 900, 1100, 10));
  TEST_ASSERT_FALSE(ErgControl::positionMatchesPowerWindow(1000, 900, 1100, -1));

  PTData boundsTable;
  boundsTable.tableRow[2].tableEntry[3].targetPosition  = 100;
  boundsTable.tableRow[2].tableEntry[3].readings        = 2;
  boundsTable.tableRow[2].tableEntry[7].targetPosition  = 200;
  boundsTable.tableRow[2].tableEntry[7].readings        = 2;
  boundsTable.tableRow[6].tableEntry[4].targetPosition  = 120;
  boundsTable.tableRow[6].tableEntry[4].readings        = 2;
  boundsTable.tableRow[6].tableEntry[10].targetPosition = 300;
  boundsTable.tableRow[6].tableEntry[10].readings       = 2;
  // A row with only one reliable point is ignored because lookup() cannot
  // establish a watt-to-position slope there.
  boundsTable.tableRow[9].tableEntry[15].targetPosition = 400;
  boundsTable.tableRow[9].tableEntry[15].readings       = 2;

  const ErgControl::RecordedTableBounds syntheticBounds = ErgControl::recordedTableBounds(boundsTable);
  TEST_ASSERT_TRUE(syntheticBounds.valid);
  TEST_ASSERT_EQUAL_INT(90, syntheticBounds.minWatts);
  TEST_ASSERT_EQUAL_INT(300, syntheticBounds.maxWatts);
  TEST_ASSERT_EQUAL_INT(70, syntheticBounds.minCadence);
  TEST_ASSERT_EQUAL_INT(90, syntheticBounds.maxCadence);
  TEST_ASSERT_TRUE(syntheticBounds.contains(200, 80));
  TEST_ASSERT_FALSE(syntheticBounds.contains(89, 80));
  TEST_ASSERT_FALSE(syntheticBounds.contains(200, 91));

  PTData table;
  RideReplaySummary tableSummary;
  TEST_ASSERT_TRUE_MESSAGE(replayActiveRideLog(table, tableSummary), "active ride log could not be replayed for confidence validation");

  PTData unusedStatusTable;
  StatusReplaySummary statusSummary;
  std::vector<StatusPowerSample> samples;
  TEST_ASSERT_TRUE_MESSAGE(replayStatusLog(ACTIVE_RIDE_LOG_PATH, unusedStatusTable, statusSummary, &samples),
                           "settled ride samples could not be replayed for confidence validation");

  ErgControl::TableConfidence replayConfidence;
  const ErgControl::RecordedTableBounds replayBounds = ErgControl::recordedTableBounds(table);
  PTHelpers helpers;
  int eligibleSamples = 0;
  int accurateSamples = 0;
  bool becameTrusted  = false;
  for (const StatusPowerSample& sample : samples) {
    if (!replayBounds.contains(sample.watts, sample.cadence)) continue;
    const int32_t lowPosition  = helpers.lookup(std::max(0, sample.watts - ERG_MODE_PID_WINDOW), sample.cadence, table);
    const int32_t highPosition = helpers.lookup(sample.watts + ERG_MODE_PID_WINDOW, sample.cadence, table);
    if (lowPosition == RETURN_ERROR || highPosition == RETURN_ERROR) continue;

    const bool accurate = ErgControl::positionMatchesPowerWindow(sample.currentPosition, lowPosition, highPosition, static_cast<int32_t>(TABLE_DIVISOR));
    replayConfidence.update(accurate);
    becameTrusted = becameTrusted || replayConfidence.trusted();
    ++eligibleSamples;
    if (accurate) ++accurateSamples;
  }

  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(3000, eligibleSamples, "confidence replay did not retain enough settled samples");
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(eligibleSamples * 3 / 5, accurateSamples, "the dynamic position window rejected more than 40% of settled samples");
  TEST_ASSERT_TRUE_MESSAGE(becameTrusted, "the power table never earned trust from the settled ride samples");
}
