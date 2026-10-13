#!/usr/bin/env python3
"""Local, frozen-source paired 90-minute high-power stress validation.

Only the original three 25-second 500 W sprint targets change. No repository
files are modified. The archived baseline selection is read, never re-resolved.
Use --dry-run to validate provenance and write the plan without building/riding.
"""
import argparse
import csv
import gc
import resource
import shutil
from dataclasses import asdict
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent
SIMULATOR = ROOT/'simulator'
sys.path.insert(0,str(SIMULATOR))
import numpy as np
from ss2k_sim import baseline
from ss2k_sim.adapter import file_fingerprint, load_adapter, source_provenance
from ss2k_sim.cadence import CadencePlan
from ss2k_sim.cli import PACKAGE
from ss2k_sim.high_power import load_fixture, phase_response, target_table_support
from ss2k_sim.longride import digest, long_workout, run_one, sprint_windows
from ss2k_sim.model import BikeConfig
from ss2k_sim.performance import DEFAULT_BUDGET, RELATIVE_ALLOWANCE, compare
from ss2k_sim.sampling import SensorSchedule
from ss2k_sim.workout import Segment, Workout

EXPERIMENT = 'local-extended-high-power-stress-v1'
BASELINE_SHA = '615fce5599f01fbe9459f59e924e7bb65c81a22b'
SIMULATOR_TREE = '362ec247af1067b3d6ea5972c4550380b9c44b70'
SIMULATOR_PACKAGE_SHA = 'c84f685fe6c49b682ea1a61ec226b535bbb645a29d101c234bce146a8e0c12b7'
ADAPTER_SHA = 'c0c4db673fe124753ca1379d8ab6d10576a973b6c519ed99b175afe7a5469d62'
FIXTURE_SHA = '5d7a6ac3a0c2dd77f01cae3a5e0b4902bdc4d8bb25a60649519b0da10fada31b'
FIXTURE_FILE_SHA = 'aa9e49c300817de7df1b93694f5933e19f4c7db3d274c8d3c601b5165552d3b2'
PROFILE_SHA = 'e4650c2aa1f59f8282207f5ac2c27dd6187265d2603defc3606edaa8d032dac8'
SELECTION_FILE_SHA = 'af822d0296a301ed833a2b5d7733ac6918f568b98d45bc8bee91231808e3183a'
DURATION_S = 5400
EXPLICIT_ADDITIONAL_CANDIDATE_PATHS = {'src/PowerTable_Helpers.cpp','test/test.h',
    'test/test_pt_lookup_resistance.cpp','test/test_unity.cpp','test/test_erg_feedback.py'}


def sha256(path):
    value=hashlib.sha256()
    with Path(path).open('rb') as handle:
        for chunk in iter(lambda:handle.read(1024*1024),b''):
            value.update(chunk)
    return value.hexdigest()


def git(root,*args):
    return subprocess.check_output(['git','-C',str(root),*args],text=True).strip()


def write_json(path,value):
    """Checkpoint atomically, so interrupted matrices retain valid JSON."""
    temp=path.with_suffix(path.suffix+'.tmp')
    with temp.open('w') as handle:
        json.dump(value,handle,indent=2,allow_nan=False)
        handle.write('\n')
    temp.replace(path)


def extended_workout(target_w):
    if target_w not in (500,700,1000):
        raise ValueError('Extended targets must be 500, 700 or 1000 W')
    original,windows=long_workout(90,stress=True)
    segments=[]; count=0
    for segment in original.segments:
        values=asdict(segment)
        if segment.start_w==500:
            if segment.end_w!=500 or segment.duration_s!=25 or segment.cadence_rpm!=85:
                raise ValueError('Unexpected original sprint geometry')
            values['start_w']=values['end_w']=target_w; count+=1
        segments.append(Segment(**values))
    if count!=3: raise ValueError('Expected exactly three original sprint segments')
    workout=Workout(segments)
    if workout.duration!=DURATION_S or CadencePlan(workout).sha256!=CadencePlan(original).sha256:
        raise ValueError('Modified duration or cadence plan')
    return workout,windows


def phase_plan(workout,target_w):
    """Every constant workout phase, with explicit previous target and role."""
    windows=sprint_windows(90); phases=[]; start=0.; previous=None
    for index,segment in enumerate(workout.segments):
        end=start+segment.duration_s
        if segment.start_w!=segment.end_w: raise ValueError('Expected constant workout phases')
        sprint=next((w for w in windows if start==w['sprint_start_s']),None)
        recovery=next((w for w in windows if start==w['recovery_start_s']),None)
        name=sprint['name']+'_sprint' if sprint else recovery['name']+'_recovery' if recovery else f'phase_{index:02d}'
        role='sprint' if sprint else 'recovery' if recovery else 'low_power'
        if sprint and (segment.start_w!=target_w or end!=sprint['recovery_start_s']):
            raise ValueError('Sprint phase no longer matches original windows')
        if recovery and (segment.start_w!=100 or end!=recovery['start_s']+85):
            raise ValueError('Recovery phase no longer matches original windows')
        if role=='low_power' and segment.start_w>240:
            raise ValueError('Unexpected non-sprint target above 240 W')
        phases.append({'name':name,'segment_index':index,'role':role,'start_s':start,'end_s':end,
            'duration_s':segment.duration_s,'target_w':segment.start_w,'previous_target_w':previous,
            'cadence_cue_rpm':segment.cadence_rpm,'contains_zero_cadence_pause':segment.cadence_rpm==0,
            'after_high_target':previous is not None and previous>240})
        start=end; previous=segment.end_w
    return phases


def run_diagnostics(trace,workout,target_w,snapshots,profile):
    phases=phase_plan(workout,target_w); signals={}
    for signal,column in (('effective_plant',2),('delivered_sensor',14)):
        reports=[]
        for phase in phases:
            response=phase_response(trace,phase['target_w'],phase['start_s'],phase['end_s'],column=column)
            # Keep first 90%-of-target rise only for actual sprint rises.
            if phase['role']!='sprint':
                response.pop('rise_to_90pct_target_s'); response.pop('rise_threshold_w')
            reports.append({**phase,'response':response})
        signals[signal]={'phases':reports,
            'sprints':[p for p in reports if p['role']=='sprint'],
            'recoveries':[p for p in reports if p['role']=='recovery'],
            'low_power_phases':[p for p in reports if p['target_w']<=240]}
    support=[]
    for window in sprint_windows(90):
        table=next(s['table'] for s in snapshots if s['time_s']==window['sprint_start_s'])
        support.append({'name':window['name'],'time_s':window['sprint_start_s'],
            'table_sha256':digest(table),'support':target_table_support(table,target_w)})
    fit=profile.get('identification',{}); bounds=fit.get('training_position_range'); cad=fit.get('training_cadence_range')
    fit_phases=[]
    if bounds and cad:
        weights=np.diff(np.r_[trace[:,0],DURATION_S])
        outside=(trace[:,4]<bounds[0])|(trace[:,4]>bounds[1])|(trace[:,3]<cad[0])|(trace[:,3]>cad[1])
        for phase in phases:
            mask=(trace[:,0]>=phase['start_s'])&(trace[:,0]<phase['end_s'])
            fit_phases.append({'name':phase['name'],'outside_recorded_fit_range_s':float(weights[mask&outside].sum())})
    return {'signals':signals,'sprint_start_tables':support,'plant_fit_support_by_phase':fit_phases,
            'phase_metrics_source':'Frozen ss2k_sim.high_power.phase_response; ±10/±20 W, five-second dwell, null unachieved/final tails',
            'pause_interpretation':'The scheduled zero-cadence phase is retained and identified; watts-tracking goal is not expected while the rider is stopped.'}


def low_probe_comparison(scores):
    rows=[]
    for b,c in zip(scores['baseline']['windows'],scores['candidate']['windows']):
        if (b['name'],b['start_s'],b['duration_s'])!=(c['name'],c['start_s'],c['duration_s']):
            raise ValueError('Paired low-power probe mismatch')
        signals={name:compare(c[key],b[key]) for name,key in
                 (('effective_plant','metrics'),('delivered_sensor','delivered_sensor_metrics'))}
        rows.append({'name':b['name'],'start_s':b['start_s'],'signals':signals})
    if len(rows)!=3: raise ValueError('Expected three paired low-power probes')
    return {'probes':rows,**{key:all(x['signals'][s][source] for x in rows for s in x['signals'])
        for key,source in (('passed','passed'),('absolute_passed','absolute_passed'),('relative_passed','relative_passed'))}}


def verify_pair(metadata,traces):
    for key in ('initial_table_sha256','cadence_plan_sha256','sensor_schedules','seed','sensitivity',
                'tick_ms','simulation_seconds','pause_events','initial_position_steps'):
        if metadata['baseline'][key]!=metadata['candidate'][key]:
            raise ValueError('Paired input mismatch: '+key)
    # Independent rider/clock/requested targets must be identical, not just hashes.
    if not np.array_equal(traces['baseline'][:,[0,1,3]],traces['candidate'][:,[0,1,3]]):
        raise ValueError('Paired actual time/target/cadence traces differ')


def candidate_changed_file_hashes(root):
    return {p:sha256(root/p) for p in git(root,'diff','--name-only','HEAD').splitlines()}


def preflight(candidate_root,additional_candidate_paths=()):
    selection_path=ROOT/'baseline-selection.json'
    if sha256(selection_path)!=SELECTION_FILE_SHA: raise ValueError('Frozen baseline selection file changed')
    selection=baseline.validate(json.loads(selection_path.read_text()))
    if selection['commit']!=BASELINE_SHA or selection['mode']!='develop': raise ValueError('Wrong frozen develop selection')
    if git(SIMULATOR,'rev-parse','HEAD^{tree}')!=SIMULATOR_TREE or git(SIMULATOR,'status','--porcelain'):
        raise ValueError('Frozen simulator tree changed or became dirty')
    if file_fingerprint(PACKAGE,list(PACKAGE.rglob('*.py')))!=SIMULATOR_PACKAGE_SHA:
        raise ValueError('Frozen simulator package fingerprint changed')
    roots={'baseline':ROOT/'baseline','candidate':candidate_root.resolve()}
    sources={k:source_provenance(v) for k,v in roots.items()}
    if sources['baseline']['commit']!=BASELINE_SHA or sources['baseline']['dirty']:
        raise ValueError('Baseline must remain clean at the frozen develop selection')
    if sources['candidate']['commit']!=BASELINE_SHA:
        raise ValueError('Candidate must be a frozen source patch over the exact baseline commit')
    # Trial candidates are intentionally frozen source patches over the base.
    # Record their real dirty state and patch; never describe them as clean commits.
    changed=set(git(roots['candidate'],'diff','--name-only','HEAD').splitlines())
    allowed={'include/ERG_Mode.h','include/ERG_Mode_Utils.h','src/ERG_Mode.cpp'}
    selected=set(additional_candidate_paths)
    if selected-EXPLICIT_ADDITIONAL_CANDIDATE_PATHS:
        raise ValueError('Additional candidate paths must be explicitly reviewed source/test paths')
    allowed |= selected
    if changed-allowed or git(roots['candidate'],'ls-files','--others','--exclude-standard'):
        raise ValueError('Candidate contains changes outside the authorized frozen controller patch')
    adapter,ainfo=load_adapter(ROOT/'adapter',expected_sha256=ADAPTER_SHA)
    if ainfo['commit']!=BASELINE_SHA or ainfo['dirty']:
        raise ValueError('Adapter must remain clean at the frozen merged develop revision')
    profile_path=PACKAGE/'profiles/ride_20260927.json'; fixture_path=ROOT/'archive_fixture/full.json'
    if sha256(profile_path)!=PROFILE_SHA or sha256(fixture_path)!=FIXTURE_FILE_SHA:
        raise ValueError('Frozen plant profile or archived fixture file changed')
    profile=json.loads(profile_path.read_text()); config=BikeConfig(**profile['bike'])
    fixture=load_fixture(fixture_path,profile_path,config)
    if fixture['sha256']!=FIXTURE_SHA: raise ValueError('Unexpected archived table digest')
    return selection,roots,sources,adapter,ainfo,profile,config,fixture


def verify_pair_files(metadata,baseline_path,candidate_path,workout=None):
    for key in ('initial_table_sha256','cadence_plan_sha256','sensor_schedules','seed','sensitivity',
                'tick_ms','simulation_seconds','pause_events','initial_position_steps'):
        if metadata['baseline'][key]!=metadata['candidate'][key]:
            raise ValueError('Paired input mismatch: '+key)
    meta=metadata['baseline']; tick=meta['tick_ms']; duration=meta['simulation_seconds']
    if not isinstance(tick,int) or tick<=0 or not math.isfinite(duration) or duration<=0:
        raise ValueError('Invalid independent-clock metadata')
    expected=round(duration*1000/tick)
    if not math.isclose(expected*tick/1000,duration,abs_tol=1e-10,rel_tol=0):
        raise ValueError('Duration does not end on an independent tick')
    count=0
    with baseline_path.open() as b,candidate_path.open() as c:
        rb,rc=csv.reader(b),csv.reader(c)
        bh,ch=next(rb,None),next(rc,None)
        if bh!=ch or bh is None or len(bh)!=15: raise ValueError('Paired CSV headers differ or are malformed')
        while True:
            br,cr=next(rb,None),next(rc,None)
            if br is None or cr is None:
                if br is not None or cr is not None: raise ValueError('Paired CSV row counts differ')
                break
            if len(br)!=15 or len(cr)!=15: raise ValueError('Malformed trace column count')
            try:
                bv,cv=[float(x) for x in br],[float(x) for x in cr]
            except ValueError as exc:
                raise ValueError('Nonnumeric trace value') from exc
            if not all(math.isfinite(x) for x in (*bv,*cv)):
                raise ValueError('Nonfinite trace value')
            at=count*tick/1000
            if bv[0]!=at or cv[0]!=at: raise ValueError('Trace timestamp differs from independent tick')
            if workout is not None and (bv[1]!=workout.at(at)[0] or cv[1]!=workout.at(at)[0]):
                raise ValueError('Trace target differs from declared workout')
            if [br[i] for i in (0,1,3)]!=[cr[i] for i in (0,1,3)]:
                raise ValueError('Paired actual time/target/cadence traces differ')
            count+=1
    if count!=expected:
        raise ValueError('Paired traces do not cover the complete independent-clock ride')
    return {'rows':count,'time_target_cadence_identical':True,'all_values_finite':True,'every_tick_verified':True,
            'targets_match_workout':workout is not None,'method':'Streaming CSV comparison; no trace arrays retained by the parent'}


def verify_worker_result(config):
    dest=Path(config['destination'])
    if json.loads((dest/'worker_config.json').read_text())!=config:
        raise ValueError('Completed worker config differs from requested case')
    result=json.loads((dest/'worker_result.json').read_text())
    if sha256(__file__)!=config['runner_sha256']:
        raise ValueError('Runner changed from frozen matrix hash')
    expected={'status':'complete','runner_sha256':config['runner_sha256'],'exe_sha256':config['exe_sha256'],
              'source_provenance':config['source_provenance'],'case':config['case'],'variant':config['variant']}
    for key,value in expected.items():
        if result.get(key)!=value: raise ValueError('Incomplete or mismatched worker result: '+key)
    if sha256(config['exe'])!=config['exe_sha256']:
        raise ValueError('Compiled binary changed after completed worker')
    return result


def execute_variant(config):
    if sha256(__file__)!=config['runner_sha256']:
        raise ValueError('Runner changed before worker execution')
    dest=Path(config['destination']); dest.mkdir(parents=True,exist_ok=True)
    config_path=dest/'worker_config.json'; write_json(config_path,config)
    with (dest/'worker.log').open('w') as log:
        subprocess.run([sys.executable,str(Path(__file__).resolve()),'--worker-config',str(config_path)],
                       stdout=log,stderr=subprocess.STDOUT,check=True)


def worker_main(path):
    config=json.loads(path.read_text()); case=config['case']; variant=config['variant']
    if sha256(__file__)!=config['runner_sha256']:
        raise ValueError('Worker runner differs from frozen matrix hash')
    selection,roots,sources,adapter,ainfo,profile,bike,fixture=preflight(Path(config['candidate_root']),config['additional_candidate_paths'])
    if candidate_changed_file_hashes(roots['candidate'])!=config['candidate_changed_file_sha256']:
        raise ValueError('Candidate changed source/test files differ from frozen worker plan')
    if sources[variant]!=config['source_provenance'] or sha256(config['exe'])!=config['exe_sha256']:
        raise ValueError('Worker source or compiled binary differs from frozen plan')
    target=case['target_w']; workout,windows=extended_workout(target); dest=Path(config['destination'])
    table=fixture['table'] if case['initial_state']=='full' else None
    snapshot_times=sorted({x for w in sprint_windows(90) for x in
                           (w['start_s'],w['sprint_start_s'],w['recovery_start_s'])})
    trace,meta,score,snapshots=run_one(adapter,Path(config['exe']),bike,profile,dest,workout,case['seed'],
        table=table,windows=windows,sprints=(),stress=True,sensitivity=case['sensitivity'],
        extra_snapshot_times=snapshot_times)
    gc.collect()
    meta['scenario_version']=EXPERIMENT; meta['diagnostic_sprint_target_w']=target
    meta['sprint_blocks']=[{**w,'target_w':target} for w in sprint_windows(90)]
    meta['coverage_note']='CPS/CSC and FTMS native coverage; no Peloton cable windows, radio or ESP32 scheduling.'
    meta['legacy_sprint_response_used']=False
    diagnostic=run_diagnostics(trace,workout,target,snapshots,profile)
    write_json(dest/'metadata.json',meta); write_json(dest/'diagnostic.json',diagnostic)
    del trace,snapshots; gc.collect()
    if source_provenance(roots[variant])!=sources[variant]: raise ValueError('Worker source changed during ride')
    if candidate_changed_file_hashes(roots['candidate'])!=config['candidate_changed_file_sha256']:
        raise ValueError('Candidate changed source/test files during ride')
    if sha256(__file__)!=config['runner_sha256']:
        raise ValueError('Worker runner changed during ride')
    write_json(dest/'worker_result.json',{'status':'complete','peak_rss_kib':resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
        'runner_sha256':config['runner_sha256'],'exe_sha256':config['exe_sha256'],'source_provenance':sources[variant],
        'case':case,'variant':variant})
    return 0


def try_reuse_baseline(origin,dest,case,name,manifest):
    source=origin/'baseline'/name
    if not source.exists(): return None
    with (origin/'extended_high_power.json').open() as handle: old=json.load(handle)
    for key in ('baseline_selection','baseline_selection_file_sha256','adapter','simulator_sha256','profile_sha256',
                'archived_full_fixture_file_sha256','duration_s','stress_enabled'):
        if old[key]!=manifest[key]: raise ValueError('Baseline reuse provenance mismatch: '+key)
    if old['firmware']['baseline']!=manifest['firmware']['baseline']:
        raise ValueError('Baseline reuse source mismatch')
    if old['workouts'][str(case['target_w'])]!=manifest['workouts'][str(case['target_w'])] or case not in old['requested_cases']:
        raise ValueError('Baseline reuse workout or requested case mismatch')
    old_runner=old['runner_sha256']; old_status=old['status']; del old; gc.collect()
    required=('metadata.json','score.json','diagnostic.json','trace.csv','learning_snapshots.json','power_table.json',
              'sensor_packets.csv','firmware.log','ftms_control_packets.json','cadence.csv')
    if not all((source/f).is_file() for f in required):
        raise ValueError('Existing baseline variant is incomplete and cannot be reused: '+str(source))
    meta=json.loads((source/'metadata.json').read_text())
    if any(meta[key]!=value for key,value in {'seed':case['seed'],'sensitivity':case['sensitivity'],
            'simulation_seconds':DURATION_S,'tick_ms':20,'diagnostic_sprint_target_w':case['target_w'],'scenario_version':EXPERIMENT,
            'pause_events':[{'time_s':1380,'active':False},{'time_s':1440,'active':True}],
            'sensor_schedules':{'power':{**SensorSchedule(1,.2,.1).to_dict(),'dropouts':[[1350,1362]]},
                                'cadence':{**SensorSchedule(.25,.1).to_dict(),'dropouts':[]}}}.items()):
        raise ValueError('Baseline reuse run metadata mismatch')
    # Validate all clock rows before accepting a complete baseline variant.
    workout=Workout([Segment(**s) for s in manifest['workouts'][str(case['target_w'])]['segments']])
    if meta['cadence_plan_sha256']!=CadencePlan(workout).sha256:
        raise ValueError('Reused baseline cadence plan differs from declared workout')
    snapshots=json.loads((source/'learning_snapshots.json').read_text())
    if not snapshots or snapshots[0]['time_s']!=0 or snapshots[-1]['time_s']!=DURATION_S:
        raise ValueError('Reused baseline lacks initial/final table snapshots')
    if digest(snapshots[0]['table'])!=meta['initial_table_sha256']:
        raise ValueError('Reused baseline initial table digest mismatch')
    if case['initial_state']=='full' and meta['initial_table_sha256']!=FIXTURE_SHA:
        raise ValueError('Reused baseline did not start from archived full fixture')
    if case['initial_state']=='empty' and any(n or p!=-32768 for p,n in snapshots[0]['table']['cells']):
        raise ValueError('Reused baseline did not start with an empty table')
    if snapshots[-1]['table']!=json.loads((source/'power_table.json').read_text()):
        raise ValueError('Reused baseline final table differs from snapshot')
    verified=verify_pair_files({'baseline':meta,'candidate':meta},source/'trace.csv',source/'trace.csv',workout)
    if dest.exists(): raise ValueError('Refusing to replace existing reuse destination')
    shutil.copytree(source,dest)
    info={'reused':True,'source_path':str(source.resolve()),'path':str(dest.resolve()),
          'original_runner_sha256':old_runner,'source_matrix_status':old_status,
          'variant_status':'This baseline variant independently verified complete; source matrix status is retained separately',
          'trace_sha256':sha256(source/'trace.csv'),'verified':verified}
    if sha256(dest/'trace.csv')!=info['trace_sha256']: raise ValueError('Copied baseline trace differs')
    write_json(dest/'reuse-provenance.json',info)
    return info


def parser():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--candidate-root',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--targets',type=int,nargs='+',choices=(500,700,1000),default=[500,700,1000])
    p.add_argument('--states',nargs='+',choices=('empty','full'),default=['empty','full'])
    p.add_argument('--sensitivities',type=int,nargs='+',choices=(3,5,10),default=[3,5,10])
    p.add_argument('--seeds',type=int,nargs='+',default=[7])
    p.add_argument('--dry-run',action='store_true',help='Validate frozen inputs and write plan; no build or controller execution')
    p.add_argument('--reuse-baseline-from',type=Path,help='Reuse only complete, verified baseline variant artifacts from a compatible prior matrix')
    p.add_argument('--additional-candidate-path',action='append',default=[],choices=sorted(EXPLICIT_ADDITIONAL_CANDIDATE_PATHS),
                   help='Explicit reviewed changed source/test path beyond the three controller files; repeat as needed')
    return p


def main(argv=None):
    a=parser().parse_args(argv)
    for values in (a.targets,a.states,a.sensitivities,a.seeds):
        if len(values)!=len(set(values)): raise ValueError('Duplicate matrix values are not allowed')
    if a.output.exists() and any(a.output.iterdir()): raise ValueError('Output must be new/empty')
    selection,roots,sources,adapter,ainfo,profile,config,fixture=preflight(a.candidate_root,a.additional_candidate_path)
    changed_hashes=candidate_changed_file_hashes(roots['candidate'])
    workouts={target:extended_workout(target) for target in a.targets}
    a.output.mkdir(parents=True,exist_ok=True)
    cases=[{'target_w':t,'initial_state':s,'sensitivity':v,'seed':seed}
           for t in a.targets for s in a.states for v in a.sensitivities for seed in a.seeds]
    candidate_patch=subprocess.check_output(['git','-C',str(roots['candidate']),'diff','--binary','HEAD'])
    (a.output/'candidate-source.patch').write_bytes(candidate_patch)
    (a.output/'extended_high_power.runner.py').write_bytes(Path(__file__).read_bytes())
    manifest={'status':'planned' if a.dry_run else 'incomplete','experiment':EXPERIMENT,
        'candidate_patch_sha256':sha256(a.output/'candidate-source.patch'),
        'candidate_changed_paths':git(roots['candidate'],'diff','--name-only','HEAD').splitlines(),
        'additional_candidate_paths':a.additional_candidate_path,'candidate_changed_file_sha256':changed_hashes,
        'runner_sha256':sha256(__file__),'baseline_selection':selection,'baseline_selection_file_sha256':SELECTION_FILE_SHA,
        'firmware':sources,'adapter':ainfo,'simulator_commit':git(SIMULATOR,'rev-parse','HEAD'),
        'simulator_tree':SIMULATOR_TREE,'simulator_sha256':SIMULATOR_PACKAGE_SHA,
        'profile_sha256':PROFILE_SHA,'bike':asdict(config),'archived_full_fixture':fixture,
        'archived_full_fixture_file_sha256':FIXTURE_FILE_SHA,
        'duration_s':DURATION_S,'stress_enabled':True,'requested_cases':cases,
        'workouts':{str(t):{'segments':[asdict(s) for s in w.segments],'sha256':digest([asdict(s) for s in w.segments]),
                          'cadence_plan_sha256':CadencePlan(w).sha256,'probes':p,'phases':phase_plan(w,t)}
                    for t,(w,p) in workouts.items()},
        'goal':{'band_w':10,'dwell_s':5,'settling_limit_s':10,'meaning':'Final uninterrupted tail begins strictly below ten seconds and lasts at least five seconds'},
        'absolute_budgets':DEFAULT_BUDGET,'relative_allowances':RELATIVE_ALLOWANCE,
        'limitations':['CPS/CSC and FTMS native coverage; no Peloton cable windows, radio or ESP32 scheduling.',
            'Archived full fixture unchanged; 45 declared training-grid cells do not imply high-target bracket support.',
            '1000 W is beyond the 870 W table axis. Fitted plant, sensor noise and all budgets remain unchanged.',
            'Scheduled pause/outage stress retained; zero-cadence phases are explicitly labeled.',
            'New high-target phase metrics are descriptive. Existing budget comparisons apply separately to all three low-power probes.'],
        'execution_model':'One fresh Python worker per native ride, two compiled binaries reused; streaming checkpoints and paired CSV verification.',
        'runs':[]}
    report=a.output/'extended_high_power.json'; write_json(report,manifest)
    if a.dry_run:
        print(f'Validated {len(cases)} paired 90-minute cases; no native build or ride executed: {report}')
        return 0
    try:
        with baseline.pinned_root(roots['baseline'],selection) as pinned:
            build_roots={**roots,'baseline':pinned}
            exes={variant:adapter.build(a.output/variant/'build',source_root=path,packet_ingress=True)
                  for variant,path in build_roots.items()}
            exe_hashes={variant:sha256(path) for variant,path in exes.items()}
            manifest['executables']={variant:{'path':str(path.resolve()),'sha256':exe_hashes[variant]} for variant,path in exes.items()}
            write_json(report,manifest)
            for variant,path in roots.items():
                if source_provenance(path)!=sources[variant]: raise ValueError('Source changed while building: '+variant)
            if candidate_changed_file_hashes(roots['candidate'])!=changed_hashes:
                raise ValueError('Candidate source/test files changed while building')
            for case in cases:
                target=case['target_w']; workout,windows=workouts[target]
                name=f'extended_{target}w_{case["initial_state"]}_sensitivity{case["sensitivity"]}-{case["seed"]}'
                diagnostics={}; scores={}; metadata={}; artifacts={}
                for variant in roots:
                    if shutil.disk_usage(a.output).free < 512*1024*1024:
                        raise RuntimeError('Less than 512 MiB free before a ride/artifact reuse; stopped without discarding completed cases')
                    dest=a.output/variant/name
                    worker={'candidate_root':str(roots['candidate']),'variant':variant,'destination':str(dest.resolve()),
                            'exe':str(exes[variant].resolve()),'exe_sha256':exe_hashes[variant],
                            'case':case,'source_provenance':sources[variant],
                            'runner_sha256':manifest['runner_sha256'],
                            'additional_candidate_paths':a.additional_candidate_path,'candidate_changed_file_sha256':changed_hashes}
                    reused=None
                    if variant=='baseline' and a.reuse_baseline_from:
                        reused=try_reuse_baseline(a.reuse_baseline_from,dest,case,name,manifest)
                    if reused is None:
                        execute_variant(worker)
                        artifacts[variant]={'reused':False,'path':str(dest.resolve()),
                            'resource_usage':verify_worker_result(worker)}
                    else:
                        artifacts[variant]=reused
                    metadata[variant]=json.loads((dest/'metadata.json').read_text())
                    scores[variant]=json.loads((dest/'score.json').read_text())
                    diagnostics[variant]=json.loads((dest/'diagnostic.json').read_text())
                    gc.collect()
                verified=verify_pair_files(metadata,a.output/'baseline'/name/'trace.csv',a.output/'candidate'/name/'trace.csv',workout)
                low=low_probe_comparison(scores)
                result={**case,'case':name,'diagnostics':diagnostics,'scores':scores,'metadata':metadata,
                        'low_power_probe_comparison':low,'artifacts':artifacts,'paired_trace_verification':verified}
                manifest['runs'].append(result)
                manifest['parent_peak_rss_kib']=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
                write_json(report,manifest)
                gc.collect()
                print(f'{name}: low-probe relative={low["relative_passed"]}, absolute={low["absolute_passed"]}',flush=True)
            for variant,path in roots.items():
                if source_provenance(path)!=sources[variant]: raise ValueError('Source changed during matrix: '+variant)
            # Recheck all other immutable inputs before claiming completion.
            preflight(a.candidate_root,a.additional_candidate_path)
            if candidate_changed_file_hashes(roots['candidate'])!=changed_hashes:
                raise ValueError('Candidate source/test files changed during matrix')
            if sha256(__file__)!=manifest['runner_sha256']:
                raise ValueError('Runner changed during matrix')
            if any(sha256(path)!=exe_hashes[variant] for variant,path in exes.items()):
                raise ValueError('Compiled binary changed during matrix')
        manifest['status']='complete'
        manifest['low_power_probe_relative_passed']=all(r['low_power_probe_comparison']['relative_passed'] for r in manifest['runs'])
        manifest['low_power_probe_absolute_passed']=all(r['low_power_probe_comparison']['absolute_passed'] for r in manifest['runs'])
    except BaseException as exc:
        manifest['status']='failed'; manifest['error']=str(exc); raise
    finally:
        write_json(report,manifest)
    return 0 if all(r['low_power_probe_comparison']['passed'] for r in manifest['runs']) else 1


if __name__=='__main__':
    if len(sys.argv)==3 and sys.argv[1]=='--worker-config':
        raise SystemExit(worker_main(Path(sys.argv[2])))
    raise SystemExit(main())
