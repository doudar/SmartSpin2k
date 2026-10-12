"""Ride-domain metrics and fixed acceptance policy, independent of controller code.

These are engineering regression budgets, not calibrated hardware accuracy claims.
Power must be sampled on an independent clock, including sensor outage windows.
"""
import math
import numpy as np


def summarize(trace, duration, workout, *, band_w=20, dwell_s=5):
    data = np.asarray(trace, dtype=float)
    if data.ndim != 2 or data.shape[1] < 3 or len(data) < 2 or not np.isfinite(data[:, :3]).all():
        raise ValueError('Need finite time, requested target and physical power samples')
    times, targets, power = data[:, 0], data[:, 1], data[:, 2]
    if times[0] != 0 or not math.isfinite(duration) or duration <= times[-1]:
        raise ValueError('Trace must cover the full ride starting at zero')
    dt = np.diff(np.r_[times, duration])
    if np.any(dt <= 0) or np.max(dt) > 1.01:
        raise ValueError('Independent performance sample clock must be monotonic with no gaps over one second')
    error = power-targets
    absolute = np.abs(error)
    order = np.argsort(absolute)
    p95 = absolute[order[min(len(data)-1, np.searchsorted(np.cumsum(dt[order]), .95*duration))]]
    transitions = []
    start, previous = 0., None
    steady_error, steady_weights, variations = [], [], []
    for segment in workout.segments:
        end = start+segment.duration_s
        indices = np.flatnonzero((times >= start) & (times < end))
        constant = segment.start_w == segment.end_w
        if constant and previous is not None and segment.start_w != previous:
            settling, held = None, 0.
            for i in indices:
                weight = min(dt[i], end-times[i])
                held = held+weight if absolute[i] <= band_w else 0.
                if held >= dwell_s-1e-9:
                    settling = max(0., float(times[i]+weight-held-start))
                    break
            transitions.append({'time_s': start, 'delta_w': segment.start_w-previous,
                                'settling_s': settling,
                                'overshoot_w': float(max(0., np.max(error[indices]))) if len(indices) else None,
                                'undershoot_w': float(max(0., -np.min(error[indices]))) if len(indices) else None})
        if constant:
            stable = np.flatnonzero((times >= start+15) & (times < end))
            if len(stable):
                steady_error.extend(error[stable]); steady_weights.extend(np.minimum(dt[stable], end-times[stable]))
                # One-second power differences suppress the integrator tick frequency.
                grid = np.arange(start+15, end, 1.)
                sampled = np.interp(grid, times, power)
                if len(sampled) > 1: variations.extend(np.abs(np.diff(sampled)))
        start, previous = end, segment.end_w
    if not math.isclose(start, duration): raise ValueError('Workout and trace duration disagree')
    return {'mae_w': float(np.average(absolute, weights=dt)), 'p95_error_w': float(p95),
            'overshoot_w': float(max(0., error.max())), 'undershoot_w': float(max(0., -error.min())),
            'outside_band_percent': float(100*dt[absolute > band_w].sum()/duration),
            'steady_p95_error_w': float(np.quantile(np.abs(steady_error), .95)) if steady_error else None,
            'steady_rmse_w': float(np.sqrt(np.average(np.square(steady_error), weights=steady_weights))) if steady_error else None,
            'steady_variation_w_per_s': float(np.mean(variations)) if variations else None,
            'unsettled_steps': sum(row['settling_s'] is None for row in transitions),
            'worst_settling_s': max((row['settling_s'] for row in transitions if row['settling_s'] is not None), default=None),
            'transitions': transitions, 'band_w': band_w, 'dwell_s': dwell_s, 'scored_seconds': duration}


# Fixed before observing either revision. Relative allowance avoids failing on
# tiny numerical/noise changes; absolute bands still reject a poor baseline.
DEFAULT_BUDGET = {'mae_w': 25., 'p95_error_w': 60., 'overshoot_w': 150.,
                  'steady_rmse_w': 35., 'unsettled_steps': 0.}
RELATIVE_ALLOWANCE = {'mae_w': (2., .10), 'p95_error_w': (5., .10),
                      'steady_rmse_w': (2., .10), 'steady_variation_w_per_s': (1., .15),
                      'unsettled_steps': (0., 0.), 'worst_settling_s': (2., .15)}


def compare(candidate, baseline, budget=None):
    failures, changes = [], {}
    for metric, ceiling in (DEFAULT_BUDGET if budget is None else budget).items():
        value = candidate.get(metric)
        if value is None or not math.isfinite(value) or value > ceiling:
            failures.append(f'{metric}: {value} exceeds absolute budget {ceiling}')
    for metric, (floor, fraction) in RELATIVE_ALLOWANCE.items():
        new, old = candidate.get(metric), baseline.get(metric)
        if old is None or new is None:
            if old is not None and new is None: failures.append(f'{metric}: candidate has no measurable result')
            continue
        if not math.isfinite(new) or not math.isfinite(old):
            failures.append(f'{metric}: nonfinite result'); continue
        delta, allowance = new-old, max(floor, abs(old)*fraction)
        changes[metric] = {'baseline': old, 'candidate': new, 'delta': delta, 'allowance': allowance,
                           'direction': 'worse' if delta > 0 else 'better' if delta < 0 else 'same'}
        if delta > allowance+1e-9: failures.append(f'{metric}: regression {delta:.3f} exceeds allowance {allowance:.3f}')
    return {'passed': not failures, 'absolute_passed': not any('absolute budget' in f for f in failures),
            'relative_passed': not any('absolute budget' not in f for f in failures), 'failures': failures, 'changes': changes}
