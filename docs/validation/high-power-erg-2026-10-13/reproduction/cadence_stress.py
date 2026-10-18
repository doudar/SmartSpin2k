#!/usr/bin/env python3
"""Paired high-power cadence excursions, preserving the original 390 s prefix.

Only the 25 s sprint cadence cue changes. Existing PlannedRider dynamics,
normal CPS/CSC schedules, plant, travel, and phase-response budgets are reused.
Source repositories are read-only; this script and its results live outside them.
"""
import argparse
from dataclasses import asdict, replace
import hashlib
import json
from pathlib import Path
import shutil
import sys
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT/'simulator'))
import numpy as np
import extended_high_power as frozen
from timing_stress import audit_diagnostics, NumericalAuditTests
from ss2k_sim.adapter import source_provenance
from ss2k_sim.cadence import CadencePlan, PlannedRider
from ss2k_sim.high_power import diagnostic_workout, response_summary
from ss2k_sim.longride import digest, run_one
from ss2k_sim.model import equilibrium
from ss2k_sim.performance import DEFAULT_BUDGET, RELATIVE_ALLOWANCE, compare
from ss2k_sim.workout import Workout

EXPERIMENT = 'local-high-power-cadence-stress-v1'
DURATION_S = 445
MIN_FREE_BYTES = 512*1024*1024
PREFIX = [{'name':'low_power_prefix','start_s':0,'duration_s':360}]
EXPECTED_SCHEDULES = {'power':{'period_s':1,'latency_s':.2,'jitter_s':.1,'phase_s':0.,'dropouts':[]},
                      'cadence':{'period_s':.25,'latency_s':.1,'jitter_s':0.,'phase_s':0.,'dropouts':[]}}


def guard_disk(path):
    free=shutil.disk_usage(path).free
    if free<=MIN_FREE_BYTES: raise RuntimeError(f'Disk guard: {free} free bytes <= {MIN_FREE_BYTES}')
    return free


def cadence_workout(target, cadence):
    if cadence not in (65,105): raise ValueError('Only predeclared 65/105 RPM cues')
    original=diagnostic_workout(target)
    segments=[]; start=0.; changed=0
    for segment in original.segments:
        if start==390:
            assert segment.duration_s==25 and segment.cadence_rpm==85
            segment=replace(segment,cadence_rpm=cadence); changed+=1
        segments.append(segment); start+=segment.duration_s
    assert changed==1 and start==445 and segments[-1].cadence_rpm==85
    return Workout(segments)


def prefix_fingerprint(trace):
    part=np.ascontiguousarray(trace[trace[:,0]<390],dtype='<f8')
    assert len(part)==19500 and part[0,0]==0 and part[-1,0]==389.98
    return hashlib.sha256(part.tobytes()).hexdigest()


def reference_audit(reference, variant, case, trace, metadata, snapshots):
    folder=reference/variant/f'early_{case["target_w"]}w_{case["initial_state"]}_sensitivity{case["sensitivity"]}-7'
    prior=np.loadtxt(folder/'trace.csv',delimiter=',',skiprows=1)
    oldmeta=json.loads((folder/'metadata.json').read_text())
    oldsnaps=json.loads((folder/'learning_snapshots.json').read_text())
    for key in ('initial_table_sha256','sensor_schedules','seed','sensitivity','tick_ms','simulation_seconds','initial_position_steps'):
        assert metadata[key]==oldmeta[key],f'Reference input differs: {key}'
    old_table=next(s['table'] for s in oldsnaps if s['time_s']==390)
    new_table=next(s['table'] for s in snapshots if s['time_s']==390)
    old_hash=prefix_fingerprint(prior); new_hash=prefix_fingerprint(trace)
    assert old_hash==new_hash,'Actual complete pre-390 trace differs from original 85 RPM reference'
    assert old_table==new_table,'Learned table at sprint entry differs from original reference'
    return {'reference_path':str(folder.resolve()),'reference_trace_sha256':frozen.sha256(folder/'trace.csv'),
            'actual_prefix_rows':19500,'actual_prefix_all_columns_sha256':new_hash,'reference_prefix_identical':True,
            'sprint_start_table_identical':True,'sprint_start_table_sha256':digest(new_table)}


def cadence_support(table, target, cadence):
    low=int(np.floor(min(cadence)/table['cadence_increment'])*table['cadence_increment'])
    high=int(np.ceil(max(cadence)/table['cadence_increment'])*table['cadence_increment'])
    watt_cols=sorted({int(np.floor(target/table['watt_increment'])),int(np.ceil(target/table['watt_increment']))})
    cells=[]
    for rpm in range(low,high+1,table['cadence_increment']):
        row=(rpm-table['cadence_min'])//table['cadence_increment']
        for col in watt_cols:
            valid=0<=row<table['rows'] and 0<=col<table['columns']
            position,count=table['cells'][row*table['columns']+col] if valid else (-32768,0)
            cells.append({'cadence_rpm':rpm,'watts':col*table['watt_increment'],'in_grid':valid,
                          'supported':bool(valid and position!=-32768 and count>=2)})
    return {'target_w':target,'actual_cadence_bracket_rpm':[low,high],
            'table_max_w':(table['columns']-1)*table['watt_increment'],
            'all_actual_cadence_brackets_supported':all(c['supported'] for c in cells),'cells':cells,
            'meaning':'Measured-cell coverage at sprint entry across actual sprint cadence. Does not imply controller trust.'}


def motion_diagnostics(trace, config, profile):
    fit=profile['identification']; pos=fit['training_position_range']; cad=fit['training_cadence_range']
    result={}
    for name,start,end in (('sprint',390,415),('recovery',415,445)):
        x=trace[(trace[:,0]>=start)&(trace[:,0]<end)]; dt=np.diff(np.r_[x[:,0],end]); rpm=x[:,3]; target=x[:,1]
        ceiling=np.array([equilibrium(config,config.max_position,c) for c in rpm])
        floor=np.array([equilibrium(config,config.min_position,c) for c in rpm])
        pos_out=(x[:,4]<pos[0])|(x[:,4]>pos[1]); cad_out=(rpm<cad[0])|(rpm>cad[1])
        result[name]={'start_s':start,'duration_s':end-start,
            'actual_cadence_rpm':{'start':float(rpm[0]),'end':float(rpm[-1]),'min':float(min(rpm)),
                                  'max':float(max(rpm)),'mean':float(np.average(rpm,weights=dt))},
            'physical_position_steps':{'min':float(min(x[:,4])),'max':float(max(x[:,4]))},
            'physical_upper_endstop_s':float(dt[x[:,4]>=config.max_position-.5].sum()),
            'physical_lower_endstop_s':float(dt[x[:,4]<=config.min_position+.5].sum()),
            'command_at_or_above_upper_endstop_s':float(dt[x[:,6]>=config.max_position-.5].sum()),
            'command_at_or_below_lower_endstop_s':float(dt[x[:,6]<=config.min_position+.5].sum()),
            'model_equilibrium_ceiling_w':{'min':float(min(ceiling)),'max':float(max(ceiling))},
            'target_above_model_ceiling_s':float(dt[target>ceiling].sum()),
            'target_minus_10w_above_model_ceiling_s':float(dt[target-10>ceiling].sum()),
            'target_below_model_floor_s':float(dt[target<floor].sum()),
            'outside_fitted_position_s':float(dt[pos_out].sum()),'outside_fitted_cadence_s':float(dt[cad_out].sum()),
            'outside_either_fitted_range_s':float(dt[pos_out|cad_out].sum()),
            'capacity_meaning':'Instantaneous equilibrium feasibility at actual cadence and unchanged travel; excludes lag/noise and is extrapolated model evidence, not hardware capability.'}
    return result


class CadenceAuditTests(unittest.TestCase):
    def test_only_high_cadence_changes(self):
        for target in (500,700,1000):
            original=diagnostic_workout(target)
            for rpm in (65,105):
                changed=cadence_workout(target,rpm)
                self.assertEqual(changed.duration,original.duration)
                differing=[i for i,(a,b) in enumerate(zip(original.segments,changed.segments)) if a!=b]
                self.assertEqual(differing,[len(original.segments)-2])
                a,b=asdict(original.segments[differing[0]]),asdict(changed.segments[differing[0]])
                b['cadence_rpm']=a['cadence_rpm']; self.assertEqual(a,b)
                r1,r2=PlannedRider(original),PlannedRider(changed)
                for tick in range(1,19500):
                    self.assertEqual(r1.advance(tick*.02,.02),r2.advance(tick*.02,.02))

    def test_disk_guard_rejects_floor(self):
        from unittest.mock import patch
        with patch('shutil.disk_usage',return_value=shutil._ntuple_diskusage(100,50,MIN_FREE_BYTES)):
            with self.assertRaises(RuntimeError):guard_disk(ROOT)


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--candidate-root',type=Path,default=ROOT/'trial-o')
    p.add_argument('--reference',type=Path,default=ROOT/'experiments/o_source_coverage')
    p.add_argument('--output',type=Path)
    p.add_argument('--self-test',action='store_true')
    p.add_argument('--dry-run',action='store_true')
    a=p.parse_args(argv)
    suite=unittest.TestSuite([unittest.defaultTestLoader.loadTestsFromTestCase(c) for c in (NumericalAuditTests,CadenceAuditTests)])
    tests=unittest.TextTestRunner(verbosity=2).run(suite)
    if not tests.wasSuccessful():return 2
    if a.self_test:return 0
    if a.output is None:p.error('Need --output')
    if a.candidate_root.resolve().name!='trial-o':raise ValueError('This audit requires frozen trial-o')
    selection,roots,sources,adapter,ainfo,profile,config,fixture=frozen.preflight(a.candidate_root)
    reference=json.loads((a.reference/'high_power.json').read_text())
    assert reference['status']=='complete' and reference['profile_sha256']==frozen.PROFILE_SHA
    for v in roots:
        for key in ('commit','source_sha256','dirty'):assert reference['firmware'][v][key]==sources[v][key],(v,key)
    assert reference['adapter']==ainfo
    if a.output.exists() and any(a.output.iterdir()):raise ValueError('Output must be new/empty')
    a.output.mkdir(parents=True,exist_ok=True); guard_disk(a.output)
    cases=[{'sprint_cadence_cue_rpm':rpm,'initial_state':state,'sensitivity':s,'target_w':t,'seed':7}
           for rpm in (65,105) for state in ('empty','full') for s in (3,5,10) for t in (500,700,1000)]
    workouts={(t,r):cadence_workout(t,r) for t in (500,700,1000) for r in (65,105)}
    (a.output/'candidate-source.patch').write_text(frozen.git(roots['candidate'],'diff','--binary','HEAD')+'\n')
    manifest={'status':'planned' if a.dry_run else 'incomplete','experiment':EXPERIMENT,'runner_sha256':frozen.sha256(__file__),
        'audit_helper_sha256':frozen.sha256(ROOT/'timing_stress.py'),'provenance_helper_sha256':frozen.sha256(frozen.__file__),
        'candidate_patch_sha256':frozen.sha256(a.output/'candidate-source.patch'),'baseline_selection':selection,
        'baseline_selection_file_sha256':frozen.SELECTION_FILE_SHA,'firmware':sources,'adapter':ainfo,
        'simulator_sha256':frozen.SIMULATOR_PACKAGE_SHA,'simulator_tree':frozen.SIMULATOR_TREE,
        'profile_sha256':frozen.PROFILE_SHA,'bike':asdict(config),'archived_full_fixture':fixture,
        'archived_full_fixture_file_sha256':frozen.FIXTURE_FILE_SHA,'duration_s':DURATION_S,'seed':7,
        'normal_sensor_schedules':EXPECTED_SCHEDULES,'requested_cases':cases,
        'absolute_budgets':DEFAULT_BUDGET,'relative_allowances':RELATIVE_ALLOWANCE,
        'goal':{'band_w':10,'dwell_s':5,'settling_limit_s':10,'strictly_less_than':True},
        'reference':{'path':str(a.reference.resolve()),'manifest_sha256':frozen.sha256(a.reference/'high_power.json')},
        'workouts':{f'{t}w_{r}rpm':{'segments':[asdict(s) for s in w.segments],'cadence_plan_sha256':CadencePlan(w).sha256} for (t,r),w in workouts.items()},
        'disk_guard_bytes':MIN_FREE_BYTES,'disk_initial_free_bytes':guard_disk(a.output),'self_tests':tests.testsRun,
        'limitations':['High-power/cadence fitted-model extrapolation is not validated hardware performance; 700/1000 W remain extrapolation experiments.',
            'Cadence cues ramp through unchanged PlannedRider dynamics; actual RPM, not just cue, is reported.',
            '1000 W exceeds the 870 W table axis; the unchanged archived full fixture cannot bracket it.',
            'No radio, ESP32 scheduling or Peloton cable emulation. Normal existing CPS/CSC packet schedules are unchanged.',
            'Null settling remains explicit. Existing budgets and strict final-tail goal are unchanged.'],
        'runs':[]}
    report=a.output/'cadence_stress.json'; frozen.write_json(report,manifest)
    if a.dry_run:return 0
    try:
        exes={}
        for v,root in roots.items():
            guard_disk(a.output); exes[v]=adapter.build(a.output/v/'build',source_root=root,packet_ingress=True)
        manifest['executables']={v:{'path':str(exe),'sha256':frozen.sha256(exe)} for v,exe in exes.items()}
        for case in cases:
            guard_disk(a.output)
            name=f'cadence{case["sprint_cadence_cue_rpm"]}_{case["target_w"]}w_{case["initial_state"]}_sensitivity{case["sensitivity"]}-7'
            traces={};metadata={};scores={};diagnostics={};audits={};motion={}
            for v,root in roots.items():
                guard_disk(a.output); assert source_provenance(root)==sources[v]
                dest=a.output/v/name; workout=workouts[(case['target_w'],case['sprint_cadence_cue_rpm'])]
                trace,meta,score,snapshots=run_one(adapter,exes[v],config,profile,dest,workout,7,
                    table=fixture['table'] if case['initial_state']=='full' else None,windows=PREFIX,
                    sensitivity=case['sensitivity'],extra_snapshot_times=(390,415))
                # Normalize dataclass tuples to their persisted JSON form before comparing metadata.
                meta=json.loads(json.dumps(meta))
                assert meta['sensor_schedules']==EXPECTED_SCHEDULES and not meta['pause_events']
                diag=response_summary(trace,case['target_w'],snapshots); audit_diagnostics(trace,diag)
                table=next(s['table'] for s in snapshots if s['time_s']==390)
                diag['sprint_start_table_support']=cadence_support(table,case['target_w'],trace[(trace[:,0]>=390)&(trace[:,0]<415),3])
                motion[v]=motion_diagnostics(trace,config,profile)
                audits[v]=reference_audit(a.reference,v,case,trace,meta,snapshots)
                meta.update(scenario_version=EXPERIMENT,sprint_cadence_cue_rpm=case['sprint_cadence_cue_rpm'])
                for key,value in (('diagnostic',diag),('motion',motion[v]),('prefix_audit',audits[v]),('metadata',meta)):
                    frozen.write_json(dest/(key+'.json'),value)
                traces[v]=trace;metadata[v]=meta;scores[v]=score;diagnostics[v]=diag
            frozen.verify_pair(metadata,traces)
            identical=prefix_fingerprint(traces['baseline'])==prefix_fingerprint(traces['candidate'])
            same_table=audits['baseline']['sprint_start_table_sha256']==audits['candidate']['sprint_start_table_sha256']
            assert identical and same_table,'Baseline/candidate pre-high controller or table regression'
            low={signal:compare(scores['candidate']['windows'][0][key],scores['baseline']['windows'][0][key])
                 for signal,key in (('effective_plant','metrics'),('delivered_sensor','delivered_sensor_metrics'))}
            row={**case,'case':name,'metadata':metadata,'scores':scores,'diagnostics':diagnostics,'motion':motion,'prefix_audit':audits,
                 'prefix_trace_identical':identical,'sprint_entry_table_identical':same_table,'low_power_regression':low}
            for key in ('passed','relative_passed','absolute_passed'):row['low_power_'+key]=all(x[key] for x in low.values())
            manifest['runs'].append(row);manifest['disk_latest_free_bytes']=guard_disk(a.output);frozen.write_json(report,manifest)
            s=diagnostics['candidate']['signals']['effective_plant']['sprint'];r=diagnostics['candidate']['signals']['effective_plant']['recovery'];m=motion['candidate']['sprint']
            print(f'{name}: plant sprint={s["bands"]["10"]["final_settling_s"]}, recovery={r["bands"]["10"]["final_settling_s"]}, peak={s["peak_power_w"]:.1f} W; upper-stop={m["physical_upper_endstop_s"]:.2f} s, unreachable={m["target_above_model_ceiling_s"]:.2f} s; prefix-identical={identical}',flush=True)
        frozen.preflight(a.candidate_root)
        assert all(source_provenance(root)==sources[v] for v,root in roots.items())
        manifest['status']='complete'
        for key in ('passed','relative_passed','absolute_passed'):manifest['low_power_'+key]=all(r['low_power_'+key] for r in manifest['runs'])
        manifest['all_prefix_and_entry_tables_identical']=all(r['prefix_trace_identical'] and r['sprint_entry_table_identical'] for r in manifest['runs'])
    except BaseException as exc:
        manifest['status']='failed';manifest['error']=str(exc);raise
    finally:frozen.write_json(report,manifest)
    return 0


if __name__=='__main__':raise SystemExit(main())
