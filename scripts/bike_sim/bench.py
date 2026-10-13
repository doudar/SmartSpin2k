"""Real-time, real-BLE hardware boundary runner. No firmware/controller mock.

Run `python -m scripts.bike_sim.bench --help`. Requires a Linux peripheral
adapter, a real SS2K, and a physically independent STEP/DIR/encoder counter.
An emitted GATT notification is not proof of firmware receipt: separate live
FTMS telemetry is recorded as evidence, never used to advance the plant.
"""
import argparse
import asyncio
import csv
from dataclasses import asdict
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import struct
import time

from scripts.erg_sim.model import Bike, BikeConfig
from scripts.erg_sim.cadence import PlannedRider
from scripts.erg_sim.workout import Workout
from .motor_capture import MotorCapture
from .protocol import SensorPublisher
from .transport_bluez import BlueZBikePeripheral


def uuid(short):
    return f"0000{short}-0000-1000-8000-00805f9b34fb"


CONTROL_POINT = uuid('2ad9')
INDOOR_BIKE_DATA = uuid('2ad2')
FIRMWARE_REVISION = uuid('2a26')


class BenchError(RuntimeError):
    pass


def decode_indoor_bike_data(payload):
    """Decode only standard cadence/power; preserve raw evidence independently."""
    if len(payload) < 2:
        raise BenchError("Truncated FTMS Indoor Bike Data flags")
    flags = struct.unpack_from('<H', payload)[0]
    offset = 2
    result = {"flags": flags}
    fields = [(not flags & 1, 'speed_kph', '<H', .01),
              (flags & 2, 'average_speed_kph', '<H', .01),
              (flags & 4, 'cadence_rpm', '<H', .5),
              (flags & 8, 'average_cadence_rpm', '<H', .5),
              (flags & 16, 'distance_m', None, 1),
              (flags & 32, 'resistance', '<h', .1),
              (flags & 64, 'power_w', '<h', 1)]
    for present, name, fmt, scale in fields:
        if not present:
            continue
        size = struct.calcsize(fmt) if fmt else 3
        if len(payload) < offset+size:
            raise BenchError(f"Truncated FTMS {name}")
        result[name] = (struct.unpack_from(fmt, payload, offset)[0] if fmt else int.from_bytes(payload[offset:offset+size], 'little'))*scale
        offset += size
    return result


class FTMSClient:
    """One indicated control procedure in flight. Negative results are fatal."""
    def __init__(self, client, *, timeout_s=5, event=None):
        self.client, self.timeout_s = client, timeout_s
        self.event = event or (lambda data: None)
        self.pending = None
        self.lock = asyncio.Lock()
        self.error = None
        self.telemetry_count = 0
        self.last_telemetry_s = None
        self.control_owned = False
        self.subscribed = False

    def _indication(self, _sender, data):
        raw = bytes(data)
        self.event({"kind": "ftms_indication", "payload_hex": raw.hex()})
        if self.pending is None:
            return
        opcode, future = self.pending
        if future.done():
            return
        if len(raw) < 3 or raw[0] != 0x80 or raw[1] != opcode:
            future.set_exception(BenchError("Unexpected or malformed FTMS procedure indication"))
        elif raw[2] != 1:
            future.set_exception(BenchError(f"FTMS opcode {opcode:#x} rejected, result {raw[2]:#x}"))
        else:
            future.set_result(raw)

    def _telemetry(self, _sender, data):
        raw = bytes(data)
        try:
            decoded = decode_indoor_bike_data(raw)
            self.event({"kind": "firmware_ftms_telemetry", "payload_hex": raw.hex(), **decoded})
            # Require both measurement fields, not an unrelated status heartbeat.
            if 'power_w' in decoded and 'cadence_rpm' in decoded:
                self.telemetry_count += 1
                self.last_telemetry_s = time.monotonic()
        except Exception as exc:
            self.error = exc

    async def subscribe(self):
        characteristic = self.client.services.get_characteristic(CONTROL_POINT)
        if characteristic is None or 'indicate' not in characteristic.properties:
            raise BenchError("Firmware FTMS control point does not support indications")
        await asyncio.wait_for(self.client.start_notify(CONTROL_POINT, self._indication), self.timeout_s)
        self.subscribed = True
        await asyncio.wait_for(self.client.start_notify(INDOOR_BIKE_DATA, self._telemetry), self.timeout_s)

    async def procedure(self, opcode, parameters=b''):
        async with self.lock:
            future = asyncio.get_running_loop().create_future()
            self.pending = (opcode, future)
            self.event({"kind": "ftms_request", "opcode": opcode, "parameters_hex": parameters.hex()})
            try:
                await asyncio.wait_for(self.client.write_gatt_char(CONTROL_POINT, bytes([opcode])+parameters, response=True), self.timeout_s)
                await asyncio.wait_for(future, self.timeout_s)
                if opcode == 0:
                    self.control_owned = True
            except asyncio.TimeoutError as exc:
                raise BenchError(f"FTMS opcode {opcode:#x} write/indication timed out") from exc
            finally:
                self.pending = None
                if not future.done():
                    future.cancel()

    async def target(self, watts):
        if type(watts) is not int or not 0 <= watts <= 32767:
            raise ValueError("FTMS watts must be an integer in 0..32767")
        await self.procedure(5, struct.pack('<h', watts))

    async def stop(self):
        if self.control_owned:
            await self.procedure(8, b'\x01')

    def healthy(self, telemetry_timeout_s):
        if not self.client.is_connected:
            raise BenchError("Firmware BLE disconnected")
        if self.error:
            raise BenchError(f"Invalid firmware telemetry: {self.error}")
        if self.last_telemetry_s is None or time.monotonic()-self.last_telemetry_s > telemetry_timeout_s:
            raise BenchError("Firmware FTMS power/cadence telemetry missing or stale")


async def apply_targets(control, workout, started, stop_event, interval_s=.25):
    previous = None
    while not stop_event.is_set():
        elapsed = time.monotonic()-started
        target = workout.at(min(elapsed, workout.duration))[0]
        if target != previous:
            await control.target(target)
            previous = target
        try:
            await asyncio.wait_for(stop_event.wait(), interval_s)
        except asyncio.TimeoutError:
            pass


def sha256_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


async def run_bench(args):
    # Lazy import lets contract tests run without pretending hardware exists.
    try:
        from bleak import BleakClient
    except ImportError as exc:
        raise BenchError("Install requirements-bench.txt for real BLE operation") from exc
    profile = json.loads(Path(args.profile).read_text())
    config = BikeConfig(**profile['bike'])
    if not config.min_position <= args.initial_position <= config.max_position:
        raise ValueError('Initial physical position is outside the profile travel')
    firmware_config = json.loads(Path(args.firmware_config).read_text())
    workout = Workout.load(args.workout, ftp=args.ftp)
    if any(max(s.start_w, s.end_w) > 32767 for s in workout.segments):
        raise ValueError("Workout targets exceed FTMS signed 16-bit watts")
    rider = PlannedRider(workout)
    bike = Bike(config, args.initial_position, rider.cadence, args.seed)
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    # Never overwrite earlier hardware evidence accidentally.
    if any((output/name).exists() for name in ('events.jsonl', 'trace.csv', 'metadata.json')):
        raise FileExistsError('Output contains bench evidence; choose a new output directory')
    origin = time.monotonic()
    def event(data):
        log.write(json.dumps({"host_monotonic_s": time.monotonic(), **data}, allow_nan=False)+'\n')
        log.flush()
    metadata = {"mode": "real-hardware-ble-external-counter", "status": "incomplete",
        "started_utc": datetime.now(timezone.utc).isoformat(), "build_id_operator_supplied": args.build_id,
        "firmware_image_sha256": sha256_file(args.firmware_image),
        "firmware_config_operator_supplied": firmware_config,
        "firmware_config_sha256": sha256_file(args.firmware_config),
        "firmware_attribution": "Live DIS revision verified; flashed image hash and configuration are operator attestations, not read back from device",
        "capture_description": args.capture_description, "capture_endpoint": args.capture,
        "capture_limits": {"stale_s": args.capture_stale_s, "max_delta": args.max_pulse_delta, "max_rate": args.max_pulse_rate},
        "pulse_scale": args.pulse_scale, "sensor_periods_s": {"power": args.power_period_s, "cadence": args.cadence_period_s},
        "firmware_address": args.firmware_address, "sensor_peer_address": args.sensor_peer_address,
        "peripheral_adapter": args.peripheral_adapter, "central_adapter": args.central_adapter,
        "profile_sha256": sha256_file(args.profile), "workout_sha256": sha256_file(args.workout),
        "bike_config": asdict(config), "initial_physical_position": args.initial_position,
        "cadence_plan": rider.plan.data, "cadence_plan_sha256": rider.plan.sha256,
        "seed": args.seed, "plant_tick_s": args.tick_s, "speed": 1,
        "limitations": ["GATT notifications emitted with CCC subscriptions are not delivery acknowledgments",
            "FTMS telemetry is independently logged, not proof every sensor notification was consumed",
            "STEP/DIR pulses represent electrical commands; encoder capture is needed to verify mechanical motion",
            "No performance pass/fail grade is assigned by this acquisition runner"]}
    log = (output/'events.jsonl').open('x', encoding='utf-8')
    trace = (output/'trace.csv').open('x', newline='', encoding='utf-8')
    trace_writer = csv.writer(trace)
    trace_writer.writerow(['time_s', 'target_w', 'power_w', 'cadence_rpm', 'physical_steps', 'motor_delta', 'motor_steps'])
    control = target_task = None
    stop_event = asyncio.Event()
    try:
        async with MotorCapture(args.capture, baudrate=args.baudrate, stale_s=args.capture_stale_s,
                max_delta=args.max_pulse_delta, max_rate=args.max_pulse_rate) as capture:
            async with BlueZBikePeripheral(adapter=args.peripheral_adapter, name=args.name) as peripheral:
                async with BleakClient(args.firmware_address, adapter=args.central_adapter, timeout=args.connect_timeout_s) as client:
                    revision = bytes(await asyncio.wait_for(client.read_gatt_char(FIRMWARE_REVISION), args.procedure_timeout_s)).decode('utf-8').rstrip('\x00')
                    metadata['firmware_revision_read'] = revision
                    if revision != args.expected_firmware_revision:
                        raise BenchError(f"Firmware revision mismatch: read {revision!r}")
                    control = FTMSClient(client, timeout_s=args.procedure_timeout_s, event=event)
                    try:
                        await control.subscribe()
                        deadline = time.monotonic()+args.subscribe_timeout_s
                        publisher = SensorPublisher(peripheral, args.power_period_s, args.cadence_period_s)
                        warmup = time.monotonic()
                        while True:
                            capture.snapshot()
                            publisher.update(time.monotonic()-warmup, bike.power, rider.cadence)
                            if peripheral.power_subscribed and peripheral.cadence_subscribed and control.telemetry_count:
                                break
                            if time.monotonic() >= deadline:
                                raise BenchError("Timed out waiting for CPS/CSC subscriptions and firmware FTMS telemetry")
                            await asyncio.sleep(min(args.tick_s, .1))
                        peers = await asyncio.wait_for(peripheral.connected_peer_addresses(), args.procedure_timeout_s)
                        metadata['connected_sensor_adapter_peers'] = peers
                        if {args.sensor_peer_address.upper()} != {p.upper() for p in peers}:
                            raise BenchError("Dedicated peripheral adapter must have exactly the expected firmware sensor peer")
                        # Zero relative pulse integration only at the explicit physical start.
                        capture.consume()
                        motion_start = capture.validator.total_motion
                        await control.procedure(0)  # Request control.
                        await control.target(workout.at(0)[0])
                        await control.procedure(7)  # Start/resume.
                        started = time.monotonic()
                        publisher = SensorPublisher(peripheral, args.power_period_s, args.cadence_period_s)
                        target_task = asyncio.create_task(apply_targets(control, workout, started, stop_event))
                        last = 0.0
                        tick = 0
                        next_peer_check = 0.0
                        while True:
                            scheduled = tick*args.tick_s
                            if scheduled > workout.duration:
                                break
                            await asyncio.sleep(max(0, started+scheduled-time.monotonic()))
                            elapsed = time.monotonic()-started
                            late = elapsed-scheduled
                            if late > args.max_tick_lateness_s:
                                raise BenchError(f"Real-time plant deadline missed by {late:.3f}s")
                            if target_task.done():
                                target_task.result()
                                raise BenchError("Workout command worker stopped unexpectedly")
                            control.healthy(args.telemetry_stale_s)
                            if not (peripheral.power_subscribed and peripheral.cadence_subscribed):
                                raise BenchError("CPS/CSC sensor subscription lost")
                            if elapsed >= next_peer_check:
                                peers = await asyncio.wait_for(peripheral.connected_peer_addresses(), args.procedure_timeout_s)
                                if {args.sensor_peer_address.upper()} != {p.upper() for p in peers}:
                                    raise BenchError("Firmware sensor BLE peer disconnected or unexpected peer joined dedicated adapter")
                                next_peer_check = elapsed+1
                            pulses, obs = capture.consume()
                            cadence = rider.advance(elapsed, elapsed-last)
                            bike.advance(elapsed, elapsed-last, pulses*args.pulse_scale, cadence)
                            sent = publisher.update(elapsed, bike.power, cadence)
                            event({"kind": "plant_tick", "time_s": elapsed, "scheduled_s": scheduled,
                                "late_s": late, "target_w": workout.at(elapsed)[0], "power_w": bike.power,
                                "cadence_rpm": cadence, "physical_steps": bike.position,
                                "motor_delta": pulses, "counter": asdict(obs),
                                "power_emitted": sent[0], "cadence_emitted": sent[1]})
                            trace_writer.writerow([elapsed, workout.at(elapsed)[0], bike.power, cadence, bike.position, pulses, obs.signed_steps])
                            trace.flush()
                            last = elapsed
                            tick += 1
                        stop_event.set()
                        await target_task
                        target_task = None
                        metadata['sensor_evidence'] = peripheral.counters
                        metadata['motor_samples'] = capture.validator.samples
                        metadata['observed_motion_pulses'] = capture.validator.total_motion-motion_start
                        metadata['firmware_telemetry_packets'] = control.telemetry_count
                        if not metadata['observed_motion_pulses']:
                            raise BenchError("No independently observed motor pulses; closed-loop boundary unproven")
                        for key in ('power', 'cadence'):
                            if not peripheral.counters[key]['notifications_emitted']:
                                raise BenchError(f"No actual {key} notification emissions")
                        metadata['status'] = 'acquisition-complete-ungraded'
                    finally:
                        stop_event.set()
                        if target_task:
                            target_task.cancel()
                            await asyncio.gather(target_task, return_exceptions=True)
                            target_task = None
                        # Remain connected until a positive stop indication, including failures.
                        if control:
                            try:
                                await control.stop()
                                metadata['stop_confirmed'] = control.control_owned
                            except Exception as exc:
                                metadata['stop_confirmed'] = False
                                metadata['stop_error'] = str(exc)
                                raise BenchError(f"STOP not confirmed; manually stop the trainer: {exc}") from exc
    except BaseException as exc:
        metadata['status'] = 'failed'
        metadata['error'] = f'{type(exc).__name__}: {exc}'
        raise
    finally:
        metadata['wall_seconds'] = time.monotonic()-origin
        (output/'metadata.json').write_text(json.dumps(metadata, indent=2, allow_nan=False)+'\n')
        log.close()
        trace.close()
    return metadata


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('firmware-address', 'sensor-peer-address', 'peripheral-adapter', 'central-adapter',
                 'capture', 'capture-description', 'firmware-image', 'expected-firmware-revision',
                 'firmware-config', 'build-id', 'profile', 'workout', 'output'):
        p.add_argument('--'+name, required=True)
    p.add_argument('--initial-position', type=float, required=True, help='Independently established physical position in profile steps')
    p.add_argument('--name', default='SS2K Bike Sim')
    p.add_argument('--baudrate', type=int, default=115200)
    p.add_argument('--ftp', type=float, default=250)
    p.add_argument('--seed', type=int, default=27)
    for name, default in [('tick-s', .02), ('max-tick-lateness-s', .2), ('power-period-s', 1.),
            ('cadence-period-s', 1.), ('capture-stale-s', .5), ('max-pulse-rate', 50000.),
            ('pulse-scale', 1.), ('connect-timeout-s', 20.), ('subscribe-timeout-s', 60.),
            ('procedure-timeout-s', 5.), ('telemetry-stale-s', 5.)]:
        p.add_argument('--'+name, type=float, default=default)
    p.add_argument('--max-pulse-delta', type=int, default=10000)
    return p


def main(argv=None):
    p = parser()
    args = p.parse_args(argv)
    for name, value in vars(args).items():
        if isinstance(value, float) and (not math.isfinite(value) or (name != 'initial_position' and value <= 0)):
            p.error(f'{name} must be finite and positive')
    if args.central_adapter == args.peripheral_adapter:
        p.error('Use separate dedicated peripheral and central adapters so sensor-peer evidence is unambiguous')
    if args.max_pulse_delta <= 0 or args.baudrate <= 0:
        p.error('Pulse delta and baudrate must be positive')
    if args.tick_s > min(args.power_period_s, args.cadence_period_s):
        p.error('Plant tick must not exceed either sensor period')
    try:
        result = asyncio.run(run_bench(args))
    except (Exception, KeyboardInterrupt) as exc:
        print(f'BENCH FAILED: {exc}')
        return 1
    print(f"Hardware acquisition complete, ungraded: {args.output}; {result['observed_motion_pulses']} externally captured pulses")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
