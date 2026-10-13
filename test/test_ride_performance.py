"""Behavior and deliberate-degradation checks for independent ride evaluation."""
import unittest
try:
    import numpy as np
except ModuleNotFoundError as exc:
    raise unittest.SkipTest("Install scripts/erg_sim/requirements.txt for ride performance tests") from exc

from scripts.erg_sim.performance import summarize, compare
from scripts.erg_sim.sampling import SensorChannel, SensorSchedule
from scripts.erg_sim.workout import Segment, Workout


class SamplingTests(unittest.TestCase):
    def test_fast_sensor_acquires_continuous_plant_not_held_one_hz_reports(self):
        from scripts.erg_sim.model import Bike, BikeConfig
        bike = Bike(BikeConfig(position_knots=[0, 1000], watts_at_80rpm=[0, 400],
                    min_position=0, max_position=1000, delay_s=0, lag_s=1,
                    sample_period_s=100, noise_w=0), 0, 80)
        channel = SensorChannel(SensorSchedule(.25), 1)
        observed = channel.update(0, bike.sample_power)
        for tick in range(1, 101):
            now = tick/100
            bike.advance(now, .01, 500 if tick == 1 else 0, 80)
            observed += channel.update(now, bike.sample_power)
        self.assertEqual(bike.power, 0)  # Legacy reporting clock never ticked.
        self.assertGreater(len(set(value for _, value in observed)), 3)
        self.assertGreater(observed[-1][1], observed[1][1])
        self.assertGreater(bike.filtered, 100)

    def test_independent_rates_delay_and_frozen_samples(self):
        fast = SensorChannel(SensorSchedule(.25, .5), 1)
        slow = SensorChannel(SensorSchedule(1), 2)
        delivered, other = [], []
        for i in range(301):
            now = i/100
            delivered += [(now, *sample) for sample in fast.update(now, now*100)]
            other += slow.update(now, 100)
        self.assertGreater(len(delivered), 2*len(other))
        for at, sampled, value in delivered:
            self.assertAlmostEqual(at-sampled, .5, places=6)
            self.assertAlmostEqual(value, sampled*100, places=5)
        self.assertEqual(other[0][1], other[-1][1])  # equal values are still fresh samples

    def test_dropouts_and_jitter_repeat_without_plant_dependence(self):
        cfg = SensorSchedule(.5, .2, .1, dropouts=((1, 2),))
        channels = [SensorChannel(cfg, 23), SensorChannel(cfg, 23)]
        traces = [[], []]
        for i in range(401):
            for j, channel in enumerate(channels):
                traces[j] += [(i, stamp) for stamp, _ in channel.update(i/100, j*100)]
        self.assertEqual(traces[0], traces[1])
        self.assertFalse(any(1 <= sampled < 2 for _, sampled in traces[0]))
        self.assertEqual(channels[0].dropped, 2)
        self.assertGreater(channels[0].delivered, 0)

    def test_invalid_clock_and_missing_acquisitions_fail(self):
        for kwargs in ({'period_s': 0}, {'latency_s': -1}, {'jitter_s': float('nan')}, {'dropouts': ((3, 2),)}):
            with self.assertRaises(ValueError): SensorSchedule(**kwargs)
        channel = SensorChannel(SensorSchedule(.1), 1)
        channel.update(0, 20)
        with self.assertRaises(ValueError): channel.update(1, 30)


class RidePerformanceTests(unittest.TestCase):
    def setUp(self):
        self.workout = Workout([Segment(30, 150, 150, 85), Segment(30, 250, 250, 85), Segment(30, 160, 160, 85)])
        self.times = np.arange(0, 90, .25)
        self.targets = np.array([self.workout.at(t)[0] for t in self.times])

    def score(self, power):
        return summarize(np.column_stack((self.times, self.targets, power)), 90, self.workout)

    def test_perfect_response_and_real_improvement_pass(self):
        perfect = self.score(self.targets)
        self.assertTrue(compare(perfect, self.score(self.targets+5))['passed'])
        self.assertEqual(perfect['scored_seconds'], 90)
        self.assertEqual(perfect['unsettled_steps'], 0)

    def test_bias_oscillation_and_frozen_controller_are_rejected(self):
        baseline = self.score(self.targets+2*np.sin(self.times))
        degraded = [self.targets+40, self.targets+50*np.sin(self.times*2), np.full(len(self.times), 150)]
        for power in degraded:
            with self.subTest(kind=str(power[:4])):
                result = compare(self.score(power), baseline)
                self.assertFalse(result['passed'])
                self.assertFalse(result['relative_passed'])

    def test_slow_settling_detected_without_constant_value_assertions(self):
        baseline = self.score(self.targets)
        delayed = np.interp(self.times-12, self.times, self.targets)
        score = self.score(delayed)
        self.assertGreater(score['worst_settling_s'], 10)
        self.assertFalse(compare(score, baseline)['relative_passed'])

    def test_outage_cannot_be_hidden_by_removing_samples(self):
        data = np.column_stack((self.times, self.targets, self.targets))
        data = data[~((self.times >= 35) & (self.times < 45))]
        with self.assertRaisesRegex(ValueError, 'gaps'): summarize(data, 90, self.workout)

    def test_settling_requires_contiguous_dwell_before_interval_end(self):
        power = self.targets.copy()
        power[(self.times >= 30) & (self.times < 58)] += 30
        score = self.score(power)
        self.assertIsNone(score['transitions'][0]['settling_s'])
        self.assertGreater(score['unsettled_steps'], 0)


if __name__ == '__main__': unittest.main()
