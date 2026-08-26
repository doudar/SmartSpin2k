/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>

#include <iomanip>

#include "test.h"
#include "test_data_helpers.h"

void TestActiveRideTable::test_active_ride_table_generation(void) {
  PTData ptData;
  RideReplaySummary summary;
  TEST_ASSERT_TRUE_MESSAGE(replayActiveRideLog(ptData, summary), "active ride log could not be opened");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, summary.invalidEntries, "active ride log contains invalid power-table entries");
  TEST_ASSERT_EQUAL_INT_MESSAGE(552, summary.entries, "active ride log entry count changed or entries failed to parse");

  PTHelpers helpers;
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, helpers.getTotalReadings(ptData), "active ride replay produced an empty power table");
  PowerTableValidationSummary validation;
  std::string failure;
  TEST_ASSERT_TRUE_MESSAGE(validatePowerTableSurface(ptData, validation, failure), failure.c_str());
  TEST_ASSERT_TRUE_MESSAGE(savePTDataToCSV(ptData, ACTIVE_POWER_TABLE_OUTPUT_PATH), "failed to write generated active power table");
  TEST_ASSERT_TRUE_MESSAGE(savePowerTableViewer(ptData, ACTIVE_POWER_TABLE_VIEWER_PATH), "failed to write active power-table graphical viewer");

  PTData reloaded;
  TEST_ASSERT_TRUE_MESSAGE(loadCSVToPTData(ACTIVE_POWER_TABLE_OUTPUT_PATH, reloaded), "generated active power table could not be reloaded");
  TEST_ASSERT_TRUE_MESSAGE(powerTablePositionsMatch(ptData, reloaded, failure), failure.c_str());
}

void TestActiveRideTable::test_status_ride_table_generation(void) {
  PTData statusTable;
  StatusReplaySummary statusSummary;
  TEST_ASSERT_TRUE_MESSAGE(replayActiveStatusLog(statusTable, statusSummary), "active ride log could not be opened for status replay");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, statusSummary.invalidSamples, "active ride log contains malformed Main status samples");
  TEST_ASSERT_EQUAL_INT_MESSAGE(667, statusSummary.statusSamples, "active ride Main status sample count changed or samples failed to parse");
  TEST_ASSERT_EQUAL_INT_MESSAGE(548, statusSummary.acceptedSamples, "status-derived table accepted an unexpected number of settled samples");
  TEST_ASSERT_EQUAL_INT_MESSAGE(5, statusSummary.highCadenceSamples, "status replay did not collect all settled 103-107 RPM samples into the 105 RPM row");

  TEST_ASSERT_TRUE_MESSAGE(savePTDataToCSV(statusTable, ACTIVE_STATUS_POWER_TABLE_OUTPUT_PATH), "failed to write status-derived power table");
  TEST_ASSERT_TRUE_MESSAGE(savePowerTableViewer(statusTable, ACTIVE_STATUS_POWER_TABLE_VIEWER_PATH,
                                                "SmartSpin2k status-derived power table",
                                                "Settled Main status samples from active_ride_log.txt"),
                           "failed to write status-derived graphical viewer");
  PowerTableValidationSummary validation;
  std::string failure;
  TEST_ASSERT_TRUE_MESSAGE(validatePowerTableSurface(statusTable, validation, failure), failure.c_str());
  PTData reloadedStatusTable;
  TEST_ASSERT_TRUE_MESSAGE(loadCSVToPTData(ACTIVE_STATUS_POWER_TABLE_OUTPUT_PATH, reloadedStatusTable),
                           "status-derived power table could not be reloaded");
  TEST_ASSERT_TRUE_MESSAGE(powerTablePositionsMatch(statusTable, reloadedStatusTable, failure), failure.c_str());

  PTData firmwareTable;
  RideReplaySummary firmwareSummary;
  TEST_ASSERT_TRUE_MESSAGE(replayActiveRideLog(firmwareTable, firmwareSummary), "active ride log could not be replayed for status comparison");

  int firmwareCells = 0;
  int statusCells = 0;
  int overlappingCells = 0;
  int firmwareOnlyCells = 0;
  int statusOnlyCells = 0;
  int maximumPositionDelta = 0;
  int totalPositionDelta = 0;
  int worstCadence = 0;
  int worstWatts = 0;
  std::ostringstream differences;
  for (int row = 0; row < POWERTABLE_CAD_SIZE; ++row) {
    for (int col = 0; col < POWERTABLE_WATT_SIZE; ++col) {
      const int16_t firmwarePosition = firmwareTable.tableRow[row].tableEntry[col].targetPosition;
      const int16_t statusPosition = statusTable.tableRow[row].tableEntry[col].targetPosition;
      const bool firmwareMeasured = firmwarePosition != INT16_MIN;
      const bool statusMeasured = statusPosition != INT16_MIN;
      if (firmwareMeasured) ++firmwareCells;
      if (statusMeasured) ++statusCells;
      if (firmwareMeasured && statusMeasured) {
        ++overlappingCells;
        const int delta = std::abs(static_cast<int>(statusPosition) - firmwarePosition);
        totalPositionDelta += delta;
        if (delta > maximumPositionDelta) {
          maximumPositionDelta = delta;
          worstCadence = MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT;
          worstWatts = col * POWERTABLE_WATT_INCREMENT;
        }
      } else if (firmwareMeasured) {
        ++firmwareOnlyCells;
      } else if (statusMeasured) {
        ++statusOnlyCells;
        differences << "status-only: cadence=" << MINIMUM_TABLE_CAD + row * POWERTABLE_CAD_INCREMENT
                    << " watts=" << col * POWERTABLE_WATT_INCREMENT << " position=" << statusPosition << '\n';
      }
    }
  }

  std::ofstream report(ACTIVE_STATUS_POWER_TABLE_AUDIT_PATH, std::ios::trunc);
  TEST_ASSERT_TRUE_MESSAGE(report.is_open(), "failed to open status-derived power-table audit report");
  report << "Status-derived power-table audit\n"
         << "status records=" << statusSummary.statusSamples << " accepted=" << statusSummary.acceptedSamples
         << " invalid=" << statusSummary.invalidSamples << " disconnected=" << statusSummary.rejectedDisconnected
         << " out_of_range=" << statusSummary.rejectedOutOfRange << " moving=" << statusSummary.rejectedMoving << '\n'
         << "accepted final-cadence-row samples=" << statusSummary.highCadenceSamples << '\n'
         << "firmware cells=" << firmwareCells << " status cells=" << statusCells << " overlap=" << overlappingCells
         << " firmware_only=" << firmwareOnlyCells << " status_only=" << statusOnlyCells << '\n'
         << "overlap mean position delta=" << (overlappingCells > 0 ? static_cast<double>(totalPositionDelta) / overlappingCells : 0.0)
         << " maximum=" << maximumPositionDelta << " at " << worstCadence << " RPM / " << worstWatts << " W\n"
         << "trusted status-table round trips=" << validation.trustedRoundTrips
         << " maximum error=" << validation.maximumRoundTripError << "W\n"
         << differences.str();
  TEST_ASSERT_TRUE_MESSAGE(report.good(), "failed while writing status-derived power-table audit report");
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, overlappingCells, "status and firmware streams produced no overlapping table cells");
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, statusOnlyCells, "status stream did not expose any additional table coverage");
}

void TestActiveRideTable::test_active_table_status_prediction_accuracy(void) {
  PTData finalTable;
  TEST_ASSERT_TRUE_MESSAGE(loadCSVToPTData(ACTIVE_POWER_TABLE_OUTPUT_PATH, finalTable),
                           "final active power-table output could not be loaded for status prediction test");

  PTData unusedStatusTable;
  StatusReplaySummary statusSummary;
  std::vector<StatusPowerSample> samples;
  TEST_ASSERT_TRUE_MESSAGE(replayStatusLog(ACTIVE_RIDE_LOG_PATH, unusedStatusTable, statusSummary, &samples),
                           "active ride log could not be opened for status prediction test");
  TEST_ASSERT_EQUAL_INT_MESSAGE(statusSummary.acceptedSamples, samples.size(),
                                "status prediction sample capture did not match accepted status count");
  TEST_ASSERT_EQUAL_INT_MESSAGE(548, samples.size(), "status prediction test did not receive every settled active-log sample");

  PTHelpers helpers;
  std::vector<int> positionErrors;
  std::vector<int> wattErrors;
  std::vector<double> positionPercentErrors;
  std::vector<double> wattPercentErrors;
  std::vector<int> trustedPositionErrors;
  std::vector<int> trustedWattErrors;
  long long signedPositionErrorTotal = 0;
  long long signedWattErrorTotal = 0;
  int worstPositionError = -1;
  int worstWattError = -1;
  StatusPowerSample worstPositionSample = {};
  StatusPowerSample worstWattSample = {};
  int worstPredictedPosition = 0;
  int worstPredictedWatts = 0;
  char failure[260];
  std::ostringstream comparisons;
  comparisons << "timestamp,cadence,actual_watts,predicted_watts,watt_error,actual_position,predicted_position,position_error,slope_vetted\n";

  for (const StatusPowerSample& sample : samples) {
    const int32_t predictedPosition = helpers.lookup(sample.watts, sample.cadence, finalTable);
    std::snprintf(failure, sizeof(failure),
                  "forward status prediction failed: timestamp=%d cadence=%d watts=%d actual_position=%d",
                  sample.timestamp, sample.cadence, sample.watts, sample.currentPosition);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RETURN_ERROR, predictedPosition, failure);

    const int predictedWatts = helpers.lookupWatts(sample.cadence, sample.currentPosition, finalTable);
    std::snprintf(failure, sizeof(failure),
                  "reverse status prediction returned invalid watts: timestamp=%d cadence=%d position=%d predicted_watts=%d",
                  sample.timestamp, sample.cadence, sample.currentPosition, predictedWatts);
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(0, predictedWatts, failure);
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(4000, predictedWatts, failure);

    const int signedPositionError = static_cast<int>(predictedPosition) - sample.currentPosition;
    const int signedWattError = predictedWatts - sample.watts;
    const int positionError = std::abs(signedPositionError);
    const int wattError = std::abs(signedWattError);
    positionErrors.push_back(positionError);
    wattErrors.push_back(wattError);
    positionPercentErrors.push_back(100.0 * positionError / sample.currentPosition);
    wattPercentErrors.push_back(100.0 * wattError / sample.watts);
    signedPositionErrorTotal += signedPositionError;
    signedWattErrorTotal += signedWattError;

    if (positionError > worstPositionError) {
      worstPositionError = positionError;
      worstPositionSample = sample;
      worstPredictedPosition = predictedPosition;
    }
    if (wattError > worstWattError) {
      worstWattError = wattError;
      worstWattSample = sample;
      worstPredictedWatts = predictedWatts;
    }

    double localStepsPerWatt;
    const bool slopeVetted = helpers.lookupSlope(sample.watts, sample.cadence, localStepsPerWatt, finalTable);
    if (slopeVetted) {
      trustedPositionErrors.push_back(positionError);
      trustedWattErrors.push_back(wattError);
    }
    comparisons << sample.timestamp << ',' << sample.cadence << ',' << sample.watts << ',' << predictedWatts << ',' << wattError
                << ',' << sample.currentPosition << ',' << predictedPosition << ',' << positionError << ',' << (slopeVetted ? 1 : 0) << '\n';
  }

  const auto percentile = [](std::vector<int> values, int percent) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>(std::round((values.size() - 1) * percent / 100.0));
    return values[index];
  };
  const auto mean = [](const std::vector<int>& values) {
    long long total = 0;
    for (int value : values) total += value;
    return values.empty() ? 0.0 : static_cast<double>(total) / values.size();
  };
  const auto percentPercentile = [](std::vector<double> values, int percent) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>(std::round((values.size() - 1) * percent / 100.0));
    return values[index];
  };

  const int positionMedian = percentile(positionErrors, 50);
  const int positionP95 = percentile(positionErrors, 95);
  const int wattMedian = percentile(wattErrors, 50);
  const int wattP95 = percentile(wattErrors, 95);
  const int trustedPositionP95 = percentile(trustedPositionErrors, 95);
  const int trustedWattP95 = percentile(trustedWattErrors, 95);
  const double positionPercentP95 = percentPercentile(positionPercentErrors, 95);
  const double wattPercentP95 = percentPercentile(wattPercentErrors, 95);

  std::ofstream report(ACTIVE_POWER_TABLE_PREDICTION_AUDIT_PATH, std::ios::trunc);
  TEST_ASSERT_TRUE_MESSAGE(report.is_open(), "failed to open active power-table prediction audit report");
  report << std::fixed << std::setprecision(1)
         << "For the supplied active_ride_log.txt using active_power_table.ptab, the results were:\n"
         << "Watt lookup error was p95 +/-" << wattPercentP95 << "% or +/-" << wattP95 << " W\n"
         << "Resistance lookup error was p95 +/-" << positionPercentP95 << "% or +/-" << positionP95 << " steps\n"
         << "Maximum watt error was +/-" << worstWattError << " W\n"
         << "Maximum resistance error was +/-" << worstPositionError << " steps\n\n"
         << std::defaultfloat << std::setprecision(6)
         << "Active table versus settled status samples\n"
         << "samples=" << samples.size() << " trusted_by_local_slope=" << trustedPositionErrors.size() << '\n'
         << "forward position error (steps): mean=" << mean(positionErrors) << " median=" << positionMedian
         << " p95=" << positionP95 << " maximum=" << worstPositionError
         << " signed_bias=" << static_cast<double>(signedPositionErrorTotal) / samples.size() << '\n'
         << "worst forward sample: timestamp=" << worstPositionSample.timestamp << " cadence=" << worstPositionSample.cadence
         << " watts=" << worstPositionSample.watts << " actual_position=" << worstPositionSample.currentPosition
         << " predicted_position=" << worstPredictedPosition << '\n'
         << "reverse watt error: mean=" << mean(wattErrors) << " median=" << wattMedian << " p95=" << wattP95
         << " maximum=" << worstWattError << " signed_bias=" << static_cast<double>(signedWattErrorTotal) / samples.size() << '\n'
         << "worst reverse sample: timestamp=" << worstWattSample.timestamp << " cadence=" << worstWattSample.cadence
         << " position=" << worstWattSample.currentPosition << " actual_watts=" << worstWattSample.watts
         << " predicted_watts=" << worstPredictedWatts << '\n'
         << "trusted-region p95: position=" << trustedPositionP95 << " steps watts=" << trustedWattP95 << "W\n\n"
         << comparisons.str();
  TEST_ASSERT_TRUE_MESSAGE(report.good(), "failed while writing active power-table prediction audit report");

  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, trustedPositionErrors.size(), "no settled status samples were inside a slope-vetted table region");
  std::snprintf(failure, sizeof(failure), "forward status prediction p95 exceeded 800 steps: p95=%d worst=%d", positionP95, worstPositionError);
  TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(800, positionP95, failure);
  std::snprintf(failure, sizeof(failure), "reverse status prediction p95 exceeded 35W: p95=%d worst=%d", wattP95, worstWattError);
  TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(35, wattP95, failure);
  std::snprintf(failure, sizeof(failure), "slope-vetted forward prediction p95 exceeded 800 steps: p95=%d", trustedPositionP95);
  TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(800, trustedPositionP95, failure);
  std::snprintf(failure, sizeof(failure), "slope-vetted reverse prediction p95 exceeded 35W: p95=%d", trustedWattP95);
  TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(35, trustedWattP95, failure);
}
