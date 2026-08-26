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

int errorSign(int error) {
  return (error > 0) - (error < 0);
}

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

  std::vector<const ErgInterval*> hardIntervals;
  for (const ErgInterval& interval : intervals) {
    if (interval.target >= 350 && interval.samples.size() >= 5) hardIntervals.push_back(&interval);
  }

  TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(3, hardIntervals.size(), "ride log should contain the three early hard intervals");
  const ErgInterval& unstable = *hardIntervals[1];
  const ErgInterval& stable   = *hardIntervals[2];

  TEST_ASSERT_EQUAL_INT(385, unstable.target);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 5.0, unstable.sensitivity);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(8, unstable.samples.size());
  TEST_ASSERT_GREATER_OR_EQUAL_INT(50, maxAbsoluteError(unstable));
  TEST_ASSERT_GREATER_OR_EQUAL_INT(5, signChanges(unstable));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, static_cast<float>(maxGain(unstable)));

  TEST_ASSERT_EQUAL_INT(390, stable.target);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 3.0, stable.sensitivity);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(8, stable.samples.size());
  TEST_ASSERT_LESS_OR_EQUAL_INT(30, maxAbsoluteError(stable));
  TEST_ASSERT_LESS_THAN_FLOAT(8.0f, static_cast<float>(maxGain(stable)));

  // Sensitivity remains a user-controlled multiplier. When table geometry is
  // rejected, sensitivity 5 follows the historical sensitivity-5 gain path.
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 5.0f, static_cast<float>(ErgControl::sanitizeSensitivity(5.0)));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 10.0f, static_cast<float>(ErgControl::sanitizeSensitivity(10.0)));

  const double fallback = ErgControl::fallbackGain(5.0, unstable.target);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 5.0f, static_cast<float>(fallback));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 10.0f, static_cast<float>(ErgControl::boundedTableGain(1000.0, fallback)));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.5f, static_cast<float>(ErgControl::boundedTableGain(0.01, fallback)));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, static_cast<float>(fallback),
                           static_cast<float>(ErgControl::boundedTableGain(std::numeric_limits<double>::quiet_NaN(), fallback)));

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
         << "trusted-table base-gain bounds at 385 W: " << fallback * ErgControl::TABLE_GAIN_MIN_FALLBACK_RATIO << ".."
         << fallback * ErgControl::TABLE_GAIN_MAX_FALLBACK_RATIO << "\n"
         << "worst historical-fallback correction replayed over unstable samples: " << worstReplayedMove << " steps\n";
}
