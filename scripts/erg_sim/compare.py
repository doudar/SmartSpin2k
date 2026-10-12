"""Paired native controller experiments against an immutable develop revision.

python -m scripts.erg_sim.compare --baseline-root PATH --output test/output/paired
This is NOT the real-BLE bench gate; scripts.bike_sim.bench owns that boundary.
"""
import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import subprocess

from .bridge import ROOT, build
from .model import BikeConfig
from .runner import run
from .sampling import SensorSchedule
from .workout import Segment, Workout
from .performance import summarize, compare, DEFAULT_BUDGET

BASELINE_SHA = '082d1ee26ddb009d5132547087f77be03d1b0ab7'


def revision(root):
    return subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()


def fingerprint(root):
    files = sorted([*Path(root).glob('src/*'), *Path(root).glob('include/*'), *Path(root).glob('lib/SS2K/**/*'), *Path(root).glob('lib/ArduinoCompat/**/*')])
    digest = hashlib.sha256()
    for file in files:
        if file.is_file(): digest.update(str(file.relative_to(root)).encode()+b'\0'+file.read_bytes())
    return digest.hexdigest()


def scenarios():
    step = Workout([Segment(60, 150, 150, 85), Segment(60, 250, 250, 85),
                    Segment(60, 160, 160, 85), Segment(60, 180, 180, 85)])
    cadence = Workout([Segment(60, 200, 200, 90), Segment(60, 200, 200, 65),
                       Segment(60, 200, 200, 105), Segment(60, 200, 200, 85)])
    ramp = Workout([Segment(45, 120, 120, 85), Segment(90, 120, 300, 85),
                    Segment(45, 300, 300, 85), Segment(60, 300, 120, 85)])
    normal = SensorSchedule()
    return [
        ('steps', step, normal, SensorSchedule(.25)),
        ('ramp', ramp, normal, SensorSchedule(.25)),
        ('cadence_shift', cadence, normal, SensorSchedule(.25)),
        ('slow_power', step, SensorSchedule(2, .35, .1), SensorSchedule(.25, .1)),
        ('fast_power_slow_cadence', cadence, SensorSchedule(.25, .1), SensorSchedule(2, .4)),
        ('sensor_outage', step, SensorSchedule(1, .2, .1, dropouts=((90, 98),)),
         SensorSchedule(.5, .1, dropouts=((150, 158),))),
    ]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-root', type=Path, required=True)
    parser.add_argument('--candidate-root', type=Path, default=ROOT)
    parser.add_argument('--output', type=Path, default=ROOT/'test/output/paired')
    parser.add_argument('--seeds', type=int, nargs='+', default=[7, 27, 91])
    parser.add_argument('--scenario', action='append', help='Limit named scenarios for diagnosis; default all')
    args = parser.parse_args(argv)
    baseline_root, candidate_root = args.baseline_root.resolve(), args.candidate_root.resolve()
    if revision(baseline_root) != BASELINE_SHA:
        parser.error('Baseline checkout must match pinned develop '+BASELINE_SHA)
    if subprocess.check_output(['git', '-C', str(baseline_root), 'status', '--porcelain'], text=True).strip():
        parser.error('Baseline checkout must be clean')
    args.output.mkdir(parents=True, exist_ok=True)
    profile = json.loads((ROOT/'scripts/erg_sim/profiles/ride_20260927.json').read_text())
    cfg = BikeConfig(**profile['bike'])
    selected = [s for s in scenarios() if not args.scenario or s[0] in args.scenario]
    if not selected or args.scenario and set(args.scenario)-{s[0] for s in selected}: parser.error('Unknown scenario')
    manifest = {'coverage_level': 'BLE-packet firmware ingestion + FTMS control + virtual motor; no radio or ESP32 scheduler',
                'baseline_commit': BASELINE_SHA, 'candidate_commit': revision(candidate_root),
                'baseline_source_sha256': fingerprint(baseline_root), 'candidate_source_sha256': fingerprint(candidate_root),
                'bike': asdict(cfg), 'seeds': args.seeds, 'absolute_budgets': DEFAULT_BUDGET,
                'budget_status': 'Provisional engineering budgets fixed before runs; require rider acceptance, not empirically calibrated',
                'harness_sha256': hashlib.sha256(b''.join(str(p.relative_to(ROOT)).encode()+b'\0'+p.read_bytes() for p in sorted([*(ROOT/'scripts/erg_sim').glob('*.py'), *(ROOT/'scripts/erg_sim').glob('*.h'), *(ROOT/'scripts/erg_sim').glob('*.hpp'), *(ROOT/'scripts/erg_sim').glob('*.inc'), ROOT/'scripts/erg_sim/native.cpp', ROOT/'scripts/bike_sim/protocol.py']))).hexdigest(),
                'physical_fidelity': 'Effective fitted model; timing/dropouts are stress assumptions, not new calibration',
                'runs': []}
    exes = {name: build(args.output/name/'build', source_root=root, packet_ingress=True) for name, root in [('baseline', baseline_root), ('candidate', candidate_root)]}
    for name, workout, power, cadence in selected:
        for seed in args.seeds:
            scores = {}
            for variant in ('baseline', 'candidate'):
                output = args.output/variant/f'{name}-{seed}'
                trace, metadata = run(workout, cfg, exes[variant], output, seed=seed,
                                      initial_position=profile.get('start_position', 7433),
                                      power_schedule=power, cadence_schedule=cadence, packet_ingress=True)
                scores[variant] = summarize(trace, workout.duration, workout)
                reported_trace = trace.copy(); reported_trace[:, 2] = trace[:, 14]
                scores[variant]['delivered_sensor_metrics'] = summarize(reported_trace, workout.duration, workout)
                (output/'metadata.json').write_text(json.dumps(metadata, indent=2)+'\n')
                (output/'score.json').write_text(json.dumps(scores[variant], indent=2)+'\n')
            result = compare(scores['candidate'], scores['baseline'])
            reported = compare(scores['candidate']['delivered_sensor_metrics'], scores['baseline']['delivered_sensor_metrics'])
            result['reported_power_comparison'] = reported
            result['passed'] = result['passed'] and reported['passed']
            result['failures'] += ['delivered sensor: '+f for f in reported['failures']]
            manifest['runs'].append({'scenario': name, 'seed': seed, 'scores': scores, 'power': power.to_dict(), 'cadence': cadence.to_dict(), **result})
            print(f'{name} seed={seed}: '+('PASS' if result['passed'] else 'FAIL: '+'; '.join(result['failures'])), flush=True)
    manifest['passed'] = all(row['passed'] for row in manifest['runs'])
    (args.output/'comparison.json').write_text(json.dumps(manifest, indent=2)+'\n')
    return 0 if manifest['passed'] else 1


if __name__ == '__main__': raise SystemExit(main())
