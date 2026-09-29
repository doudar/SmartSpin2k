# ERG rider/bike simulator

Runs the actual SmartSpin2k ERG controller against a Python bike, with simulated
time driving **all** controller timestamps, motor travel, cadence changes, power
filtering, transport delay, and sensor reports. No device connection is needed.

## Run the supplied workout

### Run with native tests in VS Code

In the **pioarduino** extension's Project Tasks, choose the **native** environment's
**Test** task (`pio test -e native`). The native environment in `platformio.ini`
contains just this hook:

```ini
extra_scripts = post:scripts/erg_sim/native_hook.py
```

After a successful native build, the hook runs the Python simulator checks,
including the complete Random_Attacks workout at FTP 305 W. This also runs when
the binary is already up to date. A simulator failure fails the build/test command;
if it succeeds, the normal native Test action proceeds to the C++ Unity tests.
The hook prints the output folder, `test/output/erg_regression/`; open `response.png`,
`power_table.png`, and `report.md` there in VS Code's Explorer. It uses accelerated
time and requires no bike. Clean/IDE indexing do not run it. `pio test --without-building`
bypasses build hooks. Firmware builds are unaffected. pioarduino retains the `pio`
command and `.platformio` directory names.

This is a local hook: it skips GitHub Actions and requires no workflow changes.
Generic Python test discovery skips the optional simulator suite when its
dependencies are absent; the local native hook instead fails with setup guidance.

Install the dependencies once in **the same Python environment as pioarduino**.
For the default Windows IDE installation, from PowerShell at the repository root:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m pip install -r scripts/erg_sim/requirements.txt
```

`g++` must also be on VS Code's PATH (restart VS Code after changing PATH). A
missing dependency fails the native build with setup instructions.

### Run directly

Python 3.11+ and `g++` on PATH are required. From the repository root:

```sh
python -m pip install -r scripts/erg_sim/requirements.txt
python -m scripts.erg_sim run --ftp 305
python -B -m unittest discover -s test -p test_erg_simulator.py
```

The default is the supplied `workouts/Random_Attacks.zwo`, FTP **305 W**, seed 27,
the fixed interval cadence plan beside the workout, and the fitted bike from
`profiles/ride_20260927.json`. The whole 4,525-second workout (75:25) runs
in roughly 8 seconds on the development machine, plus compilation/plotting.

```sh
# Run against wall time; 20 means twenty simulated seconds per real second.
python -m scripts.erg_sim run --speed 1
python -m scripts.erg_sim run --speed 20

# Tune controller sensitivity or stress the assumed lag; use separate outputs.
python -m scripts.erg_sim run --sensitivity 3 --output test/output/erg_sensitivity3
python -m scripts.erg_sim run --lag-scale 2 --seed 28 --output test/output/erg_doublelag

# Optional original stochastic rider for less controlled stress tests.
python -m scripts.erg_sim run --rider-mode stochastic --cadence 90 --variation 4
```

`--speed 0` runs as fast as possible. Pacing changes only wall time, never physics
or firmware time. The default IO interval is 20 ms; `--tick-ms` can divide 1,000
between 1 and 100 ms. Both feedback and controller clocks keep running during
held sensor reports. Repeated equal watts still constitute fresh reports.

The runner rebuilds its native executable on every invocation so edits to
`src/ERG_Mode.cpp`, its settings/helpers, power-table learning, or motor dispatch
are actually tested. It does not build ESP32 firmware or alter controller code.

## Output and grading

CLI output goes to `test/output/erg_sim/`. The unittest always runs the complete
ZWO and writes `test/output/erg_regression/`:

- `response.png` / `response.svg`: dark workout chart with FTP-zone segment
  fills, cyan power, green cadence, dashed cadence callouts, separate W/RPM axes,
  and summary scores.
- `power_table.png` / `power_table.svg`: power versus motor-position curves,
  colored by cadence row, with table growth underneath. Dots show stored entries;
  lines join each row's entries without extrapolating outside its learned range.
  Missing entries remain missing in the exported data.
- `learned.ptab`: final controller RAM table exported in the firmware's binary
  format: LE int32 version/quality, one-byte homed flag, row-major int16 position
  and int8 reading weight. Positions retain `TABLE_DIVISOR` scaling. This simulator
  has no FTMS calibration trailer. The physical table comes from production
  learning, not from the simulator's known resistance curve. This exports the
  final snapshot even though LittleFS persistence is stubbed.
- `power_table.json` / `power_table.csv`: table dimensions, coordinates and raw
  entries, including empty cells. Reading weight 1 is inferred/low confidence;
  2+ is a supported entry. The graph is decoded from `learned.ptab`.
- `cadence_plan.json` / `cadence.csv`: resolved interval callouts and actual
  cadence. The plan fingerprint is included in the score metadata.
- `diagnostics.png`: power/target, signed error, and cadence in separate panels.
- `report.md` and `score.json`: quantitative grades and execution metadata.
- `trace.csv`: sensor-report samples, requested/effective target, physical and
  commanded positions, learned table cells, and seek state.
- `firmware.log`: real controller and learning diagnostics.

Grades include maximum absolute error, separate overshoot/undershoot, mean
absolute error, RMS error, bias, time-weighted 95th/99th percentiles, and strict
deviations **over 10, 20, 30, 50, and 100 W**. Each threshold reports:

- Number of sensor samples over the threshold (normally one per second).
- Number of distinct contiguous episodes.
- Total duration, percentage of scored time, and longest episode.

The report's headline grades and chart score cards allow **3 seconds to settle at
the start of each interval**, including workout start and ramp starts. Reports
at exactly interval start + 3 seconds count. The allowance follows workout
segment boundaries, so individual ramp watt updates do not restart it. Max,
MAE, percentiles and every threshold column use the same eligible reports.
Held durations stop at the next interval, excluded gaps split episodes, and
percentages use scored time. Coverage and excluded samples/seconds are explicit;
workouts with no eligible reports show unavailable metrics instead of zero error.

A second full-workout section retains every report, including transitions.
`score.json` keeps these unfiltered metrics at the original `score` keys and
adds the headline metrics under `score.after_interval_grace`. Existing regression
limits continue checking the unfiltered score. The power/error curves retain all
samples. This reporting allowance does not change the controller or simulation.

Requested workout watts are the reference even if firmware clamps the target. An additional
steady score covers constant segments after their first 15 seconds. For each
target jump of at least 20 W, the report measures time until power remains within
20 W for five seconds; `null` means it did not settle before the next segment.
Large instantaneous errors at target jumps are expected with delayed feedback.

The regression uses explicit broad budgets (max 300 W, p95 45 W, MAE 18 W and
threshold duration budgets). These detect deterioration; they are **not** a
claim that the current ERG tuning is optimal. Improvements remain passing. Use
multiple seeds and lag scales before selecting new tuning.

## The thin connection

The Python bike receives only **signed emitted motor steps**. The native bridge
receives only **power/cadence reports and normalized opposing motor load**, plus
the simulation clock and the workout's normal target commands. It never receives
the bike's resistance curve, true physical position, or predicted target position.
Additional positions/counters flow out for diagnostics, not control.

The native adapter compiles complete production `ERG_Mode.cpp`, production
`PowerTable::processPowerValue/newEntry`, `PowerBuffer`, `PTHelpers`, travel-limit
logic, `SS2K::moveStepper`, and `_findEndStop`/StallGuard baseline logic. Only the
peripherals, persistence, network services, and pulse generation are substituted.
Pulse generation uses 3,000 steps/s² acceleration, configured speed, and a 1 ms
integrator. The motor counter counts pulses even at a physical endstop, so the
firmware must detect load rather than read a hidden position sensor.

The standard workout starts homed with assumed bounds 0–24,482 steps and an empty
power table. Production learning runs throughout. Separate endstop tests execute
the production search in both directions; this is not a test of full `goHome`,
thermal protection, FTMS calibration, BLE delivery, or ESP32 scheduling.
The SG adapter maps dimensionless load to a synthetic SG result; it is not a
calibrated torque-in-Nm or TMC electrical model. Filesystem writes are stubbed.

The effective bike model is:

1. Motor pulses move the physical resistance position, with optional backlash
   and physical stops.
2. A monotonic position curve gives watts at 80 RPM; cadence scales it by a fitted
   exponent (1.761 for this ride). Workout target watts never enter this equation.
3. A first-order response (0.775 s) and transport delay (1.117 s) represent the
   combined bike/power-estimator lag. Reports arrive at 1 Hz.
4. Correlated stationary variation uses an 8.10 W robust scale and 1.51 s
   correlation time estimated from the training portion. It includes model error
   and rider effects; it is not an isolated power-meter noise measurement.

## Repeatable cadence scenario

The default `planned` rider uses `workouts/Random_Attacks.cadence.json`. All 40
expanded intervals have explicit cadence callouts. The original 22 intervals retain
their powers, durations, and callouts; two blocks add 810 seconds of small steps:

- Warmup gradually builds from 60 to 90 RPM.
- Three fast-pedaling drills target 110 RPM, with recovery callouts around 85–95 RPM.
- Tempo/recovery callouts are around 80–90 RPM; attacks target 95–105 RPM.
- Building intervals increase cadence gradually, usually by 5–10 RPM over several
  minutes. Cooldown falls from 85 to 60 RPM.
- Within a constant callout, smooth drift is only 2.4 RPM peak to peak over two
  minutes. Sag and gradual callout changes are added separately.
- Intervals with nonzero `sag_rpm` begin sagging after 45 seconds, lose the configured
  RPM over 45 seconds, then regain the target over 10 seconds. The current plan
  includes values up to 10 RPM; validation accepts 0–30 RPM. A 35-second hold follows
  before the next sag. These are explicit scenario assumptions, not new fitted
  physiological measurements.
- After the first hard attack, nine 45-second holds use a 200 W base at 90 RPM:
  200, 210, 200, 230, 200, 220, 200, 240, 200 W at FTP 305.
- Before cooldown, nine 45-second holds use a 300 W base at 85 RPM:
  300, 260, 300, 280, 300, 270, 300, 290, 300 W at FTP 305.
- These add sixteen transitions: four each of magnitude 10, 20, 30, and 40 W,
  including both directions. Cadence callouts stay constant within each block,
  with the existing smooth drift and no added sag, so cadence changes do not mask
  the small target changes. ZWO powers scale with FTP; the stated watt differences
  apply at FTP 305. Each block begins with a separate 45-second baseline hold.

The small-step expansion changes the scenario baseline. Compare controller
settings on the same expanded workout and cadence fingerprint; its lower overall
MAE cannot be treated as an improvement over the shorter original workout.
The standard transition grade uses a 20 W minimum jump and a +/-20 W window,
so it does not score the 10 W steps. For small-step tuning, also inspect first-ten-
second MAE and time within +/-10 W for five consecutive reports, including all
sixteen added steps. Keep the existing overall grading limits unchanged.

The September 28 independent ride supported retaining the current delayed power
response model. Its roughly one-second status logs do not expose fresh-sample
timestamps well enough to distinguish new equal readings from held readings or
identify transport jitter separately from power-estimator lag. No new delay,
dropout, backlash, or noise parameters were fitted from that ride. The observed
app reconnect/SIM-mode switch is outside the bike physics model. Use the existing
`--lag-scale` and `--seed` options for robustness comparisons.

Cadence transitions begin when the interval changes and are limited to 1.5 RPM/s
up and 2 RPM/s down, with a smooth arrival. Every callout, drift, and sag depends
only on workout time. **Actual ERG response cannot change this cadence trace**,
so controller comparisons receive identical rider inputs. Seeds still control
the bike's stationary power variation. The regression verifies cadence invariance
across ERG sensitivities and wall-clock speed multipliers.

Edit the sidecar to change callouts; its canonical fingerprint is saved with each
run. A newline-normalized workout hash and segment-duration checks prevent
silently applying these callouts to a different workout. Compare scores only
under matching cadence plans, FTP, bike parameters and seeds. Switching from the
old stochastic rider to this plan establishes a **new scenario baseline**, not
evidence that firmware improved.

Other workouts honor structured cadence recommendations, otherwise a modest
intensity-based cadence schedule is generated. Descriptive text is not executed.
`--rider-mode stochastic` retains the original fitted cadence distribution
(89 RPM median, 7.94 RPM variation), seeded wander, fatigue and load-driven droop.
`--cadence` and `--variation` apply only to that stochastic mode. Reference
validation continues using recorded cadence rather than either synthetic rider.

## Reference fitting and reproduction

```sh
python -m scripts.erg_sim fit --log "ride-log.txt" --fit "activity.fit"
python -m scripts.erg_sim validate
```

The importer reads status and FTMS targets from the supplied log and decodes FIT
records with `fitdecode`. It aligns the clocks using shared power/cadence. Both
files in this recording are approximately **1 Hz**: 3,899 log samples and 3,955
FIT samples. Alignment power correlation is 0.997. The committed derived CSV
contains relative seconds, power, cadence, positions and targets only; it excludes
GPS, HR, identity, and wall-clock timestamps. Original file hashes are in the
profile. The original FIT/log stay outside tracked source.

Fit parameters use only the first 65% chronologically; the last 35% remains
held out. The effective power response is fitted without using workout targets.
The held-out mean absolute prediction error is **11.21 W**, p95 **31.28 W**, RMS
**15.95 W**. An independently fitted zero-lag model has RMS **23.31 W**. `fit.png`
overlays observed and predicted power with the training boundary marked.

`validate` additionally closes the loop on the first uninterrupted recorded ERG
section (about 33.6 minutes), feeding its recorded cadence and reconstructed
targets into the simulator. All power is then generated by the bike, and all
motor decisions come from current production C++. `validation.json` scores
power reproduction and position difference; `comparison.png` overlays both.
This tests a different claim from replaying recorded positions through the plant.

The original firmware commit, saved power table, and complete settings snapshot
are unavailable, so exact historical commands cannot be guaranteed. One ride is
also insufficient to uniquely identify sensor delay versus flywheel dynamics,
backlash, temperature effects, or arbitrary rider behavior. The model's measured
training envelope is about 6,292–18,881 steps and 56–114 RPM; behavior beyond it
is extrapolation. An independent ride is the next useful validation, rather than
adjusting the model until the entire supplied recording appears perfect.

Supported inputs are FTP-relative `.zwo` blocks (`Warmup`, `Cooldown`, `Ramp`,
`SteadyState`, `IntervalsT`), absolute-watt `.erg` time/power points, or JSON:

```json
{"segments": [
  {"duration_s": 60, "watts": 155, "cadence_rpm": 90},
  {"duration_s": 30, "watts": 335},
  {"duration_s": 60, "watts": 170, "end_watts": 100}
]}
```

ZWO powers are converted to the nearest integer watt. ERG timestamps are minutes
and adjacent points interpolate linearly; duplicate timestamps encode steps.
Unsupported blocks such as free ride fail explicitly rather than inventing an
ERG target. The supplied workout is retained unchanged with its original author
metadata.
