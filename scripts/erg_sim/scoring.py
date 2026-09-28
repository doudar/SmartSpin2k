"""Time-weighted grading at sensor-report cadence, independent of tick rate."""

import numpy as np

THRESHOLDS = (10, 20, 30, 50, 100)


def percentile(values, weights, fraction):
    order = np.argsort(values)
    values, weights = values[order], weights[order]
    return float(values[min(len(values)-1, np.searchsorted(np.cumsum(weights), fraction*weights.sum()))])


def grade(trace, duration, workout=None):
    if len(trace) < 2 or duration <= trace[-1, 0]:
        raise ValueError("Grading needs increasing samples and the complete workout duration")
    dt = np.diff(np.r_[trace[:, 0], duration])
    if np.any(dt <= 0): raise ValueError("Trace times must increase")
    error = trace[:, 2]-trace[:, 1]
    absolute = np.abs(error)
    thresholds = {}
    for threshold in THRESHOLDS:
        over = absolute > threshold  # Strictly over, not >=.
        episodes = int(np.sum(over & ~np.r_[False, over[:-1]]))
        longest, current = 0.0, 0.0
        for hit, seconds in zip(over, dt):
            current = current+seconds if hit else 0
            longest = max(longest, current)
        thresholds[str(threshold)] = {"samples": int(over.sum()), "episodes": episodes,
                                      "seconds": float(dt[over].sum()), "percent_time": float(100*dt[over].sum()/dt.sum()),
                                      "longest_episode_s": longest}
    report = {"duration_s": duration, "samples": len(trace), "max_absolute_error_w": float(absolute.max()),
              "max_overshoot_w": float(max(0, error.max())), "max_undershoot_w": float(max(0, -error.min())),
              "p95_absolute_error_w": percentile(absolute, dt, .95), "p99_absolute_error_w": percentile(absolute, dt, .99),
              "mae_w": float(np.average(absolute, weights=dt)), "rmse_w": float(np.sqrt(np.average(error**2, weights=dt))),
              "bias_w": float(np.average(error, weights=dt)), "deviations_over_w": thresholds,
              "definitions": {"error": "reported watts minus requested workout watts (not firmware-clamped target)",
                              "counts": "one sample per power report; episodes are contiguous runs strictly above the threshold",
                              "percentile": "time-weighted nearest-rank absolute error, with sample-and-hold between reports",
                              "coverage": "full workout, including warmup, ramps, and every transition"}}
    report["transitions"] = []
    if workout:
        start, previous = 0, None
        settled_mask = np.zeros(len(trace), dtype=bool)
        for segment in workout.segments:
            end = start+segment.duration_s
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
        if settled_mask.any():
            report["steady_after_15s"] = {"seconds": float(dt[settled_mask].sum()),
                                          "mae_w": float(np.average(absolute[settled_mask], weights=dt[settled_mask])),
                                          "p95_absolute_error_w": percentile(absolute[settled_mask], dt[settled_mask], .95)}
    return report
