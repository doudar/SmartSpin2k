/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "ERG_Mode_Settings.h"
#include "SmartSpin_parameters.h"
#include "ERG_Mode_Utils.h"

#define ERG_MODE_LOG_CSV_TAG "ERG_Mode_CSV"
#define ERG_MODE_LOG_TAG     "ERG_Mode"

struct Mode {
  static const int MAINTAIN   = 0;
  static const int DECREASING = 1;
  static const int INCREASING = 2;
};

class ErgMode {
 public:
  // What used to be in the ERGTaskLoop(). This is the main control function for ERG Mode and the powertable operations.
  void runERG();
  void computeErg();
  // Called after reading actual motor position, before interpreting targetIncline.
  void prepareMode();
  bool collectionAllowed() const {
    return !(isTableSeeking() || feedbackWaiting) || controlMaximum - static_cast<int64_t>(controlMinimum) < POWER_SAMPLE_POSITION_SPAN;
  }
  void _writeLog(float currentIncline, float newIncline, int currentSetPoint, int newSetPoint, int currentWatts, int newWatts, int currentCadence, int newCadence);
  bool isTableSeeking() const { return tableSeekState != TableSeekState::INACTIVE; }
  void resetTableConfidence() {
    tableConfidence.reset();
    tableSeekState           = TableSeekState::INACTIVE;
    mode                     = Mode::MAINTAIN;
    tableSeekStableMatches   = 0;
    tableSeekStableMisses    = 0;
    tableSeekPidSeedValid    = false;
    feedbackWaiting          = false;
    tableCorrectionNeedsProgress = false;
    confidenceWattsTimestamp = 0;
    confidenceCadence        = 0;
    confidenceWasHomed       = false;
    confidenceSettledAt      = 0;
    cadenceReference         = 0;
    responseTimestamp        = 0;
    responseTrend            = 0;
    feedbackEarlyRetreatTarget = INT32_MIN;
  }

 private:
  enum class TableSeekState : uint8_t {
    INACTIVE,
    MOVING,
    SETTLING,
  };

  int mode = Mode::MAINTAIN;
  Measurement prevWatts;
  Measurement prevCadence;
  ErgControl::TableConfidence tableConfidence;
  TableSeekState tableSeekState          = TableSeekState::INACTIVE;
  int tableSeekTargetWatts               = 0;
  int tableSeekCadence                   = 0;
  int tableSeekLastWatts                 = INT32_MIN;
  int tableSeekStableMatches             = 0;
  int tableSeekStableMisses              = 0;
  int32_t tableSeekPosition              = 0;
  unsigned long tableSeekDeadline        = 0;
  unsigned long tableSeekWattsTimestamp  = 0;
  unsigned long confidenceWattsTimestamp = 0;
  int confidenceCadence                  = 0;
  bool confidenceWasHomed                = false;
  bool tableSeekPidSeedValid             = false;
  int32_t tableSeekPidSeedPosition       = 0;
  bool feedbackWaiting                   = false;
  // A table move must demonstrate progress before another can interrupt proportional recovery.
  bool tableCorrectionNeedsProgress      = false;
  bool feedbackMotorSettled              = false;
  bool feedbackIncreasing                = false;
  int feedbackTargetWatts                = 0;
  int feedbackStartWatts                 = 0;
  int feedbackEarlyRetreatTarget         = INT32_MIN;
  uint32_t feedbackStartedAt             = 0;
  uint32_t feedbackSettledAt             = 0;
  uint32_t confidenceSettledAt           = 0;
  int32_t confidencePosition             = 0;
  int cadenceReference                   = 0;
  uint32_t tableSeekStartedAt            = 0;
  int32_t tableSeekOffset                = 0;
  uint32_t tableSeekArrivedAt            = 0;
  uint32_t responseTimestamp             = 0;
  int responseWatts                      = 0;
  double responseTrend                   = 0;
  bool wasErgMode                        = false;
  int32_t controlMinimum                 = 0;
  int32_t controlMaximum                 = 0;

  // calculate incline if setpoint (from Zwift) changes
  int32_t _setPointChangeState();

  // calculate incline if setpoint is unchanged
  int32_t _inSetpointState();
  int32_t _tableCorrection(int watts, int target, int cadence);
  void _observePowerResponse();
  void _trackControlMove(int32_t position);

  void _updateTableConfidence();
  bool _positionPredictionIsAccurate(int watts, int cadence, int32_t actualPosition);
  bool _tableHasSeekSupport() const;
  bool _tableTargetIsTrusted(int watts, int cadence) const;
  bool _tableTargetIsWithinMeasuredBounds(int watts, int cadence) const;
  bool _tableTargetIsUsable(int watts, int cadence) const;
  void _scoreTable(int watts, int cadence, bool accurate);
  void _startTrustedTableSeek(int32_t position);
  void _handleTrustedTableSeek();
  void _stopTrustedTableSeek(const char* reason, bool seedPidFromTable = false, bool acquireFeedback = false);
  unsigned long _trustedTableMoveDeadline(int32_t position) const;
  void _startFeedbackWait();
  void _handleFeedbackWait();

  // update localvalues + incline, creates a log
  void _updateValues(float newIncline);
};

extern ErgMode* ergMode;
