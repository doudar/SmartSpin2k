"""Deterministic independent sensor acquisition and delivery schedules.

A component-test scheduler, not a replacement for BLE/DirCon. Samples are frozen
at acquisition; delivery never re-reads the plant. All random choices depend on
sensor identity/seed, never on firmware output or the number of polling calls.
"""
from dataclasses import dataclass, field, asdict
import heapq
import math
import random


@dataclass(frozen=True)
class SensorSchedule:
    period_s: float = 1.0
    latency_s: float = 0.0
    jitter_s: float = 0.0
    phase_s: float = 0.0
    dropouts: tuple = field(default_factory=tuple)

    def __post_init__(self):
        values = (self.period_s, self.latency_s, self.jitter_s, self.phase_s)
        if not all(math.isfinite(v) for v in values) or self.period_s <= 0 or min(values[1:]) < 0:
            raise ValueError('Sensor timing must be finite, positive period, nonnegative delay/jitter/phase')
        for start, end in self.dropouts:
            if not (math.isfinite(start) and math.isfinite(end) and 0 <= start < end):
                raise ValueError('Dropouts need finite, increasing nonnegative intervals')

    def to_dict(self):
        return asdict(self)


class SensorChannel:
    def __init__(self, schedule, seed):
        self.schedule = schedule
        self.random = random.Random(seed)
        self.next_sample = schedule.phase_s
        self.sequence = 0
        self.pending = []
        self.last_delivery = -1.0
        self.acquired = self.dropped = self.delivered = 0
        self.last_now = -1.0

    def update(self, now, value):
        if not math.isfinite(now) or now < self.last_now:
            raise ValueError('Sensor clock must be finite and monotonic')
        self.last_now = now
        cfg = self.schedule
        # Caller must resolve every acquisition, rather than invent historical
        # values from the latest plant state after a clock jump.
        if now >= self.next_sample+cfg.period_s+1e-9:
            raise ValueError('Tick skipped a sensor acquisition; use a finer simulation tick')
        if now+1e-9 >= self.next_sample:
            # Acquisition occurs on this actual plant tick; do not fabricate
            # an earlier historical value when a due time is quantized.
            acquired_at = now
            sample = value(acquired_at) if callable(value) else value
            self.next_sample = cfg.phase_s+(self.sequence+1)*cfg.period_s
            self.sequence += 1
            self.acquired += 1
            jitter = self.random.uniform(-cfg.jitter_s, cfg.jitter_s)
            delivery = max(now, acquired_at+cfg.latency_s+jitter, self.last_delivery)
            self.last_delivery = delivery  # preserve per-connection ordering
            if any(start <= acquired_at < end for start, end in cfg.dropouts):
                self.dropped += 1
            else:
                heapq.heappush(self.pending, (delivery, self.sequence, acquired_at, sample))
        result = []
        while self.pending and self.pending[0][0] <= now+1e-9:
            delivery, sequence, acquired_at, sample = heapq.heappop(self.pending)
            result.append((acquired_at, sample))
            self.delivered += 1
        return result
