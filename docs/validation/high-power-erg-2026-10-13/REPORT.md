# High-power ERG validation

**Draft hardware-validation candidate. The universal stable ±10 W in under 10 s goal is not met.** This report retains the gains, every null tail, case-level losses and unchanged budget failures. It supports review and bounded hardware testing, not a release or hardware-performance claim.

## Exact identity and scope

- Baseline: SmartSpin2k `615fce5599f01fbe9459f59e924e7bb65c81a22b`, pinned from develop once. Candidate: the production patch over that baseline, source SHA-256 `1c0f7bce6f69a353cdff73b85eb10a660b9f8f333f0f23589d9d0228b67d01e8`. See [production file hashes](source_identity.json) and [separate test identity](test_identity.json).
- Simulator: executed local `ae48c3dd04ad7896ad752eccbd6040e446b9dd21`; published equivalent-tree `809a7c9980a812874e03fe4b935f55bd560412c1`, [diagnostics PR](https://github.com/doudar/smartspin2k-simulator/pull/1). Both trees are `362ec247af1067b3d6ea5972c4550380b9c44b70`, independently read back from GitHub. Package SHA-256: `c84f685fe6c49b682ea1a61ec226b535bbb645a29d101c234bce146a8e0c12b7`.
- Model profile SHA-256: `e4650c2aa1f59f8282207f5ac2c27dd6187265d2603defc3606edaa8d032dac8`. Adapter SHA-256: `c0c4db673fe124753ca1379d8ab6d10576a973b6c519ed99b175afe7a5469d62`. Model, sensor schedules, motor/travel limits and budgets were not changed.
- **180 paired rides:** 90 short (500/700/1000 W × empty/full × sensitivities 3/5/10 × seeds 7/17/29/41/83); 18 paired 90-minute rides (seed 7, three efforts each); 36 cadence pairs (65/105 RPM sprint cues, seed 7); 36 timing pairs (two predeclared packet schedules, seed 7). This produces **216 paired sprint efforts and 216 paired recoveries**, with both signals scored independently.

The strict goal is entry **before 10 s** into the final uninterrupted ±10 W tail, lasting at least five seconds and continuing through phase end. An earlier dwell is insufficient. Null means no qualified tail in that finite phase, not zero. Every 20 ms sample is unsmoothed; original half-open phases are retained. Effective output is fitted bike/estimator output, **not independently measured mechanical truth**. Delivered sensor watts include sampling, latency, quantization and noise.

## Results across every cohort

All arrows are pinned develop baseline → candidate. “Tails / nulls” gives separate before/after counts; late qualified tails do not count as goals.

| Cohort / signal | Efforts | Sprint goal | Sprint tails / nulls | Recovery goal | Recovery tails / nulls |
|---|---:|---:|---:|---:|---:|
| Short / Effective fitted output | 90 | 6 → 16 | 35 → 69 / 55 → 21 | 31 → 43 | 63 → 69 / 27 → 21 |
| Short / Delivered sensor | 90 | 0 → 0 | 7 → 18 / 83 → 72 | 0 → 0 | 3 → 4 / 87 → 86 |
| Long / Effective fitted output | 54 | 1 → 5 | 26 → 41 / 28 → 13 | 17 → 26 | 46 → 48 / 8 → 6 |
| Long / Delivered sensor | 54 | 0 → 0 | 7 → 13 / 47 → 41 | 0 → 0 | 24 → 28 / 30 → 26 |
| Cadence / Effective fitted output | 36 | 0 → 0 | 5 → 7 / 31 → 29 | 1 → 2 | 32 → 34 / 4 → 2 |
| Cadence / Delivered sensor | 36 | 0 → 0 | 0 → 0 / 36 → 36 | 0 → 0 | 9 → 5 / 27 → 31 |
| Timing / Effective fitted output | 36 | 2 → 6 | 14 → 20 / 22 → 16 | 6 → 10 | 24 → 25 / 12 → 11 |
| Timing / Delivered sensor | 36 | 0 → 0 | 0 → 0 / 36 → 36 | 0 → 0 | 0 → 0 / 36 → 36 |

### Target-level peaks and matched gains/losses

Maximum peaks can come from different cases; their difference is not a matched-case change.

| Sprint target / signal | Efforts | Strict goal, baseline → candidate | Cross-case maximum peak, W | Matched goals gained / lost | Matched tails gained / lost |
|---|---:|---:|---:|---:|---:|
| 500 W / Effective fitted output | 72 | 3 → 12 | 802.8 → 656.6 | 9 / 0 | 9 / 0 |
| 500 W / Delivered sensor | 72 | 0 → 0 | 803.0 → 649.0 | 0 / 0 | 2 / 0 |
| 700 W / Effective fitted output | 72 | 6 → 15 | 978.0 → 982.4 | 12 / 3 | 11 / 1 |
| 700 W / Delivered sensor | 72 | 0 → 0 | 974.0 → 980.0 | 0 / 0 | 2 / 0 |
| 1000 W / Effective fitted output | 72 | 0 → 0 | 1287.9 → 1231.2 | 0 / 0 | 38 / 0 |
| 1000 W / Delivered sensor | 72 | 0 → 0 | 1301.0 → 1217.0 | 0 / 0 | 13 / 0 |

| Cohort / signal | Sprint overshoot >10 W, baseline → candidate | Worst sprint overshoot, W |
|---|---:|---:|
| Short / Effective fitted output | 44 → 35 | 302.8 → 282.4 |
| Short / Delivered sensor | 61 → 68 | 303.0 → 280.0 |
| Long / Effective fitted output | 21 → 17 | 236.4 → 56.4 |
| Long / Delivered sensor | 32 → 35 | 223.0 → 53.0 |
| Cadence / Effective fitted output | 21 → 23 | 287.9 → 231.2 |
| Cadence / Delivered sensor | 21 → 23 | 301.0 → 217.0 |
| Timing / Effective fitted output | 14 → 19 | 214.4 → 179.5 |
| Timing / Delivered sensor | 25 → 32 | 227.0 → 170.0 |

Across the paired sprint/recovery signal phases, **7 strict goals and 8 finite ±10 W final tails are lost versus develop**, even where aggregate counts improve. The frozen noisy/delayed reporting model can prevent an uninterrupted ±10 W sensor tail even when fitted effective output settles. This is a model-based explanation, not proof that noise alone caused every observed failure; raw delivered counts are preserved without smoothing. Every loss, delay and explicit null is retained in [paired changes](all_paired_phase_changes.csv). Overshoot counts at >0/>10/>20 W and all target-specific denominators are in [complete aggregates](aggregate_counts.csv). Recovery peaks include inherited high power at phase entry; they must not be interpreted as late recovery tracking alone.

## Low-power protection and retained costs

Original prefix-relative gates pass **90/90 short, 36/36 cadence and 36/36 timing**. Absolute/combined gates pass **45/90, 30/36 and 9/36**, respectively; the baseline already shares those absolute failures. Short pre-390 s traces match in **87/90** pairs and table histories in **84/90**. The two changed history families (six paired cases) remain explicit; output-prefix equality does not imply table equality. Cadence/timing prefixes and sprint-entry tables match in all 36 pairs each.

The long audit examines **all 49 ≤240 W phases per ride**, not only the three original probe windows. It retains all **1,764 paired phase/signal comparisons**, including labeled zero-cadence pauses, with **12 relative and 355 absolute failures** in the full set. Excluding pauses, **12/1728** fail the unchanged relative budgets and **319/1728** fail absolute budgets; ordinary non-recovery phases account for **0/1620** relative failures. These per-phase applications are descriptive checks, distinct from the original declared probe gate. The largest ordinary low-power MAE increase across cohorts is **+1.823 W**, at `long/extended_1000w_empty_sensitivity3-7/phase_28/effective_plant` (6.028 → 7.851 W). [All failed budgets](preserved_budget_checks.json.gz) are retained.

Original long probe gates pass 15/18 for both relative and absolute policy; all three full-table sensitivity-10 cases retain failures. All 18 candidate long traces match Candidate 4 byte-for-byte, so this profile shows preservation rather than exercising the new near-flat repair. Against develop, 12 low-phase strict goals are gained and one is lost; ordinary-phase final-tail delays still exist despite zero ordinary relative-budget failures. The unchanged disk guard stopped after 16 pairs; an explicitly mapped copied baseline was retained, and only the two missing candidate rides resumed with the original binaries. Completed rides and builds were not rerun.

The near-flat helper repairs weak extrapolation at its measured edge using an informative raw donor slope and the existing 20 stored-unit span threshold. Exact values, interpolation and no-donor fallback are preserved. This is a deterministic geometry repair, not new high-power calibration. Compared with Candidate 4, timing traces are unchanged, while cadence recovery retains a strict-goal loss (105 RPM/700 W/full/s5: 9.80 → 14.06 s), two other >6 s finite delays, and three delivered-sensor tail losses. The cited ±10 final-tail and last-10-s error values still improve on develop; those predecessor-relative losses remain real costs.

## Verification, plots and limits

- Reviewed host: **133 tests**, two unchanged expected CSC failures, no skips or unexpected failures. Native **87/87**, mandatory hook **19/19**, SDK policy **5/5**. The helper review also passed 24 public-controller methods, eight focused native methods and the fixed-source differential/sanitizer audit. Counts describe their separate suites and must not be added together.
- Exact-source ESP32 and ESP32-S3 release compilations passed (109.12 s and 115.32 s). RAM/flash use: 66,916/1,339,683 bytes for ESP32; 62,064/1,443,607 bytes for S3. [Build/toolchain/config/log/image identities](firmware_build_identity.json) include verified archive-member hashes. Nonfatal existing deprecation and Git warnings remain; optional freertos-gdb setup did not complete, so debugger setup is unverified. No flashing, deployment or binary publication occurred.
- Full matrix and independent-audit pins: [evidence build audit](evidence_build_audit.json). Exact-source execution was checked, with an explicit short-run limitation: no historical execution-time binary/toolchain pin existed in its original manifest. Afterward source/generated-input/binary checks do not create that missing historical record.

[Final-tail outcomes](plots/final_tail_outcomes.png), [sprint distributions](plots/sprint_distributions.png), [recovery distributions](plots/recovery_distributions.png) and [every low-phase comparison](plots/low_power_tradeoffs.png) retain all cases. [Trace selection](example_selection.json) uses fixed metrics across all cohorts: median/largest overshoot gains, worst remaining fitted sprint/recovery MAE, worst delivered sprint MAE and largest ordinary low-phase MAE increase. Both signals are shown separately, with raw trace extrema, MAE and final tails checked against independently audited metrics. Selected traces: [median gain](plots/example_01.png), [largest gain](plots/example_02.png), [worst sprint MAE](plots/example_03.png), [worst recovery MAE](plots/example_04.png) and [largest ordinary low-phase cost](plots/example_05.png).

“Full table” is the archived earned **45-cell grid**, with 50 supported cells overall, not a complete 1000 W calibration. The table axis ends at 870 W. The fitted operating range is 6,292–18,881 motor steps and 56–114 RPM; higher-power extrapolation is not verified bike capability. Native CPS/CSC ingress and FTMS control do not establish Peloton UART averaging, BLE radio, ESP32 scheduling or physical motor/cable behavior. Real-bike benefit remains unverified.

## Reproduction and next hardware check

[Reproduction instructions](REPRODUCE.md) include the exact-source patch, reviewed test patch, original earned fixture, fixed helper dependencies, supported simulator command and SHA-256 manifest. The local `screen_frozen.py` wrapper is not presented as portable. No runner or threshold was changed to produce a pass.

Begin a matched baseline/candidate hardware comparison with the same **500 W/sensitivity-3 schedule**, saving the actual start table, boot/homing/build identity and raw target/cadence/power/motor timestamps. Attempt 700/1000 W only if the rider can safely sustain them. Measure external-meter and cable transport separately; keep stop/abort access available. These proposed checks do not authorize flashing or deployment.
