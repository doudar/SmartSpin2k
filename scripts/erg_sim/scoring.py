"""Time-weighted grading at sensor-report cadence, independent of tick rate."""

import numpy as np

THRESHOLDS = (10, 20, 30, 50, 100)
INTERVAL_GRACE_S = 3.0


def percentile(values, weights, fraction):
    order = np.argsort(values)
    values, weights = values[order], weights[order]
    return float(values[min(len(values)-1, np.searchsorted(np.cumsum(weights), fraction*weights.sum()))])


def _error_summary(times, error, dt):
    """Zero weights exclude reports without joining episodes across the gaps."""
    eligible = dt > 0
    seconds = float(dt.sum())
    absolute = np.abs(error)
    contiguous = np.r_[False, np.isclose(times[1:], times[:-1]+dt[:-1], rtol=0, atol=1e-9)]
    thresholds = {}
    for threshold in THRESHOLDS:
        over = eligible & (absolute > threshold)  # Strictly over, not >=.
        episodes = int(np.sum(over & ~(np.r_[False, over[:-1]] & contiguous)))
        longest, current = 0.0, 0.0
        for hit, weight, connected in zip(over, dt, contiguous):
            current = (current if connected else 0)+weight if hit else 0
            longest = max(longest, current)
        thresholds[str(threshold)] = {"samples": int(over.sum()), "episodes": episodes,
                                      "seconds": float(dt[over].sum()), "percent_time": float(100*dt[over].sum()/seconds) if seconds else None,
                                      "longest_episode_s": longest}
    error, absolute, dt = error[eligible], absolute[eligible], dt[eligible]
    return {"samples": int(eligible.sum()), "scored_seconds": seconds,
            "max_absolute_error_w": float(absolute.max()) if seconds else None,
            "max_overshoot_w": float(max(0, error.max())) if seconds else None,
            "max_undershoot_w": float(max(0, -error.min())) if seconds else None,
            "p95_absolute_error_w": percentile(absolute, dt, .95) if seconds else None,
            "p99_absolute_error_w": percentile(absolute, dt, .99) if seconds else None,
            "mae_w": float(np.average(absolute, weights=dt)) if seconds else None,
            "rmse_w": float(np.sqrt(np.average(error**2, weights=dt))) if seconds else None,
            "bias_w": float(np.average(error, weights=dt)) if seconds else None, "deviations_over_w": thresholds}


def grade(trace, duration, workout=None, *, interval_grace_s=INTERVAL_GRACE_S):
    if len(trace) < 2 or duration <= trace[-1, 0]:
        raise ValueError("Grading needs increasing samples and the complete workout duration")
    if not np.isfinite(interval_grace_s) or interval_grace_s < 0:
        raise ValueError("Interval grace must be finite and nonnegative")
    dt = np.diff(np.r_[trace[:, 0], duration])
    if np.any(dt <= 0): raise ValueError("Trace times must increase")
    error = trace[:, 2]-trace[:, 1]
    absolute = np.abs(error)
    # Preserve the original unfiltered keys for regression budgets and comparisons.
    report = {"duration_s": duration, **_error_summary(trace[:, 0], error, dt),
              "definitions": {"error": "reported watts minus requested workout watts (not firmware-clamped target)",
                              "counts": "one sample per power report; episodes are contiguous runs strictly above the threshold",
                              "percentile": "time-weighted nearest-rank absolute error, with sample-and-hold between reports",
                              "coverage": "full workout, including warmup, ramps, and every transition"}}
    report["transitions"] = []
    if workout:
        start, previous = 0, None
        settled_mask = np.zeros(len(trace), dtype=bool)
        tracking_dt = np.zeros(len(trace))
        for segment in workout.segments:
            end = start+segment.duration_s
            # Use interval boundaries, not individual ramp target updates. A report
            # at exactly start+grace counts; reports acquired earlier stay excluded.
            eligible = (trace[:, 0] >= start+interval_grace_s) & (trace[:, 0] < end)
            tracking_dt[eligible] = np.minimum(dt[eligible], end-trace[eligible, 0])
            # Stable tracking is reported separately. Never remove transitions from full score.
            if segment.start_w == segment.end_w:
                settled_mask |= (trace[:, 0] >= start+15) & (trace[:, 0] < end)
            if previous is not None and abs(segment.start_w-previous) >= 20:
                mask = (trace[:, 0] >= start) & (trace[:, 0] < end)
                ix = np.flatnonzero(mask)
                settling = None
                held = 0.0
                for i in ix:
                    held = held+dt[i] if absolute[i] <= 20 else 0
                    if held >= 5:
                        settling = float(trace[i, 0]+dt[i]-held-start)
                        break
                report["transitions"].append({"time_s": start, "from_w": previous, "to_w": segment.start_w,
                                               "settling_20w_for_5s_s": settling,
                                               "peak_absolute_error_w": float(absolute[ix].max()) if len(ix) else None})
            previous, start = segment.end_w, end
        report["after_interval_grace"] = {
            "grace_s": interval_grace_s, **_error_summary(trace[:, 0], error, tracking_dt),
            "excluded_samples": int(np.count_nonzero(tracking_dt == 0)),
            "excluded_seconds": float(dt.sum()-tracking_dt.sum()),
            "coverage": "reports at least grace_s after each interval starts, including workout start and ramp starts",
            "timing": "held durations stop at the next interval; excluded gaps split episodes; percentages use scored seconds",
        }
        if settled_mask.any():
            report["steady_after_15s"] = {"seconds": float(dt[settled_mask].sum()),
                                          "mae_w": float(np.average(absolute[settled_mask], weights=dt[settled_mask])),
                                          "p95_absolute_error_w": percentile(absolute[settled_mask], dt[settled_mask], .95)}
    return report
