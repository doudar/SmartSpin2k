#!/usr/bin/env python3
"""Render concise Markdown after the complete offline evidence build succeeds."""
import csv
import json
import gzip
from collections import defaultdict
from pathlib import Path

ROOT=Path(__file__).resolve().parent

def main():
    build=json.loads((ROOT/'evidence_build_audit.json').read_text())
    if build['status']!='complete' or build['rows']!=9576:raise ValueError('All exact-source cohorts and phases are required')
    counts=list(csv.DictReader((ROOT/'aggregate_counts.csv').open()))
    changes=list(csv.DictReader((ROOT/'all_paired_phase_changes.csv').open()))
    phases=list(csv.DictReader((ROOT/'all_diagnosed_phase_metrics.csv').open()))
    budgets=json.loads(gzip.decompress((ROOT/'preserved_budget_checks.json.gz').read_bytes()))
    config=json.loads((ROOT/'build_config.json').read_text())
    tests=json.loads((ROOT/'test_identity.json').read_text())
    finalmeta=json.loads((ROOT/'final_review_metadata.json').read_text())
    if finalmeta['source_sha256']!=build['candidate_sha256']:raise ValueError('Review metadata source mismatch')
    signal_labels={'effective_plant':'Effective fitted output','delivered_sensor':'Delivered sensor'}
    def counter(cohort,signal,role,variant):
        return next(r for r in counts if (r['cohort'],r['signal'],r['role'],r['variant'],r['sprint_target_w'])==(cohort,signal,role,variant,'all'))
    def paired(cohort,signal,role,key):
        return ' → '.join(str(counter(cohort,signal,role,v)[key]) for v in ['baseline','candidate'])
    table=['| Cohort / signal | Efforts | Sprint goal | Sprint tails / nulls | Recovery goal | Recovery tails / nulls |',
           '|---|---:|---:|---:|---:|---:|']
    over=['| Cohort / signal | Sprint overshoot >10 W, baseline → candidate | Worst sprint overshoot, W |',
          '|---|---:|---:|']
    for cohort in ['short','long','cadence','timing']:
        for sig,label in signal_labels.items():
            n=counter(cohort,sig,'sprint','candidate')['attempts']
            table.append(f'| {cohort.title()} / {label} | {n} | {paired(cohort,sig,"sprint","goal_met")} | {paired(cohort,sig,"sprint","final_tail_found")} / {paired(cohort,sig,"sprint","null_final_tail")} | {paired(cohort,sig,"recovery","goal_met")} | {paired(cohort,sig,"recovery","final_tail_found")} / {paired(cohort,sig,"recovery","null_final_tail")} |')
            worst=' → '.join(f'{float(counter(cohort,sig,"sprint",v)["worst_overshoot_w"]):.1f}' for v in ['baseline','candidate'])
            over.append(f'| {cohort.title()} / {label} | {paired(cohort,sig,"sprint","overshoot_above_10w")} | {worst} |')
    target_table=['| Sprint target / signal | Efforts | Strict goal, baseline → candidate | Cross-case maximum peak, W | Matched goals gained / lost | Matched tails gained / lost |',
                  '|---|---:|---:|---:|---:|---:|']
    for target in (500,700,1000):
        for sig,label in signal_labels.items():
            rs={v:[r for r in phases if r['role']=='sprint' and float(r['target_w'])==target and r['signal']==sig and r['variant']==v] for v in ('baseline','candidate')}
            goal=' → '.join(str(sum(r['goal_met']=='True' for r in rs[v])) for v in ('baseline','candidate'))
            peak=' → '.join(f"{max(float(r['peak_w']) for r in rs[v]):.1f}" for v in ('baseline','candidate'))
            cs=[r for r in changes if r['role']=='sprint' and float(r['target_w'])==target and r['signal']==sig]
            gained=sum(r['goal_gained']=='True' for r in cs);lost=sum(r['goal_lost']=='True' for r in cs)
            tg=sum(r['baseline_final_tail_s']=='null' and r['candidate_final_tail_s']!='null' for r in cs);tl=sum(r['tail_lost']=='True' for r in cs)
            target_table.append(f'| {target} W / {label} | {len(rs["candidate"])} | {goal} | {peak} | {gained} / {lost} | {tg} / {tl} |')
    low=[b for b in budgets if b['kind']=='all_low_phase_budget_comparisons']
    assert len(low)==1764
    nonpause=[b for b in low if not b['pause']]
    ordinary=[b for b in nonpause if b['role']!='recovery']
    relative_fail=lambda values:sum(not b['checks']['relative_passed'] for b in values)
    absolute_fail=lambda values:sum(not b['checks']['absolute_passed'] for b in values)
    lost=[r for r in changes if r['goal_lost']=='True' and r['role'] in ('sprint','recovery')]
    lost_tail=[r for r in changes if r['tail_lost']=='True' and r['role'] in ('sprint','recovery')]
    lows=[r for r in changes if r['role']=='low_power' and r['zero_cadence_pause']=='False']
    worst=max(lows,key=lambda r:float(r['mae_delta_w']))
    wp=f'{worst["cohort"]}/{worst["case"]}/{worst["phase"]}/{worst["signal"]}'
    report=f'''# High-power ERG validation

**Draft hardware-validation candidate. The universal stable ±10 W in under 10 s goal is not met.** This report retains the gains, every null tail, case-level losses and unchanged budget failures. It supports review and bounded hardware testing, not a release or hardware-performance claim.

## Exact identity and scope

- Baseline: SmartSpin2k `615fce5599f01fbe9459f59e924e7bb65c81a22b`, pinned from develop once. Candidate: the production patch over that baseline, source SHA-256 `{build['candidate_sha256']}`. See [production file hashes](source_identity.json) and [separate test identity](test_identity.json).
- Simulator: executed local `ae48c3dd04ad7896ad752eccbd6040e446b9dd21`; published equivalent-tree `809a7c9980a812874e03fe4b935f55bd560412c1`, [diagnostics PR](https://github.com/doudar/smartspin2k-simulator/pull/1). Both trees are `362ec247af1067b3d6ea5972c4550380b9c44b70`, independently read back from GitHub. Package SHA-256: `{config['simulator_package_sha256']}`.
- Model profile SHA-256: `{config['profile_sha256']}`. Adapter SHA-256: `c0c4db673fe124753ca1379d8ab6d10576a973b6c519ed99b175afe7a5469d62`. Model, sensor schedules, motor/travel limits and budgets were not changed.
- **180 paired rides:** 90 short (500/700/1000 W × empty/full × sensitivities 3/5/10 × seeds 7/17/29/41/83); 18 paired 90-minute rides (seed 7, three efforts each); 36 cadence pairs (65/105 RPM sprint cues, seed 7); 36 timing pairs (two predeclared packet schedules, seed 7). This produces **216 paired sprint efforts and 216 paired recoveries**, with both signals scored independently.

The strict goal is entry **before 10 s** into the final uninterrupted ±10 W tail, lasting at least five seconds and continuing through phase end. An earlier dwell is insufficient. Null means no qualified tail in that finite phase, not zero. Every 20 ms sample is unsmoothed; original half-open phases are retained. Effective output is fitted bike/estimator output, **not independently measured mechanical truth**. Delivered sensor watts include sampling, latency, quantization and noise.

## Results across every cohort

All arrows are pinned develop baseline → candidate. “Tails / nulls” gives separate before/after counts; late qualified tails do not count as goals.

{chr(10).join(table)}

### Target-level peaks and matched gains/losses

Maximum peaks can come from different cases; their difference is not a matched-case change.

{chr(10).join(target_table)}

{chr(10).join(over)}

Across the paired sprint/recovery signal phases, **{len(lost)} strict goals and {len(lost_tail)} finite ±10 W final tails are lost versus develop**, even where aggregate counts improve. The frozen noisy/delayed reporting model can prevent an uninterrupted ±10 W sensor tail even when fitted effective output settles. This is a model-based explanation, not proof that noise alone caused every observed failure; raw delivered counts are preserved without smoothing. Every loss, delay and explicit null is retained in [paired changes](all_paired_phase_changes.csv). Overshoot counts at >0/>10/>20 W and all target-specific denominators are in [complete aggregates](aggregate_counts.csv). Recovery peaks include inherited high power at phase entry; they must not be interpreted as late recovery tracking alone.

## Low-power protection and retained costs

Original prefix-relative gates pass **90/90 short, 36/36 cadence and 36/36 timing**. Absolute/combined gates pass **45/90, 30/36 and 9/36**, respectively; the baseline already shares those absolute failures. Short pre-390 s traces match in **87/90** pairs and table histories in **84/90**. The two changed history families (six paired cases) remain explicit; output-prefix equality does not imply table equality. Cadence/timing prefixes and sprint-entry tables match in all 36 pairs each.

The long audit examines **all 49 ≤240 W phases per ride**, not only the three original probe windows. It retains all **1,764 paired phase/signal comparisons**, including labeled zero-cadence pauses, with **{relative_fail(low)} relative and {absolute_fail(low)} absolute failures** in the full set. Excluding pauses, **{relative_fail(nonpause)}/{len(nonpause)}** fail the unchanged relative budgets and **{absolute_fail(nonpause)}/{len(nonpause)}** fail absolute budgets; ordinary non-recovery phases account for **{relative_fail(ordinary)}/{len(ordinary)}** relative failures. These per-phase applications are descriptive checks, distinct from the original declared probe gate. The largest ordinary low-power MAE increase across cohorts is **{float(worst['mae_delta_w']):+.3f} W**, at `{wp}` ({float(worst['baseline_mae_w']):.3f} → {float(worst['candidate_mae_w']):.3f} W). [All failed budgets](preserved_budget_checks.json.gz) are retained.

{finalmeta['long_conclusion']}

The near-flat helper repairs weak extrapolation at its measured edge using an informative raw donor slope and the existing 20 stored-unit span threshold. Exact values, interpolation and no-donor fallback are preserved. This is a deterministic geometry repair, not new high-power calibration. Compared with Candidate 4, timing traces are unchanged, while cadence recovery retains a strict-goal loss (105 RPM/700 W/full/s5: 9.80 → 14.06 s), two other >6 s finite delays, and three delivered-sensor tail losses. The cited ±10 final-tail and last-10-s error values still improve on develop; those predecessor-relative losses remain real costs.

## Verification, plots and limits

- Reviewed host: **133 tests**, two unchanged expected CSC failures, no skips or unexpected failures. Native **87/87**, mandatory hook **19/19**, SDK policy **5/5**. The helper review also passed 24 public-controller methods, eight focused native methods and the fixed-source differential/sanitizer audit. Counts describe their separate suites and must not be added together.
- {finalmeta['build_status']}
- Full matrix and independent-audit pins: [evidence build audit](evidence_build_audit.json). Exact-source execution was checked, with an explicit short-run limitation: no historical execution-time binary/toolchain pin existed in its original manifest. Afterward source/generated-input/binary checks do not create that missing historical record.

[Final-tail outcomes](plots/final_tail_outcomes.png), [sprint distributions](plots/sprint_distributions.png), [recovery distributions](plots/recovery_distributions.png) and [every low-phase comparison](plots/low_power_tradeoffs.png) retain all cases. [Trace selection](example_selection.json) uses fixed metrics across all cohorts: median/largest overshoot gains, worst remaining fitted sprint/recovery MAE, worst delivered sprint MAE and largest ordinary low-phase MAE increase. Both signals are shown separately, with raw trace extrema, MAE and final tails checked against independently audited metrics. Selected traces: [median gain](plots/example_01.png), [largest gain](plots/example_02.png), [worst sprint MAE](plots/example_03.png), [worst recovery MAE](plots/example_04.png) and [largest ordinary low-phase cost](plots/example_05.png).

“Full table” is the archived earned **45-cell grid**, with 50 supported cells overall, not a complete 1000 W calibration. The table axis ends at 870 W. The fitted operating range is 6,292–18,881 motor steps and 56–114 RPM; higher-power extrapolation is not verified bike capability. Native CPS/CSC ingress and FTMS control do not establish Peloton UART averaging, BLE radio, ESP32 scheduling or physical motor/cable behavior. Real-bike benefit remains unverified.

## Reproduction and next hardware check

[Reproduction instructions](REPRODUCE.md) include the exact-source patch, reviewed test patch, original earned fixture, fixed helper dependencies, supported simulator command and SHA-256 manifest. The local `screen_frozen.py` wrapper is not presented as portable. No runner or threshold was changed to produce a pass.

Begin a matched baseline/candidate hardware comparison with the same **500 W/sensitivity-3 schedule**, saving the actual start table, boot/homing/build identity and raw target/cadence/power/motor timestamps. Attempt 700/1000 W only if the rider can safely sustain them. Measure external-meter and cable transport separately; keep stop/abort access available. These proposed checks do not authorize flashing or deployment.
'''
    (ROOT/'REPORT.md').write_text(report)
    print('Rendered complete report:',len(report.split()),'words')

if __name__=='__main__':main()
