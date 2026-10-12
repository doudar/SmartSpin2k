"""End-to-end accelerated packet/motor experiment and a stuck-motor negative control.

No radio/ESP32 scheduling is claimed. The negative control alters only a temporary
native peripheral adapter; production source files are never edited.
"""
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch
try:
    import numpy
except ModuleNotFoundError as exc:
    raise unittest.SkipTest('numpy required for packet ride tests') from exc

from scripts.erg_sim.bridge import ROOT, build
from scripts.erg_sim.model import BikeConfig
from scripts.erg_sim.runner import run
from scripts.erg_sim.sampling import SensorSchedule
from scripts.erg_sim.performance import summarize, compare
from scripts.erg_sim.workout import Segment, Workout


@unittest.skipUnless(shutil.which('g++') and shutil.which('gcc'), 'native C/C++ compilers required')
class PacketRideTests(unittest.TestCase):
    def test_real_packets_drive_motor_and_stuck_motor_fails(self):
        workout = Workout([Segment(45, 150, 150, 85), Segment(45, 250, 250, 85), Segment(45, 160, 160, 85)])
        cfg = BikeConfig(noise_w=0, lag_s=.5, delay_s=.3)
        original = Path.read_text
        def stuck(path, *args, **kwargs):
            text = original(path, *args, **kwargs)
            if path == ROOT/'scripts/erg_sim/native.cpp':
                text = text.replace('const int pulses = motor.advance(now-clockMs);', 'const int pulses = 0;')
            return text
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            scores = {}
            for name in ('working', 'stuck'):
                if name == 'stuck':
                    with patch.object(Path, 'read_text', stuck): exe = build(folder/name/'build', packet_ingress=True)
                else: exe = build(folder/name/'build', packet_ingress=True)
                trace, metadata = run(workout, cfg, exe, folder/name/'run', initial_position=11000,
                                      power_schedule=SensorSchedule(.5), cadence_schedule=SensorSchedule(.25),
                                      packet_ingress=True)
                evidence = metadata['packet_ingress_evidence']
                self.assertGreater(evidence['processed'], 500)
                self.assertEqual(evidence['rejected'], 0)
                self.assertGreater(evidence['final_cadence_rpm'], 70)
                scores[name] = summarize(trace, workout.duration, workout)
                if name == 'working': self.assertGreater(float(numpy.ptp(trace[:, 4])), 500)
                else: self.assertEqual(float(numpy.ptp(trace[:, 4])), 0)
            self.assertFalse(compare(scores['stuck'], scores['working'])['relative_passed'])
            self.assertGreater(scores['stuck']['mae_w'], scores['working']['mae_w']+15)


if __name__ == '__main__': unittest.main()
