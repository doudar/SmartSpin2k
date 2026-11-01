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
  bool collectionAllowed() const { return !(isTableSeeking() || feedbackWaiting) || controlMaximum - static_cast<int64_t>(controlMinimum) < POWER_SAMPLE_POSITION_SPAN; }
  void _writeLog(float currentIncline, float newIncline, int currentSetPoint, int newSetPoint, int currentWatts, int newWatts, int currentCadence, int newCadence);
  bool isTableSeeking() const { return tableSeekState != TableSeekState::INACTIVE; }
  void resetTableConfidence() {
    tableConfidence.reset();
    tableSeekState               = TableSeekState::INACTIVE;
    tableSeekDropCadence         = false;
    tableSeekFirstArrivalValid   = false;
    tableSeekHadHighTarget       = false;
    mode                         = Mode::MAINTAIN;
    tableSeekStableMatches       = 0;
    tableSeekStableMisses        = 0;
    tableSeekPidSeedValid        = false;
    feedbackWaiting              = false;
    tableCorrectionNeedsProgress = false;
    confidenceWattsTimestamp     = 0;
    confidenceCadence            = 0;
    confidenceWasHomed           = false;
    confidenceSettledAt          = 0;
    cadenceReference             = 0;
    responseTimestamp            = 0;
    responsePreviousFresh        = false;
    responseTrend                = 0;
    responsePreviousTrend        = 0;
    transitionTarget             = INT32_MIN;
    highPowerTransition          = false;
    boundedSeekTransition        = false;
    hasHighPowerRequest          = false;
    offsetSettledAt = offsetPosition = offsetCadence = 0;
    feedbackEarlyRetreatTarget   = INT32_MIN;
    feedbackUpdateValid         = false;
  }

 private:
  enum class TableSeekState : uint8_t {
    INACTIVE,
    MOVING,
    SETTLING,
  };

  int mode = Mode::MAINTAIN;
  // Target to restore after a cadence stop; it belongs to this ERG session.
  int pausedTargetWatts = 0;
  Measurement prevWatts;
  Measurement prevCadence;
  ErgControl::TableConfidence tableConfidence;
  TableSeekState tableSeekState          = TableSeekState::INACTIVE;
  int tableSeekTargetWatts               = 0;
  bool tableSeekHadHighTarget             = false;
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
  bool tableCorrectionNeedsProgress = false;
  bool feedbackMotorSettled         = false;
  bool feedbackIncreasing           = false;
  int feedbackTargetWatts           = 0;
  int feedbackStartWatts            = 0;
  int feedbackEarlyRetreatTarget    = INT32_MIN;
  uint32_t feedbackStartedAt        = 0;
  uint32_t feedbackSettledAt        = 0;
  uint32_t confidenceSettledAt      = 0;
  int32_t confidencePosition        = 0;
  int cadenceReference              = 0;
  // Spin-up after a real stop (see ERG_SPINUP_* in ERG_Mode_Settings.h).
  uint32_t spinUpLowSince  = 0;      // first report at or below MIN_ERG_CADENCE in the current low run (0: none)
  bool spinUpArmed         = false;  // a real stop was seen; hold increases until cadence settles
  uint32_t spinUpSince     = 0;      // when cadence first rose above MIN_ERG_CADENCE after the stop (0: not yet)
  bool spinUpEased         = false;  // the one-time reduction toward a lower restored target was considered
  bool spinUpSeekPending   = false;  // the hold ended; force one setpoint seek once no seek/wait is active
  double spinUpRidingCadence = 0;    // ~5 s average of riding cadence, frozen during stops (for easing off)
  uint32_t spinUpRidingAt    = 0;
  static constexpr int SPIN_UP_SAMPLES = 32;
  uint32_t spinUpSampleAt[SPIN_UP_SAMPLES] = {};
  int spinUpSampleCadence[SPIN_UP_SAMPLES] = {};
  int spinUpSampleCount = 0, spinUpSampleNext = 0;
  bool _spinUpHold(uint32_t now);
  void _resetSpinUp();
  bool _spinUpCadenceSettled(uint32_t now, int cadence);
  void _spinUpEase(int cadence);
  uint32_t tableSeekStartedAt       = 0;
  int32_t tableSeekOffset           = 0;
  uint32_t tableSeekArrivedAt       = 0;
  bool tableSeekFirstArrivalValid   = false;
  uint32_t tableSeekFirstArrivedAt  = 0;
  int tableSeekStartWatts           = 0;
  int tableSeekStartCadence         = 0;
  uint32_t responseTimestamp        = 0;
  int responseWatts                 = 0;
  int responsePreviousWatts         = 0;
  bool responsePreviousFresh        = false;
  double responseTrend              = 0;
  double responsePreviousTrend      = 0;
  int transitionTarget              = INT32_MIN;
  // High transitions bound the residual; low trusted seeks limit prediction
  // to nominal sensitivity without changing ordinary proportional feedback.
  bool highPowerTransition          = false;
  bool boundedSeekTransition        = false;
  bool hasHighPowerRequest          = false;
  uint32_t offsetSettledAt           = 0;
  int32_t offsetPosition             = 0;
  int offsetCadence                  = 0;
  bool wasErgMode                   = false;
  bool feedbackUpdateValid          = false;
  uint32_t feedbackUpdatedAt        = 0;
  int32_t controlMinimum            = 0;
  int32_t controlMaximum            = 0;
  // Stepped-resistance steady-state hold: side and start of a persisting moderate error.
  int steppedErrorSign              = 0;
  uint32_t steppedErrorSince        = 0;
  bool _steppedHold(int measuredError);
  bool _hasSteppedFeedback() const;
  // Recovery drops with stepped feedback are sized at the rider's held cadence (ERG_STEPPED_DROP_CADENCE_ALLOWANCE_RPM).
  ErgControl::CadenceHistory cadenceHistory;
  int historyTarget         = 0;
  uint32_t historySettledAt = 0;     // when power last came within ERG_STEPPED_IMMEDIATE_WATTS of historyTarget (0: not)
  bool tableSeekDropCadence = false; // the active seek used a held cadence below the rider's current cadence
  void _recordCadenceHistory(bool holding);
  int _dropSeekCadence(int target, int cadence, const char*& source);

  // calculate incline if setpoint (from Zwift) changes
  int32_t _setPointChangeState();

  // calculate incline if setpoint is unchanged
  int32_t _inSetpointState();
  int32_t _tableCorrection(int watts, int target, int cadence);
  void _observePowerResponse();
  void _trackControlMove(int32_t position);

  void _updateTableConfidence();
  void _observeOffsetSettlement();
  bool _positionPredictionIsAccurate(int watts, int cadence, int32_t actualPosition);
  bool _tableHasSeekSupport() const;
  bool _tableTargetIsTrusted(int watts, int cadence) const;
  bool _tableTargetIsWithinMeasuredBounds(int watts, int cadence) const;
  bool _tableTargetIsUsable(int watts, int cadence) const;
  int32_t _constrainedTablePosition(int watts, int cadence) const;
  void _scoreTable(int watts, int cadence, bool accurate);
  void _startTrustedTableSeek(int32_t position, int seekCadence = -1);
  void _handleTrustedTableSeek();
  void _stopTrustedTableSeek(const char* reason, bool seedPidFromTable = false, bool acquireFeedback = false);
  unsigned long _trustedTableMoveDeadline(int32_t position) const;
  void _startFeedbackWait();
  void _handleFeedbackWait();

  // update localvalues + incline, creates a log
  void _updateValues(float newIncline);
};

extern ErgMode* ergMode;
