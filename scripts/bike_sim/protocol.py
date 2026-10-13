"""Bluetooth SIG CPS/CSC packet encoding and independent sensor clocks.

Crank timestamps represent the last *revolution*, never notification time.
All times are elapsed monotonic seconds in the same simulation epoch.
"""
from dataclasses import dataclass
import math
import struct


@dataclass
class CrankState:
    revolutions: int = 0
    last_event_s: float = 0.0
    phase: float = 0.0
    time_s: float = 0.0

    def advance(self, now_s: float, cadence_rpm: float) -> None:
        """Integrate cadence held over [previous time, now]; preserve fractional turns."""
        if not math.isfinite(now_s) or now_s < self.time_s:
            raise ValueError("Crank time must be finite and nondecreasing")
        if not math.isfinite(cadence_rpm) or cadence_rpm < 0:
            raise ValueError("Cadence must be finite and nonnegative")
        rate = cadence_rpm / 60.0
        turns = self.phase + (now_s - self.time_s) * rate
        whole = math.floor(turns + 1e-12)
        if whole:
            self.last_event_s = self.time_s + (whole - self.phase) / rate
            self.revolutions += whole
        self.phase = max(0.0, turns - whole)
        self.time_s = now_s

    @property
    def event_ticks(self) -> int:
        return math.floor(self.last_event_s * 1024 + 1e-9) & 0xffff


def encode_power(watts: float, crank: CrankState | None = None) -> bytes:
    """Cycling Power Measurement: flags, signed watts, optional crank data."""
    if not math.isfinite(watts) or not -32768 <= round(watts) <= 32767:
        raise ValueError("Power must fit a signed 16-bit watt field")
    payload = struct.pack("<Hh", 0x20 if crank is not None else 0, round(watts))
    if crank is not None:
        payload += struct.pack("<HH", crank.revolutions & 0xffff, crank.event_ticks)
    return payload


def encode_csc(crank: CrankState) -> bytes:
    """CSC Measurement with crank-only flag; counts and time wrap at 16 bits."""
    return struct.pack("<BHH", 2, crank.revolutions & 0xffff, crank.event_ticks)


class SensorPublisher:
    """Drive a transport on plant ticks, using separate power and cadence schedules.

    Late ticks publish the latest sample once, not a burst of obsolete samples.
    Return values indicate notification attempts accepted by subscribed transport,
    not proof that firmware has consumed the measurements.
    """

    def __init__(self, transport, power_interval_s=1.0, cadence_interval_s=1.0):
        for interval in (power_interval_s, cadence_interval_s):
            if not math.isfinite(interval) or interval <= 0:
                raise ValueError("Sensor intervals must be finite and positive")
        self.transport = transport
        self.power_interval_s = power_interval_s
        self.cadence_interval_s = cadence_interval_s
        self.crank = CrankState()
        self.next_power_s = 0.0
        self.next_cadence_s = 0.0

    @staticmethod
    def _next_due(now, due, interval):
        return due + (math.floor((now - due + 1e-12) / interval) + 1) * interval

    def update(self, now_s, watts, cadence_rpm):
        self.crank.advance(now_s, cadence_rpm)
        power_sent = cadence_sent = False
        if now_s + 1e-12 >= self.next_power_s:
            power_sent = self.transport.publish_power(watts)
            self.next_power_s = self._next_due(now_s, self.next_power_s, self.power_interval_s)
        if now_s + 1e-12 >= self.next_cadence_s:
            cadence_sent = self.transport.publish_cadence(self.crank)
            self.next_cadence_s = self._next_due(now_s, self.next_cadence_s, self.cadence_interval_s)
        return power_sent, cadence_sent
