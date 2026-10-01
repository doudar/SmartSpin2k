"""ERG input: absolute-watt JSON/ERG or FTP-relative Zwift ZWO."""

from dataclasses import dataclass
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET


@dataclass
class Segment:
    duration_s: float
    start_w: float
    end_w: float
    cadence_rpm: float | None = None


class Workout:
    def __init__(self, segments):
        self.segments = segments
        self.ftp = 305
        self.source = None
        if not segments or any(not all(math.isfinite(v) for v in (s.duration_s, s.start_w, s.end_w)) or
                               s.duration_s <= 0 or min(s.start_w, s.end_w) < 0 or
                               (s.cadence_rpm is not None and (not math.isfinite(s.cadence_rpm) or not 0 <= s.cadence_rpm <= 200)) for s in segments):
            raise ValueError("Workout requires positive durations and finite nonnegative watts/cadence")
        self.duration = sum(s.duration_s for s in segments)

    def at(self, now):
        for s in self.segments:
            if now < s.duration_s:
                return round(s.start_w+(s.end_w-s.start_w)*max(0, now)/s.duration_s), s.cadence_rpm
            now -= s.duration_s
        return round(self.segments[-1].end_w), self.segments[-1].cadence_rpm

    @classmethod
    def load(cls, path, ftp=250):
        workout = cls._load(path, ftp)
        workout.ftp = ftp
        workout.source = Path(path)
        return workout

    @classmethod
    def _load(cls, path, ftp):
        path = Path(path)
        if not math.isfinite(ftp) or ftp <= 0:
            raise ValueError("FTP must be positive")
        if path.suffix.lower() == ".json":
            data = json.loads(path.read_text(encoding="utf-8"))
            return cls([Segment(float(s["duration_s"]), float(s["watts"]), float(s.get("end_watts", s["watts"])), s.get("cadence_rpm")) for s in data["segments"]])
        if path.suffix.lower() == ".erg":
            points, active = [], False
            for line in path.read_text(encoding="utf-8-sig").splitlines():
                text = line.strip()
                if text.upper() == "[COURSE DATA]": active = True; continue
                if text.upper() == "[END COURSE DATA]": break
                if active and text and not text.startswith((";", "#")):
                    t, w = map(float, text.split()[:2]); points.append((t*60, w))
            if len(points) < 2 or points[0][0] != 0 or any(b[0] < a[0] for a, b in zip(points, points[1:])):
                raise ValueError("ERG needs ordered minute/watt points beginning at zero")
            return cls([Segment(b[0]-a[0], a[1], b[1]) for a, b in zip(points, points[1:]) if b[0] > a[0]])
        if path.suffix.lower() != ".zwo":
            raise ValueError("Supported workouts: .json, .erg, .zwo")
        segments = []
        root = ET.parse(path).getroot().find("workout")
        if root is None:
            raise ValueError("ZWO has no workout element")
        for node in root:
            a = node.attrib
            cue = float(a["Cadence"]) if "Cadence" in a else None
            if node.tag == "SteadyState":
                segments.append(Segment(float(a["Duration"]), ftp*float(a["Power"]), ftp*float(a["Power"]), cue))
            elif node.tag in ("Warmup", "Cooldown", "Ramp"):
                lo, hi = float(a["PowerLow"])*ftp, float(a["PowerHigh"])*ftp
                start, end = (max(lo, hi), min(lo, hi)) if node.tag == "Cooldown" else (lo, hi)
                segments.append(Segment(float(a["Duration"]), start, end, cue))
            elif node.tag == "IntervalsT":
                for _ in range(int(a["Repeat"])):
                    segments.extend([Segment(float(a["OnDuration"]), ftp*float(a["OnPower"]), ftp*float(a["OnPower"]), cue),
                                     Segment(float(a["OffDuration"]), ftp*float(a["OffPower"]), ftp*float(a["OffPower"]),
                                             float(a["CadenceResting"]) if "CadenceResting" in a else cue)])
            elif node.tag != "textevent":
                raise ValueError(f"Unsupported ZWO block {node.tag}; convert it to explicit ERG segments")
        return cls(segments)
