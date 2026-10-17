# Export guide

- `all_diagnosed_phase_metrics.csv`: 9,576 rows, one variant × signal × phase per case. Every short/cadence/timing ride has nine phases; every long ride has 52. The preparation interval is independently audited where it was absent from the original manifest's phase diagnostics.
- `all_paired_phase_changes.csv`: 4,788 baseline/candidate signal-phase pairs, including every ordinary phase and labeled zero-cadence pause.
- `aggregate_counts.csv`: sprint/recovery counts, both signals, each cohort and target. The `all` target group includes the target-specific groups; do not add both. Long denominators are 54 efforts (three per 18 rides), not 18 rides.
- `preserved_budget_checks.json.gz`: original unchanged prefix/probe comparisons plus all 1,764 long low-phase/signal comparisons. Failures remain failures. The latter are descriptive per-constant-phase applications of the frozen budgets; a single constant phase has no transition list, so its legacy settling field is not proof of stability.

`target_w` is the actual phase target. `sprint_target_w` identifies the high target of the case, including on low phases. `start_s` uses absolute ride time; first dwell and final tail values are seconds relative to phase entry. The table-state label `full` means the archived earned grid, not complete high-power calibration.

`peak_w` is the actual unsmoothed maximum and `minimum_w` is the actual unsmoothed minimum, independently sourced from the full raw-trace audits. They may be below or above the target, respectively. These are not reconstructed from clipped overshoot/undershoot. `overshoot_w` is separately max(0, peak − target).

`first_dwell_s` requires five uninterrupted in-band seconds. `final_tail_s` additionally continues through phase end. `goal_met` requires final ±10 W entry strictly before ten seconds and a final duration of at least five seconds. Every unavailable tail/dwell is the literal string `null` in CSV, never an empty cell or zero. JSON uses native null. No smoothing is applied.

Positive `mae_delta_w` is worse. A goal loss and tail loss are different: a late but finite tail can lose the goal while retaining a tail. `zero_cadence_pause=True` labels the scheduled stopped-rider phase; it is retained in data and budgets, excluded only from the explicitly labeled low-phase scatter plot.

All plots and before/after counts compare with the pinned develop baseline. Predecessor Candidate 4 tradeoffs are described separately. Cross-case maximum peaks may come from different cases and therefore are not matched-case changes. Exact CSV precision is retained; report tables round displayed values only.
