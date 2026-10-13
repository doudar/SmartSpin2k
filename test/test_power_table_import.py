"""One-time pre-ride RAM snapshot import through the compiled packet adapter.

The synthetic empty/partial/dense fixtures below exercise the storage boundary,
not physically learned surfaces or evidence of controller/plant accuracy.
"""
from copy import deepcopy
from pathlib import Path
import shutil
import struct
import tempfile
import unittest

from scripts.erg_sim.bridge import Bridge, build
from scripts.erg_sim.model import Bike, BikeConfig


METADATA = ("version", "quality", "homed", "rows", "columns", "cadence_min",
            "cadence_increment", "watt_increment", "position_divisor")


def wire(snapshot):
    """Encode independently of the wrapper so native rejection is exercised."""
    values = [snapshot[key] for key in METADATA]
    values.extend(value for cell in snapshot["cells"] for value in cell)
    return "U " + " ".join(map(str, values))


def populated(snapshot, dense=False):
    result = deepcopy(snapshot)
    if dense:
        # Reliable by readings count only; deliberately no physical provenance.
        result["cells"] = [[i, 2 + i % 3] for i in range(len(result["cells"]))]
    else:
        for index, cell in ((0, [-32767, 1]), (1, [0, 2]), (-1, [32767, 127])):
            result["cells"][index] = cell
    result["quality"] = sum(n for _, n in result["cells"])
    return result


@unittest.skipUnless(shutil.which("g++") and shutil.which("gcc"), "native C/C++ compilers required")
class PowerTableImportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.folder = Path(cls.temporary.name)
        cls.exe = build(cls.folder / "build", packet_ingress=True)

    def bridge(self, **kwargs):
        bridge = Bridge(self.exe, self.folder / (self.id().split(".")[-1] + ".log"),
                        packet_ingress=True, **kwargs)
        self.addCleanup(bridge.close)
        return bridge

    def assert_packets_and_tick_work(self, bridge):
        bridge.notify(0x2a63, struct.pack("<Hh", 0, 210))
        result = bridge.tick(bridge.now_ms + 100, 0, 0, False, target=200)
        self.assertEqual(len(result), 8)
        stats = bridge.ingress_stats()
        self.assertGreaterEqual(stats["processed"], 1)
        self.assertEqual(stats["final_power_w"], 210)
        self.assertEqual(stats["rejected"], 0)

    def test_empty_partial_and_dense_snapshots_round_trip(self):
        for name in ("empty", "partial", "dense"):
            for homed in (False, True):
                with self.subTest(name=name, homed=homed):
                    with Bridge(self.exe, self.folder / f"{name}-{homed}.log",
                                packet_ingress=True, homed=homed) as bridge:
                        expected = bridge.power_table()
                        if name != "empty":
                            expected = populated(expected, dense=name == "dense")
                        original = deepcopy(expected)
                        bridge.load_power_table(expected)
                        self.assertEqual(expected, original, "Caller-owned snapshot changed")
                        self.assertEqual(bridge.power_table(), original)
                        self.assertEqual(bridge.power_table(), original, "Readback mutated RAM")
                        self.assert_packets_and_tick_work(bridge)

    def test_python_rejects_malformed_snapshots_without_mutation(self):
        bridge = self.bridge()
        before = bridge.power_table()
        good = populated(before)
        malformed = [("not a mapping", None), ("list", []), ("empty mapping", {})]
        for key in METADATA + ("cells",):
            item = deepcopy(good)
            del item[key]
            malformed.append(("missing " + key, item))
        for key in METADATA:
            item = deepcopy(good)
            item[key] = good[key] + 0.5
            malformed.append(("fractional " + key, item))
        for key in METADATA:
            item = deepcopy(good)
            item[key] = good[key] + 1
            malformed.append(("mismatched " + key, item))
        for label, cell in (("position low", [-32769, 1]), ("position high", [32768, 1]),
                            ("position fractional", [3.5, 1]), ("readings negative", [5, -1]),
                            ("readings high", [5, 128]), ("readings fractional", [5, 1.5]),
                            ("empty sentinel with readings", [-32768, 1]),
                            ("zero readings without sentinel", [0, 0]),
                            ("short cell", [1]), ("long cell", [1, 2, 3]),
                            ("cell string", "12"), ("cell none", None)):
            item = deepcopy(good)
            item["cells"][-1] = cell
            malformed.append((label, item))
        for label, cells in (("missing cell", good["cells"][:-1]),
                             ("extra cell", good["cells"] + [[-32768, 0]]),
                             ("cells none", None), ("cells string", "invalid")):
            item = deepcopy(good)
            item["cells"] = cells
            malformed.append((label, item))
        for label, item in malformed:
            with self.subTest(label=label):
                with self.assertRaises(ValueError):
                    bridge.load_power_table(item)
                self.assertEqual(bridge.power_table(), before)
        # Failed validation must not consume the one-time import opportunity.
        bridge.load_power_table(good)
        self.assertEqual(bridge.power_table(), good)
        self.assert_packets_and_tick_work(bridge)

    def test_native_rejects_partial_trailing_and_invalid_imports_atomically(self):
        bridge = self.bridge()
        before = bridge.power_table()
        good = populated(before)
        full = wire(good)
        malformed = ["U", "U 1", " ".join(full.split()[:-1]), full + " 0", full + " trailing"]
        for key in METADATA:
            item = deepcopy(good)
            item[key] += 1
            malformed.append(wire(item))
        for value in (-32769, 32768, 1.5, "nan", "1x", 10**100):
            item = deepcopy(good)
            item["cells"][-1][0] = value
            malformed.append(wire(item))
        for value in (-1, 128, 1.5, "nan", "1x", 10**100):
            item = deepcopy(good)
            item["cells"][-1][1] = value
            malformed.append(wire(item))
        for cell in ([-32768, 1], [100, 0]):
            item = deepcopy(good)
            item["cells"][-1] = cell
            item["quality"] = sum(n for _, n in item["cells"])
            malformed.append(wire(item))
        for index, command in enumerate(malformed):
            with self.subTest(case=index):
                self.assertEqual(bridge.exchange(command), ["U", "0"])
                self.assertEqual(bridge.power_table(), before, "Rejected import partially changed RAM")
        self.assertEqual(bridge.exchange(full), ["U", "1"])
        self.assertEqual(bridge.power_table(), good)
        self.assert_packets_and_tick_work(bridge)

    def test_successful_import_is_single_use_even_for_empty_snapshot(self):
        bridge = self.bridge()
        empty = bridge.power_table()
        bridge.load_power_table(empty)
        replacement = populated(empty)
        with self.assertRaises(RuntimeError):
            bridge.load_power_table(replacement)
        self.assertEqual(bridge.exchange(wire(replacement)), ["U", "0"])
        self.assertEqual(bridge.power_table(), empty)
        self.assert_packets_and_tick_work(bridge)

    def test_tick_and_sensor_arrival_lock_import_before_any_learning(self):
        for event in ("tick", "sensor"):
            with self.subTest(event=event):
                with Bridge(self.exe, self.folder / f"locked-{event}.log", packet_ingress=True) as bridge:
                    snapshot = populated(bridge.power_table())
                    if event == "tick":
                        bridge.tick(1100, 0, 0, False)
                    else:
                        bridge.notify(0x2a63, struct.pack("<Hh", 0, 200))
                    before = bridge.power_table()
                    with self.assertRaises(RuntimeError):
                        bridge.load_power_table(snapshot)
                    self.assertEqual(bridge.exchange(wire(snapshot)), ["U", "0"])
                    self.assertEqual(bridge.power_table(), before)
                    self.assert_packets_and_tick_work(bridge)

    def test_homing_locks_import(self):
        bridge = self.bridge(position=5000, hi=10000)
        bike = Bike(BikeConfig(min_position=0, max_position=10000), 5000)
        result = bridge.home(bike)
        self.assertTrue(result["found"])
        before = bridge.power_table()
        with self.assertRaises(RuntimeError):
            bridge.load_power_table(populated(before))
        self.assertEqual(bridge.exchange(wire(populated(before))), ["U", "0"])
        self.assertEqual(bridge.power_table(), before)
        self.assert_packets_and_tick_work(bridge)


if __name__ == "__main__":
    unittest.main()
