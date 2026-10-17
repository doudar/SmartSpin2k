#!/usr/bin/env python3
"""Frozen-source, paired CPS/CSC timing stress; no Peloton cable emulation.

All scenario/controller/plant files remain read-only. Native build products and
run outputs are isolated under the requested new output directory.
"""
import argparse
import shutil
import subprocess
import tempfile
from dataclasses import asdict
import json
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'simulator'))
import numpy as np
from ss2k_sim.adapter import source_provenance
from ss2k_sim.cadence import CadencePlan
from ss2k_sim.high_power import diagnostic_workout, phase_response, response_summary
from ss2k_sim.longride import coverage, digest, log_events, observed_bridge, window_summary
from ss2k_sim.performance import DEFAULT_BUDGET, RELATIVE_ALLOWANCE, compare, summarize
from ss2k_sim.runner import run
from ss2k_sim.sampling import SensorSchedule
import extended_high_power as frozen

EXPERIMENT = 'local-high-power-paired-timing-stress-v1'
DURATION_S = 445
REGIMES = {
    'slow_power': (SensorSchedule(period_s=2, latency_s=.35, jitter_s=.1),
                   SensorSchedule(period_s=.25, latency_s=.1)),
    'fast_power_slow_cadence': (SensorSchedule(period_s=.25, latency_s=.1),
                              SensorSchedule(period_s=2, latency_s=.4)),
}
PREFIX = [{'name': 'low_power_prefix', 'start_s': 0, 'duration_s': 360}]


def close(actual, expected):
    if actual is None or expected is None:
        assert actual is expected, (actual, expected)
    else:
        assert abs(actual - expected) < 1e-7, (actual, expected)


def audit_phase(trace, report, column):
    """Independent interval arithmetic checks the saved phase diagnostics."""
    start = report['start_s']; end = start + report['duration_s']
    part = trace[(trace[:, 0] >= start) & (trace[:, 0] < end)]
    t = part[:, 0]; p = part[:, column]; target = report['target_w']
    dt = np.diff(np.r_[t, end])
    close(report['peak_power_w'], float(max(p)))
    close(report['peak_overshoot_w'], max(0., float(max(p)) - target))
    close(report['max_undershoot_w'], max(0., target - float(min(p))))
    close(report['mae_w'], float(np.sum(np.abs(p-target)*dt)/np.sum(dt)))
    for band in (10, 20):
        b = report['bands'][str(band)]; inside = np.abs(p-target) <= band
        intervals = []; i = 0
        while i < len(t):
            if not inside[i]:
                i += 1; continue
            first = i
            while i < len(t) and inside[i]: i += 1
            until = t[i] if i < len(t) else end
            intervals.append((float(t[first]), float(until)))
        qualifying = [(a,z) for a,z in intervals if z-a >= b['dwell_s']-1e-9]
        first = qualifying[0][0] if qualifying else None
        final = qualifying[-1][0] if qualifying and qualifying[-1][1] == end else None
        close(b['first_dwell_entry_s'], first-start if first is not None else None)
        close(b['final_settling_s'], final-start if final is not None else None)
        close(b['outside_band_s'], float(np.sum(dt[~inside])))
        close(b['above_band_s'], float(np.sum(dt[p > target+band])))
        close(b['below_band_s'], float(np.sum(dt[p < target-band])))
    final = report['bands']['10']['final_settling_s']
    assert report['goal']['met'] == (final is not None and final < 10)


def audit_diagnostics(trace, diagnostic):
    for name, column in (('effective_plant', 2), ('delivered_sensor', 14)):
        for section in ('sprint', 'recovery'):
            audit_phase(trace, diagnostic['signals'][name][section], column)
        for report in diagnostic['signals'][name]['low_power_prefix']:
            audit_phase(trace, report, column)


def run_case(adapter, exe, config, profile, dest, workout, table, sensitivity, regime):
    snapshots = []; controls = []
    factory = observed_bridge(adapter, snapshots, initial_table=table,
        snapshot_times=(60, 120, 180, 240, 300, 360, 390, 415, 420), control_events=controls)
    power, cadence = REGIMES[regime]
    trace, meta = run(workout, config, exe, dest, seed=7,
        initial_position=profile.get('start_position', 7433), sensitivity=sensitivity,
        power_schedule=power, cadence_schedule=cadence,
        packet_ingress=True, bridge_factory=factory)
    final = json.loads((dest/'power_table.json').read_text())
    snapshots.append({'time_s': DURATION_S, 'table': final})
    events = log_events(dest/'firmware.log')
    meta.update(initial_table='empty' if table is None else 'original earned immutable full fixture; RAM import',
        initial_table_sha256=digest(snapshots[0]['table']), initial_coverage=coverage(snapshots[0]['table']),
        final_coverage=coverage(final), scenario_version=EXPERIMENT, timing_regime=regime,
        controller_processes=1, state_resets_during_ride=0, pause_events=[],
        coverage_note='Real CPS/CSC bytes through production ingress and FTMS control. Timing stress only; no Peloton cable, radio, or ESP32 scheduler emulation.')
    identification = profile.get('identification', {})
    bounds = identification.get('training_position_range'); cad_bounds = identification.get('training_cadence_range')
    if bounds and cad_bounds:
        outside = (trace[:,4] < bounds[0]) | (trace[:,4] > bounds[1]) | (trace[:,3] < cad_bounds[0]) | (trace[:,3] > cad_bounds[1])
        meta['plant_fit_support'] = {'training_position_range': bounds, 'training_cadence_range': cad_bounds,
            'outside_recorded_fit_range_s': float(np.sum(np.diff(np.r_[trace[:,0],DURATION_S])[outside]))}
    score = summarize(trace, DURATION_S, workout)
    delivered = trace.copy(); delivered[:,2] = trace[:,14]
    score['delivered_sensor_metrics'] = summarize(delivered, DURATION_S, workout)
    score['windows'] = window_summary(trace, PREFIX, events, snapshots)
    target = workout.at(390)[0]
    diagnostic = response_summary(trace, target, snapshots)
    audit_diagnostics(trace, diagnostic)
    for name, value in [('metadata', meta), ('score', score), ('diagnostic', diagnostic),
                        ('learning_snapshots', snapshots), ('learning_events', events), ('ftms_control_packets', controls)]:
        frozen.write_json(dest/(name+'.json'), value)
    assert meta['packet_ingress_evidence']['processed'] > 0
    assert controls and any(e['payload_hex'].startswith('05') for e in controls)
    return trace, meta, score, diagnostic


def verify_pair(meta, traces):
    for key in ('initial_table_sha256', 'cadence_plan_sha256', 'sensor_schedules', 'sensor_counts',
                'seed', 'sensitivity', 'tick_ms', 'simulation_seconds', 'initial_position_steps'):
        assert meta['baseline'][key] == meta['candidate'][key], key
    assert np.array_equal(traces['baseline'][:,[0,1,3]], traces['candidate'][:,[0,1,3]])


def source_patch(root):
    """Keep patch context bytes intact, including a final blank context line."""
    return subprocess.check_output(['git', '-C', str(root), 'diff', '--binary', 'HEAD'])


class NumericalAuditTests(unittest.TestCase):
    def trace(self):
        x = np.zeros((100,15)); x[:,0] = np.arange(100)/10; x[:,2] = x[:,14] = 500
        return x

    def test_patch_round_trips_final_blank_context(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q', directory], check=True)
            original = b'first\nsecond\n\n'
            changed = b'changed\nsecond\n\n'
            (root/'example.txt').write_bytes(original)
            subprocess.run(['git', '-C', directory, 'add', 'example.txt'], check=True)
            subprocess.run(['git', '-C', directory, '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                            'commit', '-qm', 'fixture'], check=True)
            (root/'example.txt').write_bytes(changed)
            patch = source_patch(root)
            self.assertTrue(patch.endswith(b' \n'))
            (root/'example.txt').write_bytes(original)
            subprocess.run(['git', '-C', directory, 'apply', '--check', '-'], input=patch, check=True)
            subprocess.run(['git', '-C', directory, 'apply', '-'], input=patch, check=True)
            self.assertEqual((root/'example.txt').read_bytes(), changed)

    def test_exact_target_tail(self):
        x=self.trace(); r=phase_response(x,500,0,10)
        audit_phase(x,r,2); close(r['bands']['10']['final_settling_s'],0)

    def test_unreached_is_null(self):
        x=self.trace(); x[:,2]=400; r=phase_response(x,500,0,10)
        audit_phase(x,r,2); assert r['bands']['10']['final_settling_s'] is None
        assert r['rise_to_90pct_target_s'] is None

    def test_later_excursion_invalidates_final_tail(self):
        x=self.trace(); x[70:72,2]=511; r=phase_response(x,500,0,10)
        audit_phase(x,r,2); close(r['bands']['10']['first_dwell_entry_s'],0)
        assert r['bands']['10']['final_settling_s'] is None
        close(r['bands']['10']['later_excursion_s'],.2)
        close(r['bands']['20']['final_settling_s'],0)

    def test_delivered_scored_separately(self):
        x=self.trace(); x[:,14]=480
        a=phase_response(x,500,0,10); b=phase_response(x,500,0,10,column=14)
        audit_phase(x,a,2); audit_phase(x,b,14)
        assert a['goal']['met'] and not b['goal']['met']

    def test_exact_existing_schedules(self):
        assert asdict(REGIMES['slow_power'][0]) == {'period_s':2,'latency_s':.35,'jitter_s':.1,'phase_s':0.,'dropouts':()}
        assert REGIMES['fast_power_slow_cadence'][1].period_s == 2
        assert REGIMES['fast_power_slow_cadence'][1].latency_s == .4


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--candidate-root',type=Path)
    p.add_argument('--additional-candidate-path',action='append',default=[])
    p.add_argument('--output',type=Path)
    p.add_argument('--self-test',action='store_true')
    p.add_argument('--dry-run',action='store_true')
    a=p.parse_args(argv)
    tests=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(NumericalAuditTests))
    if not tests.wasSuccessful(): return 2
    if a.self_test: return 0
    if a.candidate_root is None or a.output is None: p.error('Need --candidate-root and --output')
    if not a.candidate_root.resolve().name.startswith('trial-'): raise ValueError('Use a frozen trial source, not the live checkout')
    selection,roots,sources,adapter,ainfo,profile,config,fixture=frozen.preflight(a.candidate_root, a.additional_candidate_path)
    if a.output.exists() and any(a.output.iterdir()): raise ValueError('Output must be new/empty')
    a.output.mkdir(parents=True,exist_ok=True)
    helper_hashes={str(Path(__file__).resolve()):frozen.sha256(__file__),str(Path(frozen.__file__).resolve()):frozen.sha256(frozen.__file__)}
    for path in helper_hashes:
        shutil.copy2(path,a.output/(Path(path).stem+'.frozen.py'))
    cases=[{'regime':regime,'initial_state':state,'sensitivity':sensitivity,'target_w':target,'seed':7}
        for regime in REGIMES for state in ('empty','full') for sensitivity in (3,5,10) for target in (500,700,1000)]
    workouts={target:diagnostic_workout(target) for target in (500,700,1000)}
    patch=source_patch(roots['candidate'])
    (a.output/'candidate-source.patch').write_bytes(patch)
    manifest={'status':'planned' if a.dry_run else 'incomplete','experiment':EXPERIMENT,
        'runner_sha256':frozen.sha256(__file__),'provenance_helper_sha256':frozen.sha256(frozen.__file__),
        'candidate_patch_sha256':frozen.sha256(a.output/'candidate-source.patch'),
        'baseline_selection':selection,'baseline_selection_file_sha256':frozen.SELECTION_FILE_SHA,
        'firmware':sources,'adapter':ainfo,'simulator_sha256':frozen.SIMULATOR_PACKAGE_SHA,
        'simulator_tree':frozen.SIMULATOR_TREE,'profile_sha256':frozen.PROFILE_SHA,
        'bike':asdict(config),'archived_full_fixture':fixture,'duration_s':DURATION_S,
        'regimes':{name:{'power':asdict(pair[0]),'cadence':asdict(pair[1])} for name,pair in REGIMES.items()},
        'requested_cases':cases,'absolute_budgets':DEFAULT_BUDGET,'relative_allowances':RELATIVE_ALLOWANCE,
        'workouts':{str(t):{'segments':[asdict(s) for s in w.segments],
            'sha256':digest([asdict(s) for s in w.segments]),'cadence_plan_sha256':CadencePlan(w).sha256} for t,w in workouts.items()},
        'goal':{'band_w':10,'dwell_s':5,'settling_limit_s':10},
        'coverage':'CPS/CSC packet ingestion, production FTMS, virtual motor. Independent timing stress, not Peloton cable emulation.',
        'limitations':['Original fitted plant and noise unchanged; extrapolated high-position behavior is not hardware validation.',
            '1000 W exceeds the 870 W table axis; original full fixture is unchanged and does not imply high-target coverage.',
            'Existing strict phase-response and low-power budgets are unchanged. Unachieved settling is JSON null.',
            'Slow-power period exceeds firmware1.5s freshness, deliberately exercising stale-response handling.'],
        'numerical_audit_tests':tests.testsRun,'helper_hashes':helper_hashes,'additional_candidate_paths':a.additional_candidate_path,'runs':[]}
    report=a.output/'timing_stress.json'; frozen.write_json(report,manifest)
    if a.dry_run: return 0
    try:
        exes={v:adapter.build(a.output/v/'build',source_root=root,packet_ingress=True) for v,root in roots.items()}
        for v,root in roots.items(): assert source_provenance(root)==sources[v], 'Source changed during build'
        binary_hashes={v:frozen.sha256(exe) for v,exe in exes.items()}
        manifest['executable_sha256']=binary_hashes
        for case in cases:
            assert shutil.disk_usage(a.output).free >= 512*1024*1024, 'Less than512 MiB free'
            assert all(frozen.sha256(path)==digest for path,digest in helper_hashes.items()), 'Helper changed during run'
            assert all(source_provenance(root)==sources[v] for v,root in roots.items()), 'Source changed during run'
            assert all(frozen.sha256(exes[v])==digest for v,digest in binary_hashes.items()), 'Executable changed during run'
            name=f'{case["regime"]}_{case["target_w"]}w_{case["initial_state"]}_sensitivity{case["sensitivity"]}-7'
            traces={}; metadata={}; scores={}; diagnostics={}
            table=fixture['table'] if case['initial_state']=='full' else None
            for v in roots:
                traces[v],metadata[v],scores[v],diagnostics[v]=run_case(adapter,exes[v],config,profile,a.output/v/name,
                    workouts[case['target_w']],table,case['sensitivity'],case['regime'])
            verify_pair(metadata,traces)
            for trace in traces.values():
                assert trace.shape==(22250,15) and np.isfinite(trace).all(), 'Incomplete/nonfinite trace'
                assert np.array_equal(trace[:,0],np.arange(22250)*20/1000), 'Independent tick mismatch'
                assert np.array_equal(trace[:,1],np.array([workouts[case['target_w']].at(t)[0] for t in trace[:,0]])), 'Workout target mismatch'
            low={signal:compare(scores['candidate']['windows'][0][key],scores['baseline']['windows'][0][key])
                 for signal,key in (('effective_plant','metrics'),('delivered_sensor','delivered_sensor_metrics'))}
            row={**case,'case':name,'metadata':metadata,'scores':scores,'diagnostics':diagnostics,
                'low_power_regression':low,'prefix_trace_identical':bool(np.array_equal(traces['baseline'][traces['baseline'][:,0]<390],traces['candidate'][traces['candidate'][:,0]<390]))}
            for kind in ('passed','relative_passed','absolute_passed'):
                row['low_power_'+kind]=all(value[kind] for value in low.values())
            manifest['runs'].append(row); frozen.write_json(report,manifest)
            peak=diagnostics['candidate']['signals']['effective_plant']['sprint']['peak_power_w']
            print(f'{name}: peak={peak:.1f}W, low-relative={row["low_power_relative_passed"]}',flush=True)
        for v,root in roots.items(): assert source_provenance(root)==sources[v], 'Source changed during matrix'
        frozen.preflight(a.candidate_root, a.additional_candidate_path)
        manifest['status']='complete'
        for kind in ('passed','relative_passed','absolute_passed'):
            manifest['low_power_'+kind]=all(r['low_power_'+kind] for r in manifest['runs'])
    except BaseException as exc:
        manifest['status']='failed';manifest['error']=str(exc);raise
    finally: frozen.write_json(report,manifest)
    return 0 if manifest['low_power_passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())
