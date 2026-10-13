"""Wire-level and GATT contract tests; these are not over-air hardware tests."""
import importlib.util
import math
from pathlib import Path
import struct
import shutil
import subprocess
import tempfile
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from scripts.bike_sim.protocol import CrankState, SensorPublisher, encode_csc, encode_power
from scripts.bike_sim.transport_bluez import BlueZBikePeripheral, _interfaces


class ProtocolTests(unittest.TestCase):
    def test_standard_wire_layout_and_signed_power(self):
        crank = CrankState(revolutions=42, last_event_s=1.5)
        self.assertEqual(encode_power(250), b'\x00\x00\xfa\x00')
        self.assertEqual(encode_power(-20), b'\x00\x00\xec\xff')
        self.assertEqual(encode_power(250, crank), b'\x20\x00\xfa\x00\x2a\x00\x00\x06')
        self.assertEqual(encode_csc(crank), b'\x02\x2a\x00\x00\x06')

    def test_event_time_is_last_turn_not_last_notification(self):
        crank = CrankState()
        crank.advance(1.25, 90)
        self.assertEqual(crank.revolutions, 1)
        self.assertAlmostEqual(crank.last_event_s, 2/3)
        self.assertEqual(crank.event_ticks, 682)
        crank.advance(1.3, 90)
        self.assertEqual(crank.event_ticks, 682)
        crank.advance(2, 90)
        self.assertEqual(crank.revolutions, 3)
        self.assertEqual(crank.event_ticks, 2048)

    def test_fractional_turns_survive_cadence_changes_and_stops(self):
        crank = CrankState()
        crank.advance(.5, 60)
        crank.advance(.75, 120)
        self.assertEqual(crank.revolutions, 1)
        self.assertAlmostEqual(crank.last_event_s, .75)
        held = encode_csc(crank)
        crank.advance(10, 0)
        self.assertEqual(encode_csc(crank), held)
        crank.advance(10.5, 120)
        self.assertEqual(crank.revolutions, 2)
        self.assertAlmostEqual(crank.last_event_s, 10.5)

    def test_timer_and_revolution_wrap(self):
        crank = CrankState(revolutions=65535, last_event_s=63, time_s=63)
        crank.advance(65, 60)
        self.assertEqual(struct.unpack('<BHH', encode_csc(crank)), (2, 1, 1024))

    def test_tick_resolution_does_not_change_cumulative_motion(self):
        coarse, fine = CrankState(), CrankState()
        coarse.advance(100, 91.2)
        for tick in range(1, 10001):
            fine.advance(tick / 100, 91.2)
        self.assertEqual(coarse.revolutions, fine.revolutions)
        self.assertEqual(coarse.event_ticks, fine.event_ticks)
        self.assertAlmostEqual(coarse.phase, fine.phase, places=9)

    def test_invalid_inputs_fail(self):
        for value in (math.inf, math.nan, 32768, -32769):
            with self.assertRaises(ValueError):
                encode_power(value)
        for now, rpm in ((-1, 90), (1, -1), (math.nan, 90), (1, math.inf)):
            with self.assertRaises(ValueError):
                CrankState().advance(now, rpm)
        for interval in (0, -1, math.nan, math.inf):
            with self.assertRaises(ValueError):
                SensorPublisher(None, power_interval_s=interval)

    def test_schedules_independent_and_no_late_burst(self):
        class RecordingTransport:
            def __init__(self):
                self.power, self.cadence = [], []
            def publish_power(self, watts):
                self.power.append(encode_power(watts))
                return True
            def publish_cadence(self, crank):
                self.cadence.append(encode_csc(crank))
                return False
        transport = RecordingTransport()
        publisher = SensorPublisher(transport, .2, .7)
        for i in range(15):
            publisher.update(i / 10, 250, 90)
        self.assertEqual(len(transport.power), 8)
        self.assertEqual(len(transport.cadence), 3)
        self.assertTrue(all(len(frame) == 4 for frame in transport.power))
        self.assertEqual(publisher.update(10, 300, 90), (True, False))
        self.assertEqual(len(transport.power), 9)
        self.assertEqual(len(transport.cadence), 4)
        self.assertGreater(publisher.next_power_s, 10)
        self.assertGreater(publisher.next_cadence_s, 10)

    def test_unstarted_transport_does_not_claim_notifications(self):
        peripheral = BlueZBikePeripheral()
        self.assertFalse(peripheral.power_subscribed)
        self.assertFalse(peripheral.cadence_subscribed)
        self.assertEqual(peripheral.counters['power']['notifications_emitted'], 0)
        with self.assertRaises(RuntimeError):
            peripheral.publish_power(100)
        with self.assertRaises(ValueError):
            BlueZBikePeripheral(adapter='/org/bluez/hci0')


@unittest.skipUnless(importlib.util.find_spec('dbus_next'), 'Optional dbus-next not installed')
class GattContractTests(unittest.TestCase):
    def setUp(self):
        self.Service, self.Characteristic, self.Advertisement = _interfaces()
        self.service = self.Service('1818', '/test/service0')
        self.char = self.Characteristic('2a63', self.service, 0, encode_power(0), True)

    def test_notify_requires_real_start_notify_callback(self):
        with patch.object(self.char, 'emit_properties_changed') as emit:
            self.assertFalse(self.char.publish(encode_power(150)))
            emit.assert_not_called()
            self.char.StartNotify()
            self.assertTrue(self.char.publish(encode_power(200)))
            self.assertEqual(self.char.emitted, 1)
            self.assertEqual(self.char.subscription_count, 1)
            self.char.StartNotify()
            self.assertEqual(self.char.subscription_count, 1)
            self.char.StopNotify()
            self.assertFalse(self.char.publish(encode_power(250)))
            self.assertEqual(self.char.emitted, 1)
            self.assertIn(unittest.mock.call({'Value': encode_power(200)}), emit.mock_calls)

    def test_dbus_signatures_and_managed_objects(self):
        char = self.char.introspect()
        methods = {method.name: method for method in char.methods}
        self.assertEqual(methods['ReadValue'].in_signature, 'a{sv}')
        self.assertEqual(methods['ReadValue'].out_signature, 'ay')
        self.assertEqual(methods['StartNotify'].in_signature, '')
        properties = {prop.name: prop.signature for prop in char.properties}
        self.assertEqual(properties['Value'], 'ay')
        self.assertEqual(properties['Service'], 'o')
        self.assertEqual(self.char.properties()['Flags'].value, ['notify'])

    def test_read_offset_and_unsupported_notification(self):
        from dbus_next import Variant, DBusError
        feature = self.Characteristic('2a65', self.service, 1, b'\x08\x00\x00\x00')
        self.assertEqual(feature.ReadValue.__wrapped__(feature, {'offset': Variant('q', 1)}), b'\0\0\0')
        with self.assertRaises(DBusError):
            feature.ReadValue({'offset': Variant('q', 5)})
        with self.assertRaises(DBusError):
            feature.StartNotify()
        with self.assertRaises(DBusError):
            self.char.ReadValue({})

    def test_advertisement_contains_both_sensor_services(self):
        adv = self.Advertisement('SS2K Bike Sim')
        self.assertEqual(adv.Type, 'peripheral')
        self.assertEqual(len(adv.ServiceUUIDs), 2)
        self.assertEqual(adv.LocalName, 'SS2K Bike Sim')
        adv.Release()
        self.assertTrue(adv.released)



@unittest.skipUnless(shutil.which('g++') and shutil.which('gcc'), 'Production parser conformance requires g++ and gcc')
class FirmwareDecoderConformanceTests(unittest.TestCase):
    """Compile actual production parsers; explicitly record known protocol defects."""
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='bike-parser-')
        root = Path(__file__).resolve().parents[1]
        source = Path(cls.directory.name) / 'decode.cpp'
        source.write_text(r'''#include <iostream>
#include <sstream>
#include <vector>
#include "sensors/CyclePowerData.h"
#include "sensors/CscSensorData.h"
int main(int argc, char** argv) {
  CyclePowerData power;
  CscSensorData cadence;
  SensorData* sensor = std::string(argv[1]) == "power" ? static_cast<SensorData*>(&power) : static_cast<SensorData*>(&cadence);
  std::string line;
  while (std::getline(std::cin, line)) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < line.size(); i += 2) bytes.push_back(std::stoul(line.substr(i, 2), nullptr, 16));
    sensor->decode(bytes.data(), bytes.size());
    std::cout << sensor->getPower() << " " << sensor->getCadence() << "\n";
  }
}
''')
        cls.binary = Path(cls.directory.name) / 'decode'
        endian = Path(cls.directory.name) / 'endian.o'
        result = subprocess.run(['gcc', '-std=c99', '-I' + str(root / 'lib/ArduinoCompat/include'),
                                 '-c', str(root / 'lib/ArduinoCompat/src/os/endian.c'), '-o', str(endian)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)
        command = ['g++', '-std=c++17', '-O2', '-I' + str(root / 'lib/SS2K/include'),
                   '-I' + str(root / 'lib/ArduinoCompat/include'), str(source),
                   str(root / 'lib/SS2K/src/sensors/CyclePowerData.cpp'),
                   str(root / 'lib/SS2K/src/sensors/CscSensorData.cpp'),
                   str(endian), '-o', str(cls.binary)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def decode(self, kind, payloads):
        result = subprocess.run([str(self.binary), kind], input=''.join(p.hex() + '\n' for p in payloads),
                                text=True, capture_output=True, check=True)
        return [tuple(float(v) for v in line.split()) for line in result.stdout.splitlines()]

    def test_power_only_does_not_manufacture_cadence(self):
        results = self.decode('power', [encode_power(125), encode_power(275)])
        self.assertEqual([v[0] for v in results], [125, 275])
        self.assertTrue(all(math.isnan(v[1]) for v in results))

    def test_cps_and_csc_decode_same_generated_crank_stream(self):
        crank = CrankState()
        cps, csc = [], []
        for i in range(1, 21):
            crank.advance(i / 2, 90)
            cps.append(encode_power(250, crank))
            csc.append(encode_csc(crank))
        for kind, payloads in [('power', cps), ('cadence', csc)]:
            results = self.decode(kind, payloads)
            self.assertAlmostEqual(results[-1][1], 90, delta=1)

    def test_cps_wraps_cumulative_crank_and_event_time(self):
        payloads = [encode_power(200, CrankState(revolutions=65535, last_event_s=63.5)),
                    encode_power(200, CrankState(revolutions=0, last_event_s=64.5))]
        self.assertAlmostEqual(self.decode('power', payloads)[-1][1], 60, delta=.1)

    @unittest.expectedFailure
    def test_known_firmware_csc_crank_counter_wrap_defect(self):
        payloads = [encode_csc(CrankState(revolutions=65535, last_event_s=1)),
                    encode_csc(CrankState(revolutions=0, last_event_s=2))]
        self.assertEqual(self.decode('cadence', payloads)[-1][1], 60)

    @unittest.expectedFailure
    def test_known_firmware_csc_timer_wrap_off_by_one(self):
        payloads = [encode_csc(CrankState(revolutions=1, last_event_s=64817/1024)),
                    encode_csc(CrankState(revolutions=2, last_event_s=65500/1024)),
                    encode_csc(CrankState(revolutions=3, last_event_s=(65500+307)/1024))]
        self.assertEqual(self.decode('cadence', payloads)[-1][1], 200)


if __name__ == '__main__':
    unittest.main()
