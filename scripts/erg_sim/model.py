"""Bike physics and rider. No workout watt target enters the bike model."""

from collections import deque
from dataclasses import dataclass, field
import math
import random


@dataclass
class BikeConfig:
    position_knots: list = field(default_factory=lambda: [0, 6000, 8000, 10000, 12000, 14000, 16000, 20000, 24482])
    watts_at_80rpm: list = field(default_factory=lambda: [0, 10, 35, 85, 160, 260, 390, 700, 1100])
    cadence_exponent: float = 1.5
    lag_s: float = 1.5
    delay_s: float = 1.5
    sample_period_s: float = 1.0
    noise_w: float = 4.0
    noise_correlation_s: float = 0.0
    min_position: float = 0
    max_position: float = 24482
    backlash_steps: float = 0

    def __post_init__(self):
        if len(self.position_knots) != len(self.watts_at_80rpm) or len(self.position_knots) < 2:
            raise ValueError("Bike curve needs matching position/watt knots")
        if any(b <= a for a, b in zip(self.position_knots, self.position_knots[1:])):
            raise ValueError("Position knots must increase")
        if any(b < a for a, b in zip(self.watts_at_80rpm, self.watts_at_80rpm[1:])) or min(self.watts_at_80rpm) < 0:
            raise ValueError("Bike watt curve must be nonnegative and monotonic")
        values = [*self.position_knots, *self.watts_at_80rpm, self.cadence_exponent, self.lag_s, self.delay_s,
                  self.sample_period_s, self.noise_w, self.noise_correlation_s, self.min_position, self.max_position, self.backlash_steps]
        if not all(math.isfinite(v) for v in values):
            raise ValueError("Bike parameters must be finite")
        if min(self.lag_s, self.delay_s, self.noise_w, self.noise_correlation_s, self.backlash_steps) < 0 or self.sample_period_s <= 0 or self.cadence_exponent <= 0:
            raise ValueError("Invalid lag, delay, noise, cadence exponent, or sample period")
        if self.min_position >= self.max_position:
            raise ValueError("Invalid physical travel")


def interpolate(x, xs, ys):
    import bisect
    i = max(0, min(len(xs)-2, bisect.bisect_right(xs, x)-1))
    return ys[i] + (ys[i+1]-ys[i]) * (x-xs[i])/(xs[i+1]-xs[i])


def equilibrium(config, position, cadence):
    watts = max(0, interpolate(position, config.position_knots, config.watts_at_80rpm))
    return watts * (max(0, cadence)/80)**config.cadence_exponent


class Bike:
    """Consumes signed motor pulses; returns sample-and-held watts/cadence/load.

    Lag is an effective flywheel/power-estimator pole followed by transport
    delay. A ride alone cannot uniquely separate those physical sources.
    Position and resistance are intentionally not sent back to the controller.
    """
    def __init__(self, config, position, cadence=80, seed=1, initial_power=None):
        self.config = config
        self.position = max(config.min_position, min(config.max_position, position))
        self.filtered = equilibrium(config, self.position, cadence) if initial_power is None else initial_power
        self.history = deque([(0.0, self.filtered, cadence)])
        self.next_report = config.sample_period_s
        self.power, self.cadence = round(self.filtered), round(cadence)
        self.load = 0.0
        self.direction = 0
        self.slack = 0.0
        self.random = random.Random(seed)
        self.noise = 0.0
        self.last_report = 0.0

    def advance(self, now, dt, pulses, cadence):
        cfg = self.config
        direction = int(pulses > 0) - int(pulses < 0)
        if direction and direction != self.direction:
            if self.direction:
                self.slack = cfg.backlash_steps
            self.direction = direction
        takeup = min(abs(pulses), self.slack)
        self.slack -= takeup
        requested = self.position + direction*(abs(pulses)-takeup)
        self.position = max(cfg.min_position, min(cfg.max_position, requested))
        # Dimensionless opposing motor load. SG adapter is synthetic, not Nm.
        self.load = 1.0 if requested != self.position else 0.0
        physical = equilibrium(cfg, self.position, cadence)
        alpha = 1.0 if cfg.lag_s == 0 else -math.expm1(-dt/cfg.lag_s)
        self.filtered += alpha*(physical-self.filtered)
        self.history.append((now, self.filtered, cadence))
        while len(self.history) > 2 and self.history[1][0] <= now-cfg.delay_s:
            self.history.popleft()
        delayed_t = now-cfg.delay_s
        a, b = self.history[0], self.history[min(1, len(self.history)-1)]
        f = max(0, min(1, (delayed_t-a[0])/(b[0]-a[0]))) if b[0] > a[0] else 0
        delayed = a[1] + f*(b[1]-a[1])
        fresh = now+1e-9 >= self.next_report
        if fresh:
            self.next_report = (math.floor((now+1e-9)/cfg.sample_period_s)+1)*cfg.sample_period_s
            rho = math.exp(-(now-self.last_report)/cfg.noise_correlation_s) if cfg.noise_correlation_s > 0 else 0
            self.noise = rho*self.noise + cfg.noise_w*math.sqrt(1-rho*rho)*self.random.gauss(0, 1)
            self.last_report = now
            self.power = max(0, round(delayed + self.noise))
            self.cadence = max(0, round(cadence))
        return fresh


@dataclass
class RiderConfig:
    preferred_cadence: float = 85
    variation_rpm: float = 2.0
    correlation_s: float = 3.0
    response_s: float = 2.0
    fatigue_rpm_per_hour: float = 3.0
    load_droop_rpm_per_100w: float = 1.5

    def __post_init__(self):
        values = vars(self)
        if not all(math.isfinite(v) and v >= 0 for v in values.values()) or not 1 <= self.preferred_cadence <= 180:
            raise ValueError("Rider parameters must be finite and nonnegative, with preferred cadence 1..180")
        if min(self.correlation_s, self.response_s) <= 0:
            raise ValueError("Rider time constants must be positive")


class Rider:
    """Seeded correlated variation, fatigue, pedal ripple, and transient droop.

    Workout cadence cues affect the rider; workout watts never set bike power.
    Coefficients without direct ride identification remain exposed assumptions.
    """
    def __init__(self, config=None, seed=1):
        self.config = config or RiderConfig()
        self.random = random.Random(seed)
        self.cadence = self.config.preferred_cadence
        self.wander = self.phase = 0.0
        self.adapted_load = 0.0

    def advance(self, now, dt, load_w, cue=None):
        cfg = self.config
        rho = math.exp(-dt/max(.01, cfg.correlation_s))
        self.wander = rho*self.wander + cfg.variation_rpm*math.sqrt(1-rho*rho)*self.random.gauss(0, 1)
        self.adapted_load += -math.expm1(-dt/12)*(load_w-self.adapted_load)
        desired = cfg.preferred_cadence if cue is None else cue
        if desired == 0:
            self.cadence = max(0, self.cadence-40*dt)
            return self.cadence
        desired += self.wander - cfg.fatigue_rpm_per_hour*now/3600
        desired -= max(0, load_w-self.adapted_load)*cfg.load_droop_rpm_per_100w/100
        self.cadence += -math.expm1(-dt/max(.01, cfg.response_s))*(desired-self.cadence)
        self.phase += 2*math.pi*self.cadence/60*dt
        return max(0, min(180, self.cadence + .6*math.sin(2*self.phase)))
