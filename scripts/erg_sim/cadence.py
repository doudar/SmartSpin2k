"""Versioned, time-only rider callouts for reproducible ERG comparisons."""

import hashlib
import json
import math
from pathlib import Path


class CadencePlan:
    def __init__(self, workout):
        source = getattr(workout, "source", None)
        sidecar = source.with_suffix(".cadence.json") if source else None
        if sidecar and sidecar.exists():
            plan = json.loads(sidecar.read_text(encoding="utf-8"))
            # Universal-newline text hashing survives Git's Windows CRLF checkout.
            if hashlib.sha256(source.read_text(encoding="utf-8").encode()).hexdigest() != plan["workout_text_sha256"]:
                raise ValueError("Cadence plan workout hash differs; review callouts for the changed workout")
            if len(plan["segments"]) != len(workout.segments):
                raise ValueError("Cadence plan must match every expanded workout segment")
        else:
            def target(watts):
                return max(75, min(105, 85+20*(watts/workout.ftp-.65)))
            plan = {"name": "structured-cues-or-intensity-v1", "version": 1, "drift_rpm": 1.2, "drift_period_s": 120,
                    "rise_rpm_per_s": 1.5, "fall_rpm_per_s": 2.0, "segments": []}
            for i, segment in enumerate(workout.segments):
                cue = segment.cadence_rpm
                plan["segments"].append({"label": f"Interval {i+1}", "duration_s": segment.duration_s,
                    "start_rpm": cue if cue is not None else target(segment.start_w),
                    "end_rpm": cue if cue is not None else target(segment.end_w),
                    "sag_rpm": 4 if min(segment.start_w, segment.end_w) >= workout.ftp*1.05 and segment.duration_s >= 90 else 0})
        if plan.get("version") != 1:
            raise ValueError("Unsupported cadence plan version")
        for key in ("drift_period_s", "rise_rpm_per_s", "fall_rpm_per_s"):
            if not math.isfinite(plan[key]) or plan[key] <= 0: raise ValueError(f"Invalid cadence plan {key}")
        if not math.isfinite(plan["drift_rpm"]) or not 0 <= plan["drift_rpm"] <= 2.5:
            raise ValueError("Gradual cadence drift must be within 0..2.5 RPM")
        for segment, interval in zip(workout.segments, plan["segments"]):
            if abs(segment.duration_s-interval["duration_s"]) > 1e-6:
                raise ValueError("Cadence-plan duration differs from workout")
            # Structured ZWO recommendations take precedence over generated defaults.
            if segment.cadence_rpm is not None:
                interval["start_rpm"] = interval["end_rpm"] = segment.cadence_rpm
            for key in ("start_rpm", "end_rpm"):
                if not math.isfinite(interval[key]) or not 0 <= interval[key] <= 180: raise ValueError(f"Invalid {key}")
            if not math.isfinite(interval["sag_rpm"]) or not 0 <= interval["sag_rpm"] <= 30:
                raise ValueError("Cadence sag must be within 0..30 RPM")
        self.data = plan
        self.sha256 = hashlib.sha256(json.dumps(plan, sort_keys=True).encode()).hexdigest()

    def at(self, now):
        elapsed = max(0, now)
        index = len(self.data["segments"])-1
        for i, interval in enumerate(self.data["segments"]):
            if elapsed < interval["duration_s"]:
                index = i; break
            if i < len(self.data["segments"])-1: elapsed -= interval["duration_s"]
        interval = self.data["segments"][index]
        fraction = min(1, elapsed/interval["duration_s"])
        cue = interval["start_rpm"]+(interval["end_rpm"]-interval["start_rpm"])*fraction
        if cue == 0: return 0.0, 0.0, index
        # Long stable stretches: smooth 2.4 RPM peak-to-peak variation, no OU jumps.
        drift = self.data["drift_rpm"]*math.sin(2*math.pi*elapsed/self.data["drift_period_s"])
        # After 45 seconds, sag over 45 s, notice it, recover in 10 s, hold 35 s.
        # This is a scenario assumption driven by interval time, never ERG error.
        phase = (elapsed-45) % 90
        sag = 0.0
        if elapsed >= 45:
            sag = interval["sag_rpm"]*(phase/45 if phase < 45 else max(0, 1-(phase-45)/10))
        return cue, max(0, cue+drift-sag), index


class PlannedRider:
    def __init__(self, workout):
        self.plan = CadencePlan(workout)
        self.cadence = self.plan.at(0)[0]

    def advance(self, now, dt):
        _, desired, _ = self.plan.at(now)
        difference = desired-self.cadence
        speed = self.plan.data["rise_rpm_per_s"] if difference >= 0 else self.plan.data["fall_rpm_per_s"]
        # Smooth arrival, but a bounded slope on large interval changes.
        increment = difference*(-math.expm1(-dt/2))
        self.cadence += max(-speed*dt, min(speed*dt, increment))
        return self.cadence
