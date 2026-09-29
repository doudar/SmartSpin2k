/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <cstdint>

#include "settings.h"

// ERG controller and helper tuning. Shared device defaults, feature switches,
// ERG_MODE_PID_WINDOW, and power-table dimensions remain in settings.h.

// ******************************************************************************
// Control cadence and logging
// Paces table housekeeping; fresh sensor reports drive ordinary ERG corrections.
// ******************************************************************************
// Milliseconds between table housekeeping passes; also sets the per-correction motor travel cap from stepper speed.
#define ERG_MODE_DELAY 700
// Minimum interval between proportional-control diagnostic log entries.
constexpr int ERG_MODE_LOG_INTERVAL_MS = 2000;
// ******************************************************************************

// ******************************************************************************
// Table prediction tolerances and stable observations
// Used to validate table predictions and decide when trusted seeks have settled.
// ******************************************************************************
// Extra steps around the predicted power window; also ignores smaller cadence-driven seek target adjustments.
constexpr int ERG_TABLE_POSITION_PADDING_STEPS = static_cast<int>(TABLE_DIVISOR);
// Position tolerance for seek arrival and confidence scoring; feedback waits separately require exact motor completion.
constexpr int ERG_TABLE_SETTLED_POSITION_STEPS = static_cast<int>(TABLE_DIVISOR);
// Maximum cadence change between confidence observations before restarting the stationary acquisition timer.
constexpr int ERG_TABLE_STABLE_CADENCE_DELTA = 2;
// Maximum change between consecutive power readings for a settled seek observation to count as stable.
constexpr int ERG_TABLE_STABLE_WATTS_DELTA = ERG_MODE_PID_WINDOW / 2;
// Consecutive stable matches or misses needed to finish a trusted seek and score its prediction.
constexpr int ERG_TABLE_STABLE_READINGS = 3;
// ******************************************************************************

// ******************************************************************************
// Table seek deadlines
// Bounds movement and settling so an unfinished seek hands off to feedback acquisition.
// ******************************************************************************
// Settling allowance after seek arrival; also added to the estimated movement deadline.
constexpr int ERG_TABLE_SETTLE_TIMEOUT_MS = 5000;
// Overall trusted-seek deadline and movement timeout for feedback waits; cadence updates cannot extend the seek deadline.
constexpr int ERG_TABLE_MOVE_TIMEOUT_MS = 10000;
// ******************************************************************************

// ******************************************************************************
// Feedback acquisition and power freshness
// Prevents another table-sized correction from using delayed or stale power reports.
// Safety exits and a worsening reduction can release acquisition early.
// ******************************************************************************
// Minimum post-settlement sample delay for normal acquisition; also required before stationary confidence misses count.
constexpr uint32_t ERG_FEEDBACK_SETTLE_MS = 2500;
// Maximum post-settlement wait for power response; also added to the movement timeout for the overall feedback deadline.
constexpr uint32_t ERG_FEEDBACK_TIMEOUT_MS = 5000;
// Minimum measured improvement after a table move to permit another table correction and its acquisition pause.
constexpr int ERG_FEEDBACK_MIN_RESPONSE_WATTS = 5;
// Also require this fraction of the starting watt error (1/4 = 25%); insufficient progress hands the residual to proportional control.
constexpr int ERG_FEEDBACK_RESPONSE_DIVISOR = 4;
// Maximum age of a usable power sample for corrections, seeks, confidence scoring, and response-trend tracking.
constexpr uint32_t ERG_FEEDBACK_MAX_AGE_MS = 1500;
// ******************************************************************************

// ******************************************************************************
// Early retry when power rises after a reduction
// Both margins must be exceeded by a fresh post-settlement report to release the
// feedback wait early for one further correction; repeated retries are gated.
// ******************************************************************************
// Required watts above target for the worsening-reduction early retry.
constexpr int ERG_FEEDBACK_HIGH_OVERSHOOT_WATTS = ERG_MODE_PID_WINDOW;
// Required rise above the reduction's starting power for the same early retry.
constexpr int ERG_FEEDBACK_WORSENING_WATTS = 30;
// ******************************************************************************

// ******************************************************************************
// Table correction and cadence seek triggers
// Selects when maintenance can use a relative table correction or a cadence seek.
// ******************************************************************************
// Trend-adjusted error must exceed this magnitude before maintenance tries a relative table correction, provided the previous move made progress.
// A quiet return inside ERG_MODE_PID_WINDOW re-arms table correction after proportional recovery.
constexpr int ERG_TABLE_CORRECTION_WATTS = 44;
// Cadence change from the last reference that can start a trusted maintenance seek; in-flight seeks track smaller changes too.
constexpr int ERG_TABLE_CADENCE_SEEK_RPM = POWERTABLE_CAD_INCREMENT;
// Blend the measured cadence-curve estimate with the normal forward lookup.
constexpr double ERG_TABLE_CURVE_BLEND = 0.5;
// ******************************************************************************

namespace ErgControl {

// Constrain incomplete cadence curves from nearby, reliable row pairs. These
// limits bound extrapolation relative to the measured median spacing; they do
// not fill or modify the learned table.
constexpr int CADENCE_CURVE_MAX_PAIRS               = 9;
constexpr int CADENCE_CURVE_MIN_PAIRS               = 3;
constexpr double CADENCE_CURVE_DISTANCE_WEIGHT      = 0.25;
constexpr double CADENCE_CURVE_MIN_SPACING_RATIO    = 0.5;
constexpr double CADENCE_CURVE_MAX_SPACING_RATIO    = 2.0;
constexpr double CADENCE_CURVE_EXTRAPOLATION_WEIGHT = 2.0;

// ******************************************************************************
// Fallback gain scheduling by operating power
// Sets the proportional gain baseline from target watts and ERG sensitivity.
// Used alone without a usable table slope, or as the baseline for slope blending.
// ******************************************************************************
// Below this power, increase fallback gain inversely with operating watts.
constexpr int LOW_GAIN_WATTS = 120;
// Above this power, reduce fallback gain inversely with operating watts.
constexpr int HIGH_GAIN_WATTS = 400;
// Floor operating watts in the low-power gain calculation to limit amplification.
constexpr int MIN_SCHEDULE_WATTS = 30;
// ******************************************************************************

// ******************************************************************************
// Local slope blending and proportional gain limits
// Used for ERG proportional corrections; direct table seeks bypass these settings.
// The final sensitivity limits also apply when proportional gain uses only fallback.
// ******************************************************************************
// Lower bound for table-derived gain as a fraction of fallback gain before blending.
constexpr double TABLE_GAIN_MIN_FALLBACK_RATIO = 0.5;
// Upper bound for table-derived gain as a multiple of fallback gain before blending.
constexpr double TABLE_GAIN_MAX_FALLBACK_RATIO = 2.0;
// Weight of bounded table gain when blending with fallback gain (0 = fallback, 1 = table).
// Used by blendedTableGain() during ERG proportional control when a usable table slope is available.
constexpr double TABLE_GAIN_BLEND = .5;
// Minimum final gain as a fraction of the configured ERG sensitivity.
constexpr double GAIN_MIN_SENSITIVITY_RATIO = .5;
// Maximum final gain as a multiple of the configured ERG sensitivity.
constexpr double GAIN_MAX_SENSITIVITY_RATIO = 4.0;
// Divide the table's steps-per-watt slope times sensitivity by this to obtain control gain.
constexpr double SLOPE_CONTROL_DIVISOR = 5.0;
// ******************************************************************************

// ******************************************************************************
// Proportional gain scheduling by control error
// Scales slope-blended or fallback gain before the final sensitivity clamp.
// Uses trend-adjusted error; outside MAINTAIN, the small-error multiplier applies.
// ******************************************************************************
// Absolute control errors below this threshold use the small-error gain multiplier.
constexpr int SMALL_ERROR_WATTS = 20;
// While maintaining, errors at least SMALL_ERROR_WATTS but below this threshold use the medium-error multiplier.
constexpr int MEDIUM_ERROR_WATTS = 40;
// While maintaining, errors above this threshold use the large-error multiplier.
constexpr int LARGE_ERROR_WATTS = 60;
// Scale gain near the target or whenever the controller has not yet entered MAINTAIN mode.
constexpr double SMALL_ERROR_GAIN_MULTIPLIER = 0.90;
// Scale gain for medium errors while maintaining to soften corrections as power approaches the target.
constexpr double MEDIUM_ERROR_GAIN_MULTIPLIER = 0.75;
// Scale gain for large errors while maintaining to strengthen corrections far from the target.
constexpr double LARGE_ERROR_GAIN_MULTIPLIER = 1.25;
// ******************************************************************************

// ******************************************************************************
// Table seek and feedback wait power limits before safety exit
// Detects excessive overshoot during seeks or feedback acquisition to allow correction.
// These watt margins are independent of ERG sensitivity.
// ******************************************************************************
// End an increasing seek or feedback wait early when power exceeds target by more than this margin, allowing corrective control.
constexpr int TABLE_SEEK_INCREASE_OVERSHOOT_WATTS = ERG_MODE_PID_WINDOW;
// End a decreasing seek or feedback wait early when power falls below target by more than this margin, allowing corrective control.
constexpr int TABLE_SEEK_DECREASE_UNDERSHOOT_WATTS = ERG_MODE_PID_WINDOW;
// A sustained trend through the target permits early braking of a table move.
constexpr double ERG_TRANSIENT_BRAKING_SECONDS = 1.0;
constexpr double ERG_TRANSIENT_MIN_TREND_WPS   = 10.0;
constexpr int ERG_TRANSIENT_APPROACH_WATTS     = 60;
// ******************************************************************************

// ******************************************************************************
// Runtime table confidence thresholds
// Eligible accurate observations add one point; misses subtract the penalty.
// Separate grant/revoke thresholds prevent isolated misses from toggling trust.
// ******************************************************************************
// Maximum accumulated score; must fit the seven-bit score stored by TableConfidence.
constexpr uint8_t TABLE_CONFIDENCE_MAX_SCORE = 24;
// Grant trust when the score reaches this threshold, enabling otherwise-eligible trusted seeks.
constexpr uint8_t TABLE_CONFIDENCE_TRUST_SCORE = 16;
// Revoke existing trust when the score drops to this threshold or below.
constexpr uint8_t TABLE_CONFIDENCE_REVOKE_SCORE = 8;
// Points removed for each eligible inaccurate observation, with the score floored at zero.
constexpr uint8_t TABLE_CONFIDENCE_MISS_PENALTY = 1;
// ******************************************************************************

}  // namespace ErgControl
