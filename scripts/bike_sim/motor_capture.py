"""Independent STEP/DIR or encoder counter, never controller debug telemetry.

Adapter emits UTF-8 newline JSON at a regular heartbeat, even without motion:
{"seq": 1, "timestamp_us": 123456, "forward": 20, "reverse": 2}
All four fields are nonnegative integers; seq/time strictly increase and both
pulse totals only increase for the lifetime of the connection. No resets or
wraps are accepted. timestamp_us uses the capture device's monotonic clock.
"""
import asyncio
from dataclasses import dataclass
import json
import math
import time
from urllib.parse import urlparse


class CaptureError(RuntimeError):
    pass


@dataclass(frozen=True)
class Observation:
    seq: int
    timestamp_us: int
    forward: int
    reverse: int
    received_s: float

    @property
    def signed_steps(self):
        return self.forward - self.reverse


class CounterValidator:
    def __init__(self, *, stale_s=0.5, max_delta=10000, max_rate=50000):
        if not math.isfinite(stale_s) or stale_s <= 0 or max_delta <= 0 or not math.isfinite(max_rate) or max_rate <= 0:
            raise ValueError("Invalid counter watchdog or pulse limits")
        self.stale_s, self.max_delta, self.max_rate = stale_s, max_delta, max_rate
        self.latest = None
        self.anchor = None
        self.samples = 0
        self.total_motion = 0

    def accept(self, line, now=None):
        now = time.monotonic() if now is None else now
        try:
            data = json.loads(line)
            values = [data[k] for k in ("seq", "timestamp_us", "forward", "reverse")]
            if any(type(v) is not int or v < 0 for v in values):
                raise ValueError("counter fields must be nonnegative integers")
            obs = Observation(*values, now)
        except (ValueError, KeyError, TypeError) as exc:
            raise CaptureError(f"Invalid external counter frame: {exc}") from exc
        old = self.latest
        if old:
            if obs.seq <= old.seq or obs.timestamp_us <= old.timestamp_us:
                raise CaptureError("Counter sequence/time reset, repeated, or reordered")
            df, dr = obs.forward-old.forward, obs.reverse-old.reverse
            if min(df, dr) < 0:
                raise CaptureError("External counter reset or wrap")
            delta = df+dr
            elapsed = (obs.timestamp_us-old.timestamp_us)/1e6
            if delta > self.max_delta or delta > self.max_rate*elapsed+1:
                raise CaptureError("External counter delta/rate exceeds configured physical limit")
            # Clock offset is unknown, but drift/delivery delay since first frame
            # must remain bounded. This also rejects buffered old heartbeats.
            device_elapsed = (obs.timestamp_us-self.anchor.timestamp_us)/1e6
            host_elapsed = now-self.anchor.received_s
            if abs(host_elapsed-device_elapsed) > self.stale_s:
                raise CaptureError("Capture clock drift or buffered/stale device timestamps")
            self.total_motion += delta
        else:
            self.anchor = obs
        self.latest = obs
        self.samples += 1
        return obs

    def fresh(self, now=None):
        now = time.monotonic() if now is None else now
        if self.latest is None or now-self.latest.received_s > self.stale_s:
            raise CaptureError("External motor capture is missing or stale")
        return self.latest


class MotorCapture:
    def __init__(self, endpoint, *, baudrate=115200, **limits):
        self.endpoint, self.baudrate = endpoint, baudrate
        self.validator = CounterValidator(**limits)
        self.error = None
        self.reader = self.writer = self.task = None
        self.first = asyncio.Event()
        self.consumed = None

    async def __aenter__(self):
        uri = urlparse(self.endpoint)
        if uri.scheme == "tcp" and uri.hostname and uri.port:
            self.reader, self.writer = await asyncio.open_connection(uri.hostname, uri.port, limit=4096)
        elif uri.scheme == "unix" and uri.path:
            self.reader, self.writer = await asyncio.open_unix_connection(uri.path, limit=4096)
        elif uri.scheme == "serial" and uri.path:
            try:
                import serial_asyncio
            except ImportError as exc:
                raise CaptureError("Serial capture requires pyserial-asyncio") from exc
            self.reader, self.writer = await serial_asyncio.open_serial_connection(url=uri.path, baudrate=self.baudrate, limit=4096)
        else:
            raise ValueError("Capture endpoint must be tcp://host:port, unix:///path, or serial:///dev/tty...")
        self.task = asyncio.create_task(self._read())
        try:
            await asyncio.wait_for(self.first.wait(), self.validator.stale_s)
            self.snapshot()
        except BaseException:
            await self.__aexit__(None, None, None)
            raise
        return self

    async def _read(self):
        try:
            while True:
                line = await asyncio.wait_for(self.reader.readline(), self.validator.stale_s)
                if not line:
                    raise CaptureError("External motor counter disconnected")
                if len(line) > 4096 or not line.endswith(b"\n"):
                    raise CaptureError("Oversized or incomplete external counter frame")
                self.validator.accept(line)
                self.first.set()
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            self.error = CaptureError(f"External motor capture failed: {exc}")
            self.first.set()

    def snapshot(self):
        if self.error:
            raise self.error
        return self.validator.fresh()

    def consume(self):
        obs = self.snapshot()
        delta = 0 if self.consumed is None else obs.signed_steps-self.consumed
        self.consumed = obs.signed_steps
        return delta, obs

    async def __aexit__(self, *_):
        if self.task:
            self.task.cancel()
            await asyncio.gather(self.task, return_exceptions=True)
        if self.writer:
            self.writer.close()
            if hasattr(self.writer, "wait_closed"):
                await self.writer.wait_closed()
