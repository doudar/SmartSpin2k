"""Counter/FTMS contract tests only. These do not validate real BLE or hardware."""
import asyncio
import json
import struct
import unittest
import tempfile
from pathlib import Path
import sys
from unittest.mock import patch
from types import SimpleNamespace
from scripts.bike_sim.motor_capture import CounterValidator, CaptureError, MotorCapture, Observation
from scripts.bike_sim.bench import FTMSClient, BenchError, decode_indoor_bike_data, apply_targets, parser, run_bench, INDOOR_BIKE_DATA
from scripts.erg_sim.workout import Workout, Segment


def frame(seq=1, timestamp_us=0, forward=0, reverse=0):
    return json.dumps(dict(seq=seq, timestamp_us=timestamp_us, forward=forward, reverse=reverse))+'\n'


class CounterContractTests(unittest.TestCase):
    def test_forward_reverse_and_idle_heartbeat(self):
        counter = CounterValidator()
        counter.accept(frame(), now=10)
        counter.accept(frame(2, 100000, 100, 2), now=10.1)
        self.assertEqual(counter.fresh(now=10.2).signed_steps, 98)
        counter.accept(frame(3, 200000, 100, 2), now=10.2)
        self.assertEqual(counter.total_motion, 102)

    def test_reset_duplicate_out_of_order_rejected(self):
        for candidate in (frame(1, 200000, 100, 0), frame(3, 100000, 100, 0),
                          frame(3, 200000, 99, 0), frame(3, 200000, 100, 0)):
            counter = CounterValidator()
            counter.accept(frame(2, 100000, 100, 1), now=0)
            with self.assertRaises(CaptureError):
                counter.accept(candidate, now=.1)

    def test_total_absolute_delta_not_net_is_limited(self):
        counter = CounterValidator(max_delta=10)
        counter.accept(frame(), now=0)
        with self.assertRaises(CaptureError):
            counter.accept(frame(2, 100000, 6, 6), now=.1)

    def test_rate_limit(self):
        counter = CounterValidator(max_rate=10)
        counter.accept(frame(), now=0)
        with self.assertRaises(CaptureError):
            counter.accept(frame(2, 100000, 10), now=.1)

    def test_timestamp_drift_and_stale_watchdog(self):
        counter = CounterValidator(stale_s=.2)
        counter.accept(frame(), now=1)
        with self.assertRaises(CaptureError):
            counter.fresh(now=1.21)
        with self.assertRaises(CaptureError):
            counter.accept(frame(2, 1000), now=1.3)

    def test_malformed_fields(self):
        for payload in ('{}', '[]', 'null', '{"seq":true,"timestamp_us":0,"forward":0,"reverse":0}',
                        '{"seq":1,"timestamp_us":0,"forward":1.5,"reverse":0}', 'garbage'):
            with self.assertRaises(CaptureError, msg=payload):
                CounterValidator().accept(payload)


class FakeGatt:
    """Tests the adapter API, not an emulated firmware controller."""
    def __init__(self, result=1, malformed=False, respond=True):
        self.result, self.malformed, self.respond = result, malformed, respond
        self.callbacks = {}
        self.is_connected = True
        self.services = SimpleNamespace(get_characteristic=lambda uuid: SimpleNamespace(properties=['write', 'indicate']))
        self.writes = []

    async def start_notify(self, uuid, callback):
        self.callbacks[uuid] = callback

    async def write_gatt_char(self, uuid, data, response):
        self.writes.append(bytes(data))
        if self.respond:
            self.callbacks[uuid](None, b'\x80' if self.malformed else bytes([0x80, data[0], self.result]))


class FtmsContractTests(unittest.IsolatedAsyncioTestCase):
    async def test_indicated_control_start_target_stop(self):
        fake = FakeGatt()
        control = FTMSClient(fake, timeout_s=.02)
        await control.subscribe()
        await control.procedure(0)
        await control.procedure(7)
        await control.target(250)
        await control.stop()
        self.assertEqual(fake.writes, [b'\0', b'\7', b'\5\xfa\0', b'\x08\x01'])

    async def test_no_indication_not_accepted_as_success(self):
        for fake in (FakeGatt(result=5), FakeGatt(malformed=True), FakeGatt(respond=False)):
            control = FTMSClient(fake, timeout_s=.01)
            await control.subscribe()
            with self.assertRaises(BenchError):
                await control.procedure(0)
            self.assertFalse(control.control_owned)

    async def test_indicate_property_required(self):
        fake = FakeGatt()
        fake.services = SimpleNamespace(get_characteristic=lambda uuid: SimpleNamespace(properties=['notify']))
        with self.assertRaises(BenchError):
            await FTMSClient(fake).subscribe()

    async def test_missing_telemetry_and_disconnect_fail(self):
        fake = FakeGatt()
        control = FTMSClient(fake)
        with self.assertRaises(BenchError):
            control.healthy(5)
        control._telemetry(None, struct.pack('<HHHHh', 0x64, 0, 160, 0, 210))
        control.healthy(5)
        self.assertEqual(control.telemetry_count, 1)
        fake.is_connected = False
        with self.assertRaises(BenchError):
            control.healthy(5)

    async def test_target_worker_propagates_rejection(self):
        fake = FakeGatt(result=5)
        control = FTMSClient(fake)
        await control.subscribe()
        with self.assertRaises(BenchError):
            await apply_targets(control, Workout([Segment(1, 100, 100)]), asyncio.get_running_loop().time(), asyncio.Event())

    def test_standard_telemetry_decode(self):
        decoded = decode_indoor_bike_data(struct.pack('<HHHHh', 0x64, 3050, 171, 20, 246))
        self.assertEqual(decoded['cadence_rpm'], 85.5)
        self.assertEqual(decoded['power_w'], 246)
        for payload in (b'', b'\x64\x00', b'\x64\x00\x00\x00'):
            with self.assertRaises(BenchError):
                decode_indoor_bike_data(payload)


class CaptureSocketTests(unittest.IsolatedAsyncioTestCase):
    async def test_real_stream_parser_net_counts_and_eof_failure(self):
        release = asyncio.Event()
        complete = asyncio.Event()
        observed = asyncio.Event()
        async def producer(reader, writer):
            try:
                writer.write(frame().encode())
                await writer.drain()
                await release.wait()
                writer.write(frame(2, 10000, 20, 2).encode())
                await writer.drain()
                await observed.wait()
            finally:
                writer.close()
                await writer.wait_closed()
                complete.set()
        server = await asyncio.start_server(producer, '127.0.0.1', 0)
        try:
            port = server.sockets[0].getsockname()[1]
            async with MotorCapture(f'tcp://127.0.0.1:{port}') as capture:
                self.assertEqual(capture.consume()[0], 0)
                release.set()
                async def received_second_frame():
                    while capture.validator.samples < 2:
                        capture.snapshot()
                        await asyncio.sleep(0)
                await asyncio.wait_for(received_second_frame(), 1)
                self.assertEqual(capture.consume()[0], 18)
                observed.set()
                await complete.wait()
                await asyncio.wait_for(capture.task, 1)
                with self.assertRaises(CaptureError):
                    capture.snapshot()
        finally:
            release.set()
            observed.set()
            server.close()
            await server.wait_closed()


class RunnerContractTests(unittest.IsolatedAsyncioTestCase):
    """Injected adapters test orchestration only, never hardware qualification."""
    async def exercise(self, *, subscriptions=True, fail_capture=False, stop_result=1):
        class Capture:
            def __init__(self, *a, **kw):
                self.validator = SimpleNamespace(total_motion=0, samples=0)
                self.calls = 0
            async def __aenter__(self): return self
            async def __aexit__(self, *a): pass
            def snapshot(self): return Observation(1, 0, 0, 0, 0)
            def consume(self):
                self.calls += 1
                if fail_capture and self.calls > 2:
                    raise CaptureError('Injected stale source')
                self.validator.total_motion += 1
                self.validator.samples += 1
                return 1, Observation(self.calls, self.calls*10000, self.calls, 0, 0)
        class Peripheral:
            def __init__(self, **kw):
                self.power_subscribed = self.cadence_subscribed = subscriptions
                self.counters = {k: {'notifications_emitted': 0} for k in ('power', 'cadence')}
            async def __aenter__(self): return self
            async def __aexit__(self, *a): pass
            async def connected_peer_addresses(self): return ['AA:BB:CC:DD:EE:FF']
            def publish_power(self, watts):
                if subscriptions: self.counters['power']['notifications_emitted'] += 1
                return subscriptions
            def publish_cadence(self, crank):
                if subscriptions: self.counters['cadence']['notifications_emitted'] += 1
                return subscriptions
        fake = FakeGatt()
        class Client:
            def __new__(cls, *a, **kw): return fake
        async def enter(): return fake
        async def leave(*a): pass
        async def read(uuid): return b'test-revision'
        original_notify = fake.start_notify
        async def notify(uuid, callback):
            await original_notify(uuid, callback)
            if uuid == INDOOR_BIKE_DATA:
                callback(None, struct.pack('<HHHHh', 0x64, 0, 160, 0, 200))
        original_write = fake.write_gatt_char
        async def write(uuid, data, response):
            fake.result = stop_result if data[0] == 8 else 1
            await original_write(uuid, data, response)
        fake.read_gatt_char = read
        fake.start_notify = notify
        fake.write_gatt_char = write
        # Special context methods are resolved on the class.
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/'profile.json').write_text('{"bike":{"noise_w":0}}')
            (root/'workout.json').write_text('{"segments":[{"duration_s":0.03,"watts":200}]}')
            (root/'config.json').write_text('{}')
            (root/'firmware.bin').write_bytes(b'unit-test-only')
            args = parser().parse_args([
                '--firmware-address', '11:22:33:44:55:66', '--sensor-peer-address', 'AA:BB:CC:DD:EE:FF',
                '--peripheral-adapter', 'hci0', '--central-adapter', 'hci1', '--capture', 'test-only',
                '--capture-description', 'Injected contract adapter, not real hardware', '--firmware-image', str(root/'firmware.bin'),
                '--expected-firmware-revision', 'test-revision', '--firmware-config', str(root/'config.json'),
                '--build-id', 'unit-test-only', '--profile', str(root/'profile.json'), '--workout', str(root/'workout.json'),
                '--output', str(root/'out'), '--initial-position', '10000', '--tick-s', '.01', '--subscribe-timeout-s', '.02'])
            with patch.dict(sys.modules, {'bleak': SimpleNamespace(BleakClient=Client)}), \
                 patch('scripts.bike_sim.bench.BlueZBikePeripheral', Peripheral), \
                 patch('scripts.bike_sim.bench.MotorCapture', Capture), \
                 patch.object(FakeGatt, '__aenter__', lambda _: enter(), create=True), \
                 patch.object(FakeGatt, '__aexit__', lambda _, *a: leave(*a), create=True):
                error = None
                try:
                    await run_bench(args)
                except (BenchError, CaptureError) as exc:
                    error = exc
            metadata = json.loads((root/'out'/'metadata.json').read_text())
            return fake.writes, metadata, error

    async def test_adapter_success_is_ungraded_and_stops(self):
        writes, metadata, error = await self.exercise()
        self.assertIsNone(error)
        self.assertEqual(metadata['status'], 'acquisition-complete-ungraded')
        self.assertTrue(metadata['stop_confirmed'])
        self.assertEqual(writes[-1], b'\x08\x01')

    async def test_missing_subscriber_never_completes(self):
        _, metadata, error = await self.exercise(subscriptions=False)
        self.assertIsInstance(error, BenchError)
        self.assertEqual(metadata['status'], 'failed')

    async def test_capture_failure_still_stops(self):
        writes, metadata, error = await self.exercise(fail_capture=True)
        self.assertIsInstance(error, CaptureError)
        self.assertEqual(metadata['status'], 'failed')
        self.assertEqual(writes[-1], b'\x08\x01')

    async def test_stop_rejection_invalidates_run(self):
        _, metadata, error = await self.exercise(stop_result=5)
        self.assertIsInstance(error, BenchError)
        self.assertEqual(metadata['status'], 'failed')
        self.assertFalse(metadata['stop_confirmed'])


if __name__ == '__main__':
    unittest.main()
