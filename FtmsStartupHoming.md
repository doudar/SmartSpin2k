# FTMS startup recovery and position synchronization

## September 19 log findings

The 09:16 run failed after 49.7 seconds on the no-progress guard at resistance 4.
The 10:52 run crossed that wide level successfully, but failed after 212.9 seconds
while trying to confirm the final 30% map transition. The minimum took 61.3 s,
the maximum roughly 54 s, and the five-reference map phase roughly 98 s.
At the last reference, the stationary sensor jumped from 29 to 31 across a
20-step bracket. Rejecting that imperfect crossing discarded the entire run.

The completed references below are reconstructed bracket midpoints, not
independent physical measurements. Zero was -6836; maximum was approximately
17666, giving 24502 steps of virtual travel.

| Resistance transition | Raw position | Percentage of virtual travel |
| --- | ---: | ---: |
| 41 -> 40 | 2353 | 37.50% |
| 51 -> 50 | 4988 | 48.26% |
| 61 -> 60 | 7508 | 58.54% |
| 71 -> 70 | 10304 | 69.95% |

Their least-squares middle-range slope is 263.73 steps/level. The largest
residual is 98.9 steps, equivalent to 0.375 resistance levels or 0.404% of total
travel. This supports sparse middle-range interpolation for this run. It does
not prove the entire range is linear: the endpoint-derived slope is 245.02
steps/percent, about 7.6% lower, and level 4 is conspicuously wide. The measured
50 transition is 427 steps below the 50% travel position. A single 50% reference
would recover an offset but could not independently establish the middle slope.

The log also shows delayed reports: at stationary position 9491 the reading
changes from 77 to 70 to 68. Shortening every wait would accept stale movement
feedback. Reducing the number of probes removes more time without relying on
those early reports.

## Revised calibration

Full calibration retains the two virtual endpoint searches, then measures near
33, 50, and 67. These are stationary samples, not precisely located transitions.
The search accepts a reading within two levels of its aim and stores the actual
observed resistance and position. Adjacent readings can be averaged into a half
level. Most of the middle is interpolated between these three samples; there is
no scan through every resistance level.

Three observations retain a measured slope on each side of the middle. Compared
with a two-point format, the complete trailer is just four bytes larger; the
middle measurement can expose curvature instead of assuming it away. The FTM2
trailer is 32 bytes: magic/version (4), source/direction fingerprint (4), maximum
(4), three positions (12), three doubled-resistance values (3), reserved byte
(1), and checksum (4). This replaces the five-point, 40-byte FTM1 format.

Target movement shares `ResistanceControl::Controller` with normal resistance
mode. Endpoint approaches use its live incremental targets; map/startup moves
use the measured steps-per-level gain with a 6000-step move limit. Once the
stepper reaches its position target, fresh unchanged resistance for one second
confirms the sample. Monotonic adjacent changes restart confirmation; an actual
back-and-forth across adjacent levels can confirm their midpoint. Persistently
noisy feedback uses the latest two readings at five seconds rather than failing.
Only final endpoint transition probes retain their two-second acquisition guard
for repeatable zero estimation. A
previous confirmed observation is reused between adjacent map searches; do not
wait again before issuing the next movement. Every saved sample is logged with
actual resistance, coordinate, and percentage of travel.

Endpoint searches accept the midpoint of a completed bracket even when the
sensor skips the requested integer. They no longer make an extra final return
move and wait just to confirm that same bracket. Wider analog noise can be
averaged; a changing timestamp alone still does not prove that the sensor
responded to motor travel. Frozen readings, sustained lack of useful response,
missing reports, cancellation, and motor errors retain bounded stopping paths.
The overall endpoint deadline remains 120 seconds as a final travel safeguard.

## Persistence and startup

PTAB watts entries keep their existing version-6 layout. FTM1, missing,
truncated, corrupt, or mismatched metadata triggers a full FTMS calibration.
Legacy migration preserves watts; explicit full calibration retains the existing
watts-reset policy. Metadata-only tables can save immediately. Saves use a
temporary file and rename after all bytes are written. Reset removes both map
and watts. No BLE table-transfer format changes.

With matching metadata, startup samples stationary resistance and interpolates
within the measured support, normally approximately 33–67. Adjacent jitter uses
its averaged resistance instead of forcing another transition hunt. Outside
that middle, bounded fast moves aim toward 50 until feedback enters the map.
Startup aims for under twenty seconds but does not abort responsive feedback
just because it exceeds that target. The 120-second overall safety deadline
and no-response travel guard still bound recovery. Recovery rebases coordinates, clears the ride-time gear offset, and selects the
absolute starting-gear target: eight Shift Amounts above zero for Unlimited, or
the ratio offset of `max(1, gearCount / 3)` for bounded groupsets. Normal motor
control moves directly there after homing restores its safety policy, with the
usual travel clamps; no intermediate return to zero is needed.

## Ride-time correction and the 11:40 log

The next run recovered successfully in about 10.7 seconds, finishing at
R48/P11520. Shifting from gear 8 to 6 moved to P9100 and R38/39. After manual
removal and reseating, R32 persisted from 160.451 to 206.693 seconds while P9100
never changed. This was sufficient stationary evidence to correct the offset.

The old policy was too restrictive: offsets above three local deadbands were
rejected outright, correction was limited to 100 steps per minute, adjacent
38/39 jitter restarted confirmation, and R32 could fall outside the saved sample
support. The log does not include the saved map or rejection diagnostics, so it
cannot identify every gate that fired on this particular device.

The revised policy keeps the same 32-byte FTM2 metadata and needs no new
calibration. Within R30–70, estimates may extend at most five levels past a
sample using its nearest measured slope. Each extrapolated level adds half a
local resistance bin to uncertainty. Startup still requires measured support;
there is no ride-time extrapolation into the unreliable ends.

Correction requires ten seconds of position stable within five steps and at
least nine fresh reports. One adjacent resistance pair is allowed and its
sample-weighted mean is used. Larger changes restart confirmation. For offsets
above three deadbands, require thirty seconds and at least 25 reports instead
of rejecting the discrepancy. Missing/stale feedback, motion, pending movement,
homing, updates, external control, simulation, ERG table seeking, and motor
inhibits disqualify an observation.

When error exceeds the deadband, rebase to the estimated position in one update.
The deadband is one local resistance level plus 40 steps, increased for short
extrapolations. At most one applied correction is allowed per minute. Checks
that find no discrepancy do not consume that interval, and brief driver-lock
interruptions do not continually postpone it. Update position, target, and
relevant ERG/gear offsets together without issuing a motor command.

The log also shows a watts entry being averaged at P9100 after the manual change.
Pause collection while a position discrepancy awaits confirmation/cooldown,
clear its pending buffer, and resume after coordinates agree. Corrections still
advance `positionEpoch` to prevent mixing pre/post-correction samples. Already
learned entries are not retroactively changed.

A regression follows the logged timing and resistance sequence with a synthetic
map consistent with R48/P11520 and R38.5/P9100. Depending on the actual lower
sample position, it rebases within 10–30 seconds of stationary evidence, without
motor commands or changing gear 6. The real saved PTAB is absent from the log,
so the regression's resulting P7440 is illustrative, not a claimed exact target
for this bike. Periodic `FTMS sync check` logs now expose the guard state.

## Validation

Native tests exercise acceleration, delayed 1 Hz reports, noise, skipped levels,
wide level 4, stale/simulated reports, cancellation, unresponsive sensors,
metadata round trips, and startup/drift guard behavior. The log-shaped model
interpolates stationary observations from this run; it is not a replay of
physical motion or a guarantee of hardware repeatability.

| Synthetic scenario | Time |
| --- | ---: |
| Stationary startup within measured support | Under 5 s, no movement |
| Startup R1/R20/R80/R99 at 100 steps/level | 7 s |
| Startup R1/R99 at 300 steps/level | 12 s |
| Noisy R99 startup at 300 steps/level, 2.5 s report delay | 19 s |
| Three map observations, ideal 100 steps/level | 15 s |
| Three observations, this log's middle curve, 1.3/2.5 s delay | 22/26 s |
| Same curve with additional +/-2 level noise | 34.5 s |
| Minimum with an extra 1200/2400-step level 4 | 81/87 s |

Full first-time calibration remains longer than startup recovery. These search timings exclude BLE connection and the subsequent move to the
starting gear. They are simulations, not measurements on the user's bike.
Integration regressions cover actual persistence and homing orchestration,
failed writes/renames, migration, and correction without physical movement.

## Shared resistance control and September 19 14:50 log

The recorded calibration spans about 175 seconds: 64 seconds finding minimum,
80 finding maximum, and 30 sampling the map. Samples R32/P7484,
R48.5/P11622 and R67/P16161 show a middle interpolation residual of 47 steps.
These measurements support a sparse map but not a raw resistance-percent to
travel-percent identity. The existing three-point, 32-byte format is unchanged.

Exact resistance target equality now holds the current position. The shared
controller adds derivative braking based on fresh resistance reports. Crossings
from more than two levels below to more than two above (or vice versa) add
0.5 seconds to D, capped at 2.5 seconds. D only reduces movement approaching a
target; it never commands motion away from it. A new target or ten seconds of
controller inactivity resets D. A fresh unchanged reading sets velocity to zero;
held reports older than 1.5 seconds stop contributing derivative braking.
Normal resistance moves log changes in D. No persistent metadata is added.

The spindown opcode previously remained selected after success, bypassing local
gearing and allowing the recovered step position to become an incline term.
Successful spindown now returns to simulation with zero incline before selecting
the starting gear. All shipped groupsets are covered by integration regressions.

## September 20 full-calibration motor-enable failure

The 20:24 log recovers from R90 using saved metadata in about six seconds,
then moves to the correct starting-gear position (P6426/R25). A later full
calibration finds minimum after 58 seconds, then reports R1–2 while its counter
moves P1108 to P4143 toward the R90 target and trips the no-response guard.
R90 is the requested anchor, not the resistance reached at failure.

Homing inherited manual enable from cadence unless a thermal inhibit was active.
FastAccelStepper retains its previous automatic-disable countdown when manual
enable is selected; stationary time during a long calibration can expire it.
Normal maintenance continually re-enables outputs while pedaling, but is skipped
during homing. Subsequent manual-mode commands can advance the step counter with
outputs disabled. Homing now always selects automatic enable so each move renews
the timer and re-enables an expired output. Safety interlocks are still restored
on exit. This reproduces the counter-without-physical-motion failure in a test;
the log contains no EN-pin measurement, so confirmation on the bike is needed.
