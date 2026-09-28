"""Import observations, never instructions, from a device log and a FIT file."""

import csv
import re
from pathlib import Path

import numpy as np


def read_log(path):
    status, events = [], []
    for line in Path(path).read_text(encoding="utf-8-sig").splitlines():
        stamp = re.search(r"\[(\d+)\]\[[A-Z]\]", line)
        if not stamp:
            continue
        t = int(stamp[1]) / 1000
        sample = re.search(r"W=(-?\d+) C=(\d+) H=\d+ G=-?\d+ R=(-?\d+) P=(-?\d+)->(-?\d+)", line)
        if sample:
            w, c, r, p, target = map(int, sample.groups())
            status.append((t, w, c, p, target, r))
        target = re.search(r"ERG Mode Target: (\d+)", line)
        if target:
            events.append((t, int(target[1])))
        elif "Sim Mode Incline" in line:
            events.append((t, 0))
    if len(status) < 30 or not events:
        raise ValueError("Need at least 30 status samples and FTMS target events")
    samples = np.asarray(status, dtype=float)
    if np.any(np.diff(samples[:, 0]) <= 0):
        raise ValueError("Device log timestamps must increase (split logs at reboots)")
    return samples, np.asarray(events, dtype=float)


def read_fit(path):
    import fitdecode

    rows = []
    with fitdecode.FitReader(path) as reader:
        for frame in reader:
            if not isinstance(frame, fitdecode.FitDataMessage) or frame.name != "record":
                continue
            fields = {field.name: field.value for field in frame.fields}
            if any(fields.get(key) is None for key in ("timestamp", "power", "cadence")):
                continue
            rows.append((fields["timestamp"].timestamp(), fields["power"], fields["cadence"], fields.get("target_power") or 0))
    data = np.asarray(rows, dtype=float)
    if len(data) < 30:
        raise ValueError("FIT has fewer than 30 complete power/cadence records")
    data[:, 0] -= data[0, 0]  # Do not retain GPS, HR, identity, or wall-clock timestamps.
    if np.any(np.diff(data[:, 0]) <= 0):
        raise ValueError("FIT record timestamps must increase")
    return data


def align(log, fit):
    """Find constant clock offset from shared watts/cadence; refine to 50 ms."""
    base = log[0, 0]
    lt = log[:, 0] - base
    # At least 80% overlap; reject unrelated activity files below.
    def score(offset):
        ft = fit[:, 0] + offset
        valid = (ft >= lt[0]) & (ft <= lt[-1])
        if valid.sum() < 0.8 * min(len(log), len(fit)):
            return float("inf")
        idx = np.clip(np.searchsorted(lt, ft[valid], side="right") - 1, 0, len(log)-1)
        error = fit[valid, 1:3] - log[idx, 1:3]
        return float(np.mean(np.minimum(error[:, 0]**2, 10000)) + 16*np.mean(np.minimum(error[:, 1]**2, 400)))
    extent = max(60, int(abs(fit[-1, 0] - lt[-1])) + 120)
    coarse = min(range(-extent, extent+1), key=score)
    offset = min(np.arange(coarse-2, coarse+2.001, .05), key=score)
    ft = fit[:, 0] + offset
    valid = (ft >= 0) & (ft <= lt[-1])
    idx = np.clip(np.searchsorted(lt, ft[valid], side="right")-1, 0, len(log)-1)
    corr = float(np.corrcoef(fit[valid, 1], log[idx, 1])[0, 1])
    if not np.isfinite(corr) or corr < .85:
        raise ValueError(f"Log/FIT alignment unreliable: power correlation {corr:.3f}")
    return base + float(offset), {"fit_start_device_seconds": base+float(offset), "power_correlation": corr,
                                  "overlap_records": int(valid.sum()), "alignment_score": score(offset)}


FIELDS = ["time_s", "power_w", "cadence_rpm", "position_steps", "command_steps", "target_w", "fit_power_w", "fit_cadence_rpm", "fit_target_w"]


def import_reference(log_path, fit_path, output):
    log, events = read_log(log_path)
    fit = read_fit(fit_path)
    offset, alignment = align(log, fit)
    target_idx = np.searchsorted(events[:, 0], log[:, 0], side="right")-1
    targets = np.where(target_idx >= 0, events[np.maximum(target_idx, 0), 1], 0)
    fit_t = fit[:, 0] + offset
    fi = np.clip(np.searchsorted(fit_t, log[:, 0], side="right")-1, 0, len(fit)-1)
    fitted = fit[fi, 1:4].copy()
    fitted[(log[:, 0] < fit_t[0]) | (log[:, 0] > fit_t[-1])] = np.nan
    data = np.column_stack((log[:, 0]-log[0, 0], log[:, 1:5], targets, fitted))
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    with Path(output).open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(FIELDS)
        writer.writerows([[round(float(v), 3) for v in row] for row in data])
    alignment.update(log_samples=len(log), fit_samples=len(fit), duration_s=float(data[-1, 0]),
                     max_log_gap_s=float(np.diff(log[:, 0]).max()),
                     fit_median_interval_s=float(np.median(np.diff(fit[:, 0]))),
                     log_median_interval_s=float(np.median(np.diff(log[:, 0]))))
    return data, alignment, events - np.array([log[0, 0], 0])


def load_reference(path):
    data = np.genfromtxt(path, delimiter=",", skip_header=1)
    if data.ndim != 2 or data.shape[1] != len(FIELDS) or np.any(np.diff(data[:, 0]) <= 0):
        raise ValueError("Invalid reference CSV")
    return data


def first_erg_section(data):
    """Reconstruct the first continuous ERG window at status-log resolution."""
    from .workout import Workout, Segment
    active = np.flatnonzero(data[:, 5] > 0)
    if not len(active): raise ValueError("Reference contains no ERG target")
    first = int(active[0])
    last = next((i for i in range(first, len(data)) if data[i, 5] == 0), len(data)-1)
    section = data[first:last].copy()
    start = section[0, 0]; section[:, 0] -= start
    duration = float(data[last, 0]-start)
    changes = np.r_[0, np.flatnonzero(np.diff(section[:, 5]) != 0)+1, len(section)]
    segments = [Segment(float((section[b, 0] if b < len(section) else duration)-section[a, 0]), section[a, 5], section[a, 5])
                for a, b in zip(changes, changes[1:])]
    return section, Workout(segments)
