#!/usr/bin/env python3
"""Paired cadence validation for an explicitly selected frozen candidate.

The original cadence_stress.py and its O artifacts stay untouched. This helper
reuses its exact 445-second workouts, 65/105 RPM cues, motion diagnostics and
fixed performance policy. --dry-run records a complete plan without builds or
rides. Outputs must be new and outside source repositories or archived runs.
"""
import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'simulator'))
import numpy as np
import cadence_stress as original
import extended_high_power as frozen
import timing_stress as timing
from ss2k_sim.adapter import source_provenance
from ss2k_sim.cadence import CadencePlan, PlannedRider
from ss2k_sim.high_power import phase_response, response_summary
from ss2k_sim.longride import digest, run_one
from ss2k_sim.performance import DEFAULT_BUDGET, RELATIVE_ALLOWANCE, compare

EXPERIMENT = 'local-frozen-candidate-cadence-stress-v1'
DURATION_S = original.DURATION_S
TICK_MS = 20
REPORT = 'candidate_cadence_stress.json'
ARCHIVED_REPORTS = (REPORT, 'cadence_stress.json', 'high_power.json',
                    'extended_high_power.json', 'timing_stress.json', 'longride.json')
COVERAGE = ('Real CPS/CSC packet bytes and production FTMS/native controller; '
            'no Peloton cable windows, radio or ESP32 scheduler emulation.')
REQUIRED_ARTIFACTS = ('trace.csv', 'metadata.json', 'score.json', 'learning_snapshots.json',
                      'learning_events.json', 'power_table.json', 'sensor_packets.csv',
                      'firmware.log', 'ftms_control_packets.json', 'cadence.csv', 'cadence_plan.json',
                      'diagnostic.json', 'motion.json', 'validation.json')
CONTROL_ARTIFACTS = tuple(name for name in REQUIRED_ARTIFACTS if name != 'validation.json') + (
    'prefix_audit.json', 'learned.ptab', 'power_table.csv')


def json_value(value):
    return json.loads(json.dumps(value, allow_nan=False))


def validate_output(output, roots):
    """Do not put build products in source trees or append to archived matrices."""
    output = output.resolve()
    for root in (*roots.values(), frozen.SIMULATOR, ROOT / 'adapter'):
        if output == root.resolve() or root.resolve() in output.parents:
            raise ValueError('Output must be outside all source repositories')
    for parent in (output, *output.parents):
        # Sandbox permission sentinels can be empty .git directories. An actual
        # checkout has HEAD, or a .git indirection file for a linked worktree.
        if (parent / '.git').is_file() or (parent / '.git' / 'HEAD').is_file():
            raise ValueError('Output must be outside any Git checkout')
        if any((parent / name).is_file() for name in ARCHIVED_REPORTS):
            raise ValueError('Output cannot replace or nest inside an archived experiment')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError('Output must be new/empty')
    return output


def source_patch(root):
    """Preserve binary patch bytes, including their final newline."""
    return subprocess.check_output(['git', '-C', str(root), 'diff', '--binary', 'HEAD'])


def capture_source(roots):
    patch = source_patch(roots['candidate'])
    changed = frozen.git(roots['candidate'], 'diff', '--name-only', 'HEAD').splitlines()
    files = {}
    for name in changed:
        path = roots['candidate'] / name
        files[name] = {'present': path.is_file(), 'sha256': frozen.sha256(path) if path.is_file() else None}
    return {'firmware': {v: source_provenance(root) for v, root in roots.items()},
            'candidate_head_tree': frozen.git(roots['candidate'], 'rev-parse', 'HEAD^{tree}'),
            'candidate_changed_paths': changed, 'candidate_changed_file_hashes': files,
            'candidate_git_status': frozen.git(roots['candidate'], 'status', '--porcelain'),
            'candidate_patch_sha256': hashlib.sha256(patch).hexdigest()}, patch


def helper_sources():
    return {path.name: {'path': str(path.resolve()), 'sha256': frozen.sha256(path)}
            for path in (Path(__file__), Path(original.__file__), Path(frozen.__file__), Path(timing.__file__))}


def verify_frozen_inputs(candidate, extra_paths, roots, snapshot, helpers):
    frozen.preflight(candidate, additional_candidate_paths=extra_paths)
    current, _ = capture_source(roots)
    if current != snapshot:
        raise ValueError('Candidate/baseline source, selected test files or patch changed after planning')
    if helper_sources() != helpers:
        raise ValueError('Outside-repository validation helper changed after planning')


def expected_inputs(workout):
    """The independent clock, targets and unchanged time-only rider dynamics."""
    times = np.arange(DURATION_S * 1000 // TICK_MS) * TICK_MS / 1000
    targets = np.array([workout.at(now)[0] for now in times])
    rider = PlannedRider(workout)
    cadence = np.empty(len(times))
    for index, now in enumerate(times):
        cadence[index] = rider.cadence
        rider.advance(now + TICK_MS / 1000, TICK_MS / 1000)
    return np.column_stack((times, targets, cadence))


def validate_run(trace, meta, snapshots, case, workout, config, profile, fixture, expected):
    if trace.shape != (len(expected), 15) or not np.isfinite(trace).all():
        raise ValueError('Run must have every finite 20 ms sample and all 15 columns')
    if not np.array_equal(trace[:, [0, 1, 3]], expected):
        raise ValueError('Actual time/target/cadence differs from unchanged independent input plan')
    required = {'seed': case['seed'], 'sensitivity': case['sensitivity'], 'tick_ms': TICK_MS,
                'simulation_seconds': DURATION_S, 'sensor_schedules': original.EXPECTED_SCHEDULES,
                'pause_events': [], 'connection_events': [], 'controller_processes': 1,
                'state_resets_during_ride': 0, 'initial_position_steps': profile.get('start_position', 7433),
                'cadence_plan_sha256': CadencePlan(workout).sha256,
                'physical_travel_steps': [config.min_position, config.max_position],
                'homing': 'homed', 'requested_speed': 0, 'cadence_source': 'fixed interval plan'}
    for key, value in required.items():
        if meta.get(key) != value:
            raise ValueError('Run input metadata differs from frozen case: ' + key)
    ingress = meta.get('packet_ingress_evidence') or {}
    if not ingress.get('processed', 0) or ingress.get('rejected') != 0 or ingress.get('queue_dropped') != 0:
        raise ValueError('Run lacks complete CPS/CSC ingress evidence')
    if not snapshots or snapshots[0]['time_s'] != 0 or snapshots[-1]['time_s'] != DURATION_S:
        raise ValueError('Run lacks initial/final table snapshots')
    for time in (390, 415):
        if sum(snapshot['time_s'] == time for snapshot in snapshots) != 1:
            raise ValueError('Run lacks a unique phase-entry table snapshot')
    initial = snapshots[0]['table']
    if digest(initial) != meta['initial_table_sha256']:
        raise ValueError('Initial table and metadata digest differ')
    if case['initial_state'] == 'full' and initial != fixture['table']:
        raise ValueError('Run did not start from the unchanged archived full fixture')
    if case['initial_state'] == 'empty' and any(position != -32768 or count != 0 for position, count in initial['cells']):
        raise ValueError('Run did not start from an empty table')
    return {'rows': len(trace), 'all_values_finite': True, 'every_tick_verified': True,
            'targets_match_workout': True, 'cadence_matches_original_rider': True,
            'requested_initial_state_verified': True, 'packet_ingress_verified': True}


def diagnostics(trace, target, snapshots):
    result = response_summary(trace, target, snapshots)
    timing.audit_diagnostics(trace, result)
    # response_summary covers all six low-power prefix phases plus sprint/recovery.
    # Include the unchanged 360..390 s preparation phase as well.
    for signal, column in (('effective_plant', 2), ('delivered_sensor', 14)):
        preparation = phase_response(trace, 100, 360, 390, column=column)
        preparation.pop('rise_to_90pct_target_s')
        preparation.pop('rise_threshold_w')
        timing.audit_phase(trace, preparation, column)
        result['signals'][signal]['preparation'] = preparation
    table = next(snapshot['table'] for snapshot in snapshots if snapshot['time_s'] == 390)
    result['sprint_start_table_support'] = original.cadence_support(
        table, target, trace[(trace[:, 0] >= 390) & (trace[:, 0] < 415), 3])
    result['phase_metrics_source'] = ('Frozen ss2k_sim.high_power.phase_response: both power signals, '
                                      '10/20 W bands, five-second dwell, explicit null unachieved final tails')
    return result


def prefix_comparison(traces, entry_tables):
    """Differences are evidence, never a substitute for fixed performance budgets."""
    parts = {v: trace[trace[:, 0] < 390] for v, trace in traces.items()}
    hashes = {v: original.prefix_fingerprint(trace) for v, trace in traces.items()}
    before, after = parts['baseline'], parts['candidate']
    mismatch = before != after
    changed_rows = np.flatnonzero(mismatch.any(axis=1))
    btable, ctable = entry_tables['baseline'], entry_tables['candidate']
    table_structure = {v: {key: value for key, value in table.items() if key != 'cells'}
                       for v, table in entry_tables.items()}
    comparable = table_structure['baseline'] == table_structure['candidate']
    changed_cells = (sum(b != c for b, c in zip(btable['cells'], ctable['cells']))
                     if comparable and len(btable['cells']) == len(ctable['cells']) else None)
    return {'start_s': 0, 'end_s': 390, 'rows': len(before), 'all_columns_sha256': hashes,
            'trace_identical': hashes['baseline'] == hashes['candidate'],
            'different_rows': len(changed_rows),
            'first_difference_s': float(before[changed_rows[0], 0]) if len(changed_rows) else None,
            'changed_columns': [int(index) for index in np.flatnonzero(mismatch.any(axis=0))],
            'max_abs_difference_by_column': [float(value) for value in np.max(np.abs(before - after), axis=0)],
            'sprint_entry_table_sha256': {v: digest(table) for v, table in entry_tables.items()},
            'sprint_entry_table_identical': btable == ctable,
            'table_structure_identical': comparable, 'different_table_cells': changed_cells,
            'meaning': ('Actual baseline/candidate output and learning differences are reported. '
                        'Only paired external inputs must be identical; fixed low-power budgets remain unchanged.')}


def low_power_comparison(scores):
    for variant in ('baseline', 'candidate'):
        windows = scores[variant]['windows']
        if len(windows) != 1 or any(windows[0].get(key) != value for key, value in original.PREFIX[0].items()):
            raise ValueError('Low-power comparison must use the unchanged 0..360 s prefix')
    low = {signal: compare(scores['candidate']['windows'][0][key], scores['baseline']['windows'][0][key])
           for signal, key in (('effective_plant', 'metrics'), ('delivered_sensor', 'delivered_sensor_metrics'))}
    return {'signals': low, **{key: all(value[key] for value in low.values())
                              for key in ('passed', 'relative_passed', 'absolute_passed')}}


def case_name(case):
    return (f'cadence{case["sprint_cadence_cue_rpm"]}_{case["target_w"]}w_'
            f'{case["initial_state"]}_sensitivity{case["sensitivity"]}-{case["seed"]}')


def plan_control(origin, cases, manifest):
    """Pin existing O evidence read-only; a control never creates another ride."""
    origin = origin.resolve()
    path = origin / 'cadence_stress.json'
    prior = json.loads(path.read_text())
    if prior.get('status') != 'complete' or prior.get('experiment') != original.EXPERIMENT:
        raise ValueError('Archived control must be a complete original cadence matrix')
    for key in ('baseline_selection', 'baseline_selection_file_sha256', 'adapter', 'simulator_tree',
                'simulator_sha256', 'profile_sha256', 'archived_full_fixture', 'archived_full_fixture_file_sha256',
                'bike', 'duration_s', 'normal_sensor_schedules', 'absolute_budgets', 'relative_allowances', 'goal'):
        if prior.get(key) != json_value(manifest[key]):
            raise ValueError('Archived control frozen-input mismatch: ' + key)
    if prior['firmware']['baseline'] != manifest['firmware']['baseline']:
        raise ValueError('Archived control baseline source differs')
    if prior.get('runner_sha256') != frozen.sha256(original.__file__):
        raise ValueError('Archived control runner differs from the immutable original cadence runner')
    patch = origin / 'candidate-source.patch'
    if frozen.sha256(patch) != prior['candidate_patch_sha256']:
        raise ValueError('Archived control patch differs from its manifest')
    planned = {}
    for case in cases:
        matches = [row for row in prior['runs'] if all(row.get(key) == value for key, value in case.items())]
        if len(matches) != 1 or case not in prior['requested_cases']:
            raise ValueError('Archived control lacks one complete matching case: ' + case_name(case))
        name = case_name(case)
        row = matches[0]
        if row['case'] != name:
            raise ValueError('Archived control case directory does not match selected inputs')
        workout_key = f'{case["target_w"]}w_{case["sprint_cadence_cue_rpm"]}rpm'
        for key in ('segments', 'cadence_plan_sha256'):
            if prior['workouts'][workout_key][key] != manifest['workouts'][workout_key][key]:
                raise ValueError('Archived control workout differs: ' + key)
        dest = origin / 'candidate' / name
        planned[name] = {'path': str(dest), 'files': {filename: frozen.sha256(dest / filename)
                                                   for filename in CONTROL_ARTIFACTS}}
    return {'path': str(origin), 'manifest_sha256': frozen.sha256(path),
            'candidate_patch_sha256': prior['candidate_patch_sha256'], 'runner_sha256': prior['runner_sha256'],
            'firmware': prior['firmware'], 'cases': planned,
            'meaning': 'Read-only archived trial-O outputs; no O build, controller execution or artifact changes.'}


def verify_control_pins(control):
    origin = Path(control['path'])
    if frozen.sha256(origin / 'cadence_stress.json') != control['manifest_sha256']:
        raise ValueError('Archived control manifest changed after planning')
    if frozen.sha256(origin / 'candidate-source.patch') != control['candidate_patch_sha256']:
        raise ValueError('Archived control patch changed after planning')
    for entry in control['cases'].values():
        for name, expected in entry['files'].items():
            if frozen.sha256(Path(entry['path']) / name) != expected:
                raise ValueError('Archived control artifact changed after planning: ' + name)


def compare_control(control, case, workout, config, profile, fixture, traces, metadata, scores, tables):
    """Verify historical outputs, then compare candidate with O descriptively."""
    verify_control_pins(control)
    name = case_name(case)
    dest = Path(control['cases'][name]['path'])
    prior = json.loads((Path(control['path']) / 'cadence_stress.json').read_text())
    row = next(row for row in prior['runs'] if row['case'] == name)
    stored = {key: json.loads((dest / (key + '.json')).read_text())
              for key in ('metadata', 'score', 'diagnostic', 'learning_snapshots', 'power_table', 'motion')}
    for key, manifest_key in (('metadata', 'metadata'), ('score', 'scores'), ('diagnostic', 'diagnostics'), ('motion', 'motion')):
        if stored[key] != row[manifest_key]['candidate']:
            raise ValueError('Archived control artifact differs from its completed manifest: ' + key)
    trace = np.loadtxt(dest / 'trace.csv', delimiter=',', skiprows=1)
    validation = validate_run(trace, stored['metadata'], stored['learning_snapshots'], case, workout,
                              config, profile, fixture, expected_inputs(workout))
    if stored['power_table'] != stored['learning_snapshots'][-1]['table']:
        raise ValueError('Archived control final table differs from final snapshot')
    diag = diagnostics(trace, case['target_w'], stored['learning_snapshots'])
    for signal in ('effective_plant', 'delivered_sensor'):
        for section in ('low_power_prefix', 'sprint', 'recovery'):
            if diag['signals'][signal][section] != stored['diagnostic']['signals'][signal][section]:
                raise ValueError('Archived control phase metrics do not reproduce from its trace')
    paired = {'baseline': stored['metadata'], 'candidate': metadata['candidate']}
    frozen.verify_pair(paired, {'baseline': trace, 'candidate': traces['candidate']})
    entry_table = next(s['table'] for s in stored['learning_snapshots'] if s['time_s'] == 390)
    prefix = prefix_comparison({'baseline': trace, 'candidate': traces['candidate']},
                               {'baseline': entry_table, 'candidate': tables['candidate']})
    return {'control_artifacts': control['cases'][name], 'validation': validation,
            'control_diagnostics': diag, 'control_motion': stored['motion'],
            'prefix_comparison': prefix,
            'low_power_regression': low_power_comparison({'baseline': stored['score'], 'candidate': scores['candidate']}),
            'meaning': 'Here baseline labels refer to archived O; the main paired comparison remains clean develop.'}


def artifact_manifest(dest, snapshots):
    missing = [name for name in REQUIRED_ARTIFACTS if not (dest / name).is_file()]
    if missing:
        raise ValueError('Run artifacts missing: ' + ', '.join(missing))
    if json.loads((dest / 'learning_snapshots.json').read_text()) != snapshots:
        raise ValueError('Persisted snapshots differ from scored snapshots')
    if json.loads((dest / 'power_table.json').read_text()) != snapshots[-1]['table']:
        raise ValueError('Final table differs from final snapshot')
    controls = json.loads((dest / 'ftms_control_packets.json').read_text())
    if not any(event['payload_hex'].startswith('05') for event in controls):
        raise ValueError('Run lacks production FTMS target-power control evidence')
    return {'path': str(dest.resolve()),
            'files': {str(path.relative_to(dest)): {'sha256': frozen.sha256(path), 'bytes': path.stat().st_size}
                      for path in sorted(dest.rglob('*')) if path.is_file()}}


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--candidate-root', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--targets', type=int, nargs='+', choices=(500, 700, 1000), default=[500, 700, 1000])
    p.add_argument('--states', nargs='+', choices=('empty', 'full'), default=['empty', 'full'])
    p.add_argument('--sensitivities', type=int, nargs='+', choices=(3, 5, 10), default=[3, 5, 10])
    p.add_argument('--seeds', type=int, nargs='+', default=[7])
    p.add_argument('--cadences', type=int, nargs='+', choices=(65, 105), default=[65, 105])
    p.add_argument('--control-from', type=Path,
                   help='Read-only original O cadence matrix for matching historical comparisons; no O rerun')
    p.add_argument('--additional-candidate-path', action='append', default=[],
                   help='Explicit extra changed source/test path allowed by shared frozen preflight; repeat as needed')
    p.add_argument('--dry-run', action='store_true', help='Validate provenance and save plan; no build or ride')
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    for values in (args.targets, args.states, args.sensitivities, args.seeds, args.cadences,
                   args.additional_candidate_path):
        if len(values) != len(set(values)):
            raise ValueError('Duplicate matrix values or selected paths are not allowed')
    if any(seed < 0 for seed in args.seeds):
        raise ValueError('Seeds must be nonnegative integers')
    extra = tuple(args.additional_candidate_path)
    selection, roots, sources, adapter, ainfo, profile, config, fixture = frozen.preflight(
        args.candidate_root, additional_candidate_paths=extra)
    output = validate_output(args.output, roots)
    workouts = {(target, rpm): original.cadence_workout(target, rpm)
                for target in args.targets for rpm in args.cadences}
    cases = [{'target_w': target, 'initial_state': state, 'sensitivity': sensitivity,
              'seed': seed, 'sprint_cadence_cue_rpm': rpm}
             for rpm in args.cadences for state in args.states for sensitivity in args.sensitivities
             for target in args.targets for seed in args.seeds]
    source, patch = capture_source(roots)
    if source['firmware'] != sources:
        raise ValueError('Source changed between preflight and exact patch capture')
    helpers = helper_sources()
    output.mkdir(parents=True, exist_ok=True)
    initial_free = original.guard_disk(output)
    (output / 'candidate-source.patch').write_bytes(patch)
    for filename, entry in helpers.items():
        (output / (filename + '.snapshot')).write_bytes(Path(entry['path']).read_bytes())
    manifest = {'status': 'planned' if args.dry_run else 'incomplete', 'experiment': EXPERIMENT,
                'runner_sha256': helpers[Path(__file__).name]['sha256'], 'helper_sources': helpers,
                **source, 'additional_candidate_paths': list(extra),
                'baseline_selection': selection, 'baseline_selection_file_sha256': frozen.SELECTION_FILE_SHA,
                'adapter': ainfo, 'simulator_commit': frozen.git(frozen.SIMULATOR, 'rev-parse', 'HEAD'),
                'simulator_tree': frozen.SIMULATOR_TREE, 'simulator_sha256': frozen.SIMULATOR_PACKAGE_SHA,
                'profile_sha256': frozen.PROFILE_SHA, 'bike': asdict(config),
                'archived_full_fixture': fixture, 'archived_full_fixture_file_sha256': frozen.FIXTURE_FILE_SHA,
                'duration_s': DURATION_S, 'tick_ms': TICK_MS, 'normal_sensor_schedules': original.EXPECTED_SCHEDULES,
                'requested_cases': cases, 'requested_paired_cases': len(cases), 'completed_paired_cases': 0,
                'absolute_budgets': DEFAULT_BUDGET, 'relative_allowances': RELATIVE_ALLOWANCE,
                'goal': {'band_w': 10, 'dwell_s': 5, 'settling_limit_s': 10, 'strictly_less_than': True},
                'workouts': {f'{target}w_{rpm}rpm': {'segments': [asdict(s) for s in workout.segments],
                            'workout_sha256': digest([asdict(s) for s in workout.segments]),
                            'cadence_plan_sha256': CadencePlan(workout).sha256}
                            for (target, rpm), workout in workouts.items()},
                'disk_guard_bytes': original.MIN_FREE_BYTES, 'disk_initial_free_bytes': initial_free,
                'coverage': COVERAGE,
                'limitations': [COVERAGE,
                    'High-power/cadence fitted-model extrapolation is not validated hardware capability.',
                    'Only the original 25-second sprint cadence cue changes; actual RPM uses unchanged PlannedRider dynamics.',
                    '1000 W exceeds the 870 W table axis; archived full does not imply high-target support.',
                    'Full fixture RAM import does not exercise flash persistence; the original fixture is unchanged.',
                    'Actual prefix and learned-table differences are reported, not assumed to be regressions.',
                    'Fixed low-prefix performance budgets and strict final-tail goal are unchanged; null means unachieved.',
                    'Short 445-second cadence cases do not establish middle/late long-ride performance.'],
                'reference_policy': 'Paired clean baseline is the reference; no requirement for matching historical trial-O outputs.',
                'runs': [], 'completed_variants': [], 'active_variant': None}
    if args.control_from:
        manifest['archived_control'] = plan_control(args.control_from, cases, manifest)
    report = output / REPORT
    frozen.write_json(report, manifest)
    if args.dry_run:
        verify_frozen_inputs(args.candidate_root, extra, roots, source, helpers)
        print(f'Validated {len(cases)} paired cadence cases; no native build or ride executed: {report}')
        return 0
    try:
        exes = {}
        for variant, root in roots.items():
            original.guard_disk(output)
            verify_frozen_inputs(args.candidate_root, extra, roots, source, helpers)
            exes[variant] = adapter.build(output / variant / 'build', source_root=root, packet_ingress=True)
        manifest['executables'] = {v: {'path': str(path.resolve()), 'sha256': frozen.sha256(path)}
                                   for v, path in exes.items()}
        frozen.write_json(report, manifest)
        for case in cases:
            name = case_name(case)
            workout = workouts[(case['target_w'], case['sprint_cadence_cue_rpm'])]
            expected = expected_inputs(workout)
            traces, metadata, scores, reports, motion, tables, artifacts = {}, {}, {}, {}, {}, {}, {}
            for variant in roots:
                original.guard_disk(output)
                verify_frozen_inputs(args.candidate_root, extra, roots, source, helpers)
                if frozen.sha256(exes[variant]) != manifest['executables'][variant]['sha256']:
                    raise ValueError('Compiled binary changed after build: ' + variant)
                manifest['active_variant'] = {'case': name, 'variant': variant}
                frozen.write_json(report, manifest)
                dest = output / variant / name
                trace, meta, score, snapshots = run_one(adapter, exes[variant], config, profile, dest,
                    workout, case['seed'], table=fixture['table'] if case['initial_state'] == 'full' else None,
                    windows=original.PREFIX, sensitivity=case['sensitivity'], extra_snapshot_times=(390, 415))
                meta = json_value(meta)
                validation = validate_run(trace, meta, snapshots, case, workout, config, profile, fixture, expected)
                diagnostic = diagnostics(trace, case['target_w'], snapshots)
                movement = original.motion_diagnostics(trace, config, profile)
                meta.update(scenario_version=EXPERIMENT, sprint_cadence_cue_rpm=case['sprint_cadence_cue_rpm'],
                            diagnostic_sprint_target_w=case['target_w'], coverage_note=COVERAGE)
                for key, value in (('metadata', meta), ('diagnostic', diagnostic), ('motion', movement), ('validation', validation)):
                    frozen.write_json(dest / (key + '.json'), value)
                artifacts[variant] = artifact_manifest(dest, snapshots)
                traces[variant], metadata[variant], scores[variant] = trace, meta, score
                reports[variant], motion[variant] = diagnostic, movement
                tables[variant] = next(s['table'] for s in snapshots if s['time_s'] == 390)
                manifest['completed_variants'].append({'case': name, 'variant': variant, 'artifacts': artifacts[variant]})
                manifest['active_variant'] = None
                frozen.write_json(report, manifest)
            frozen.verify_pair(metadata, traces)
            if metadata['baseline']['sensor_counts'] != metadata['candidate']['sensor_counts']:
                raise ValueError('Paired sensor counts differ')
            verification = frozen.verify_pair_files(metadata, output / 'baseline' / name / 'trace.csv',
                                                    output / 'candidate' / name / 'trace.csv', workout)
            prefix = prefix_comparison(traces, tables)
            low = low_power_comparison(scores)
            row = {**case, 'case': name, 'metadata': metadata, 'scores': scores, 'diagnostics': reports,
                   'motion': motion, 'prefix_comparison': prefix, 'low_power_regression': low,
                   'artifacts': artifacts, 'paired_trace_verification': verification}
            if args.control_from:
                row['archived_control_comparison'] = compare_control(manifest['archived_control'], case, workout,
                    config, profile, fixture, traces, metadata, scores, tables)
            manifest['runs'].append(row)
            manifest['completed_paired_cases'] = len(manifest['runs'])
            manifest['disk_latest_free_bytes'] = original.guard_disk(output)
            frozen.write_json(report, manifest)
            print(f'{name}: low-prefix relative={low["relative_passed"]}, absolute={low["absolute_passed"]}; '
                  f'prefix-identical={prefix["trace_identical"]}, table-identical={prefix["sprint_entry_table_identical"]}', flush=True)
        verify_frozen_inputs(args.candidate_root, extra, roots, source, helpers)
        if args.control_from:
            verify_control_pins(manifest['archived_control'])
        for variant, exe in exes.items():
            if frozen.sha256(exe) != manifest['executables'][variant]['sha256']:
                raise ValueError('Compiled binary changed during matrix: ' + variant)
        manifest['status'] = 'complete'
        for key in ('passed', 'relative_passed', 'absolute_passed'):
            manifest['low_power_' + key] = all(row['low_power_regression'][key] for row in manifest['runs'])
        manifest['all_prefix_and_entry_tables_identical'] = all(
            row['prefix_comparison']['trace_identical'] and row['prefix_comparison']['sprint_entry_table_identical']
            for row in manifest['runs'])
    except BaseException as exc:
        manifest['status'] = 'incomplete'
        manifest['execution_status'] = 'interrupted' if isinstance(exc, KeyboardInterrupt) else 'failed'
        manifest['error'] = {'type': type(exc).__name__, 'message': str(exc)}
        raise
    finally:
        frozen.write_json(report, manifest)
    return 0 if manifest['low_power_passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
