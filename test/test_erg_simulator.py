"""Production ERG + fitted rider/bike, including the complete supplied ZWO.

Install scripts/erg_sim/requirements.txt, then run:
python -B -m unittest discover -s test -p test_erg_simulator.py
"""

from dataclasses import replace
import json
from pathlib import Path
import tempfile
import unittest

try:
    import numpy as np
    import scipy
    import matplotlib
    import fitdecode
except ModuleNotFoundError as exc:
    # Optional for generic integration discovery (including existing CI). The
    # local native hook checks dependencies first and fails with setup guidance.
    raise unittest.SkipTest("Install scripts/erg_sim/requirements.txt to run the ERG simulator") from exc

from scripts.erg_sim.bridge import ROOT, Bridge, build
from scripts.erg_sim.calibrate import metrics, predict_reference
from scripts.erg_sim.model import Bike, BikeConfig, RiderConfig, equilibrium
from scripts.erg_sim.cadence import CadencePlan, PlannedRider
from scripts.erg_sim.power_table import read_ptab
from scripts.erg_sim.reference import first_erg_section, load_reference
from scripts.erg_sim.report import plot_run, save_run
from scripts.erg_sim.runner import run
from scripts.erg_sim.scoring import grade
from scripts.erg_sim.workout import Segment, Workout

HERE = ROOT/"scripts/erg_sim"
PROFILE = json.loads((HERE/"profiles/ride_20260927.json").read_text(encoding="utf-8"))


class TestBikePhysics(unittest.TestCase):
    def test_power_only_follows_steps_cadence_and_elapsed_time(self):
        cfg = BikeConfig(position_knots=[0, 1000], watts_at_80rpm=[0, 400], noise_w=0,
                         delay_s=2, lag_s=1, sample_period_s=.1, max_position=1000)
        bike = Bike(cfg, 250, 80)
        # Double resistance; feedback must stay at 100 W for the transport delay.
        for tick in range(1, 200):
            bike.advance(tick*.01, .01, 250 if tick == 1 else 0, 80)
            self.assertEqual(bike.power, 100)
        for tick in range(200, 1001): bike.advance(tick*.01, .01, 0, 80)
        self.assertAlmostEqual(bike.power, 200, delta=1)
        self.assertEqual(equilibrium(cfg, 1000, 0), 0)
        self.assertGreater(equilibrium(cfg, 500, 100), equilibrium(cfg, 500, 80))

    def test_physical_endstop_does_not_rewrite_motor_pulses(self):
        bike = Bike(BikeConfig(min_position=0, max_position=1000), 900)
        bike.advance(.1, .1, 250, 80)
        self.assertEqual(bike.position, 1000)
        self.assertEqual(bike.load, 1)
        bike.advance(.2, .1, -50, 80)
        self.assertEqual(bike.position, 950)
        self.assertEqual(bike.load, 0)

    def test_backlash_consumes_reversal_steps(self):
        bike = Bike(BikeConfig(backlash_steps=100), 1000)
        bike.advance(.1, .1, 200, 80)
        bike.advance(.2, .1, -50, 80)
        self.assertEqual(bike.position, 1200)
        bike.advance(.3, .1, -100, 80)
        self.assertEqual(bike.position, 1150)

    def test_chronological_holdout_and_lag_identification(self):
        data = load_reference(ROOT/"test/data/erg_reference_20260927.csv")
        cfg = BikeConfig(**PROFILE["bike"])
        holdout = (data[:, 0] >= PROFILE["identification"]["training_end_s"]) & (data[:, 2] >= 40) & (data[:, 1] > 0)
        prediction = predict_reference(data, cfg)
        score = metrics(data[holdout, 1], prediction[holdout])
        # Measured same-ride holdout acceptance, not a claim of exact reproduction.
        self.assertGreater(score["samples"], 1000)
        self.assertLess(score["mae_w"], 13)
        self.assertLess(score["p95_absolute_error_w"], 36)
        self.assertLess(score["rmse_w"], .8*PROFILE["identification"]["zero_lag_holdout"]["rmse_w"])
        # Cross-check the scalar online plant against the vectorized fitter;
        # this catches fitting a different lag equation than the test actually runs.
        cfg = replace(cfg, noise_w=0)
        bike = Bike(cfg, data[0, 3], data[0, 2], initial_power=data[0, 1])
        sampled, clock, position = [], 0.0, data[0, 3]
        for row in data[:500]:
            while clock < row[0]-1e-9:
                dt = min(.02, row[0]-clock); clock += dt
                next_pos = np.interp(clock, data[:, 0], data[:, 3])
                cadence = np.interp(clock, data[:, 0], data[:, 2])
                bike.advance(clock, dt, next_pos-position, cadence)
                position = next_pos
            # Read the delayed latent signal directly via the scheduled report,
            # then compare at its actual report time to allow its held sample age.
            sampled.append((bike.last_report, bike.power))
        expected = np.interp(np.array(sampled)[:, 0], data[:, 0], prediction)
        self.assertLess(float(np.mean(np.abs(np.array(sampled)[:, 1]-expected))), 2)


class TestWorkoutAndGrades(unittest.TestCase):
    def test_interval_callouts_plateaus_and_fatigue_recovery(self):
        workout = Workout.load(HERE/"workouts/Random_Attacks.zwo", 305)
        plan = CadencePlan(workout)
        self.assertEqual(plan.at(620)[0], 110)
        # Exercise stable cadence separately: the editable workout may ramp or
        # add fatigue to any interval without breaking this behavior check.
        stable = CadencePlan(Workout([Segment(300, 200, 200, 85)]))
        values = [stable.at(t)[1] for t in range(30, 290)]
        self.assertLessEqual(max(values)-min(values), 2.5)
        self.assertGreater(max(values)-min(values), 1)
        hard = CadencePlan(Workout([Segment(180, 400, 400)]))
        hard.data["drift_rpm"] = 0
        target = hard.at(0)[0]
        self.assertAlmostEqual(hard.at(90)[1], target-4)
        self.assertAlmostEqual(hard.at(100)[1], target)
        hard.data["segments"][0]["sag_rpm"] = 10
        self.assertAlmostEqual(hard.at(90)[1], target-10)
        self.assertAlmostEqual(hard.at(100)[1], target)
        rider = PlannedRider(workout)
        previous = rider.cadence
        for tick in range(1, 36001):  # Includes the first fast drill and recovery.
            current = rider.advance(tick*.02, .02)
            self.assertLessEqual(abs(current-previous), .040001)
            previous = current

    def test_cadence_plan_hash_is_portable_and_rejects_different_workout(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/"ride.zwo"
            source = (HERE/"workouts/Random_Attacks.zwo").read_text(encoding="utf-8")
            path.with_suffix(".cadence.json").write_bytes((HERE/"workouts/Random_Attacks.cadence.json").read_bytes())
            hashes = []
            for newline in ("\n", "\r\n"):
                path.write_bytes(source.replace("\n", newline).encode())
                hashes.append(CadencePlan(Workout.load(path, 305)).sha256)
            self.assertEqual(hashes[0], hashes[1])
            path.write_text(source.replace('Duration="600"', 'Duration="601"'), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "hash differs"):
                CadencePlan(Workout.load(path, 305))
        explicit = CadencePlan(Workout([Segment(100, 400, 400, 110)]))
        self.assertEqual(explicit.at(30)[0], 110)

    def test_supplied_zwo_target_schedule(self):
        workout = Workout.load(HERE/"workouts/Random_Attacks.zwo", 305)
        self.assertEqual(workout.duration, 3715)
        self.assertEqual(workout.at(0)[0], 76)
        self.assertEqual(workout.at(300)[0], 152)
        self.assertEqual(workout.at(660)[0], 152)
        self.assertEqual(workout.at(960)[0], 381)
        self.assertEqual(workout.at(3715)[0], 76)

    def test_thresholds_count_strict_exceedances_and_episodes(self):
        # Errors 10, 11, 21, 5, 100, 101 W with unequal report intervals.
        trace = np.zeros((6, 10))
        trace[:, 0] = [0, 1, 2, 4, 5, 6]
        trace[:, 1] = 200; trace[:, 2] = 200+np.array([10, 11, 21, 5, 100, 101])
        score = grade(trace, 7)
        self.assertEqual(score["max_absolute_error_w"], 101)
        self.assertEqual(score["p95_absolute_error_w"], 101)
        self.assertEqual(score["deviations_over_w"]["10"]["samples"], 4)
        self.assertEqual(score["deviations_over_w"]["10"]["episodes"], 2)
        self.assertEqual(score["deviations_over_w"]["10"]["seconds"], 5)
        self.assertEqual(score["deviations_over_w"]["100"]["samples"], 1)

    def test_unsupported_zwo_and_invalid_parameters_fail(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/"bad.zwo"
            path.write_text('<workout_file><workout><FreeRide Duration="60"/></workout></workout_file>')
            with self.assertRaisesRegex(ValueError, "Unsupported ZWO"):
                Workout.load(path)
        with self.assertRaises(ValueError): BikeConfig(delay_s=-1)
        with self.assertRaises(ValueError): RiderConfig(preferred_cadence=float("nan"))
        with self.assertRaises(ValueError): Workout([Segment(0, 200, 200)])


class TestProductionBikeLoop(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.folder = Path(cls.temp.name)
        cls.exe = build(cls.folder/"native")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_complete_random_attacks_workout(self):
        workout = Workout.load(HERE/"workouts/Random_Attacks.zwo", 305)
        output = ROOT/"test/output/erg_regression"
        trace, metadata = run(workout, BikeConfig(**PROFILE["bike"]), self.exe, output,
                              rider_config=RiderConfig(**PROFILE["rider"]), seed=27)
        metadata.update(workout="Random_Attacks.zwo", ftp_w=305)
        score = grade(trace, workout.duration, workout)
        save_run(output, trace, score, metadata)
        plot_run(output, trace, workout=workout, ftp=305, score=score)
        table = json.loads((output/"power_table.json").read_text(encoding="utf-8"))
        version, quality, homed, cells = read_ptab(output/"learned.ptab", table["rows"], table["columns"])
        self.assertEqual(cells, table["cells"])
        self.assertEqual(version, table["version"])
        self.assertTrue(homed)
        self.assertEqual(quality, sum(cell[1] for cell in cells))
        supported = sum(p != -32768 and n >= 2 for p, n in cells)
        self.assertEqual(supported, metadata["power_table"]["supported_cells"])
        self.assertEqual(supported, trace[-1, 7])
        self.assertTrue(any(p == -32768 and n == 0 for p, n in cells))
        self.assertGreater((output/"power_table.png").stat().st_size, 1000)
        self.assertEqual(len(trace), 3715)
        self.assertGreater(trace[-1, 7], 12)  # Production table actually learned.
        self.assertTrue(np.all((trace[:, 4] >= 0) & (trace[:, 4] <= 24482)))
        self.assertGreater(np.std(trace[:, 3]), 4)  # A rider, not fixed cadence.
        # Broad regression budgets, intentionally allowing improvements and
        # small platform differences. Full transition errors remain counted.
        self.assertLessEqual(score["max_absolute_error_w"], 300)
        self.assertLessEqual(score["p95_absolute_error_w"], 45)
        self.assertLessEqual(score["mae_w"], 18)
        for threshold, budget_percent in {10: 58, 20: 24, 30: 12, 50: 5, 100: 2}.items():
            self.assertLessEqual(score["deviations_over_w"][str(threshold)]["percent_time"], budget_percent)

    def test_acceleration_changes_only_wall_time(self):
        workout = Workout([Segment(10, 155, 155), Segment(15, 335, 335), Segment(15, 170, 170)])
        cfg = BikeConfig(**PROFILE["bike"])
        unlimited, _ = run(workout, cfg, self.exe, self.folder/"fast", seed=3)
        wall = [0.0]
        def sleep(seconds): wall[0] += seconds
        for multiplier in (1, 20):
            trace, metadata = run(workout, cfg, self.exe, self.folder/f"speed{multiplier}", speed=multiplier,
                                  seed=3, sleep=sleep, monotonic=lambda: wall[0])
            np.testing.assert_array_equal(unlimited, trace)
            self.assertAlmostEqual(metadata["wall_seconds"], workout.duration/multiplier, places=6)

    def test_controller_tuning_cannot_change_planned_cadence(self):
        workout = Workout([Segment(20, 155, 155, 85), Segment(40, 400, 400, 105), Segment(30, 170, 170, 80)])
        cfg = BikeConfig(**PROFILE["bike"])
        a, ma = run(workout, cfg, self.exe, self.folder/"control3", sensitivity=3)
        b, mb = run(workout, cfg, self.exe, self.folder/"control8", sensitivity=8)
        np.testing.assert_array_equal(a[:, 3], b[:, 3])
        self.assertEqual(ma["cadence_plan_sha256"], mb["cadence_plan_sha256"])
        self.assertFalse(np.array_equal(a[:, 6], b[:, 6]))

    def test_table_snapshot_does_not_seed_or_mutate_controller(self):
        with Bridge(self.exe, self.folder/"snapshot.log") as bridge:
            before = bridge.power_table()
            after = bridge.power_table()
            self.assertEqual(before, after)
            self.assertEqual(before["quality"], 0)
            self.assertTrue(all(cell == [-32768, 0] for cell in before["cells"]))

    def test_recorded_erg_response_and_motor_position(self):
        data = load_reference(ROOT/"test/data/erg_reference_20260927.csv")
        section, workout = first_erg_section(data)
        trace, _ = run(workout, BikeConfig(**PROFILE["bike"]), self.exe, self.folder/"reference",
                       cadence_trace=section[:, [0, 2]], initial_position=round(section[0, 3]), initial_power=round(section[0, 1]))
        recorded_power = np.interp(trace[:, 0], section[:, 0], section[:, 1])
        recorded_position = np.interp(trace[:, 0], section[:, 0], section[:, 3])
        reproduction = metrics(recorded_power, trace[:, 2])
        self.assertGreater(workout.duration, 2000)
        self.assertLess(reproduction["mae_w"], 25)
        self.assertLess(reproduction["p95_absolute_error_w"], 70)
        self.assertLess(np.mean(np.abs(trace[:, 4]-recorded_position)), 500)

    def test_longer_lag_changes_the_closed_loop(self):
        workout = Workout([Segment(30, 155, 155), Segment(30, 335, 335), Segment(30, 155, 155)])
        cfg = BikeConfig(**PROFILE["bike"])
        instant, _ = run(workout, replace(cfg, lag_s=0, delay_s=0, noise_w=0), self.exe, self.folder/"instant")
        delayed, _ = run(workout, replace(cfg, lag_s=3, delay_s=3, noise_w=0), self.exe, self.folder/"delayed")
        self.assertGreater(np.mean(np.abs(instant[:, 2]-delayed[:, 2])), 20)
        self.assertFalse(np.array_equal(instant[:, 6], delayed[:, 6]))

    def test_production_stallguard_detects_both_physical_stops(self):
        for forward in (False, True):
            cfg = BikeConfig(min_position=0, max_position=10000)
            bike = Bike(cfg, 5000)
            with Bridge(self.exe, self.folder/f"home{forward}.log", position=5000, hi=10000) as bridge:
                result = bridge.home(bike, forward=forward)
            self.assertTrue(result["found"])
            self.assertEqual(result["physical_position"], 10000 if forward else 0)
            self.assertLess(result["duration_s"], 10)
            # The native pulse counter carries on into a physical stall: no
            # hidden position feedback informed production homing.
            self.assertNotEqual(result["physical_position"], result["command_position"])
