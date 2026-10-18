#!/usr/bin/env python3
"""Continue an exactly frozen long matrix stopped at a completed-pair disk guard.
No builds or completed rides are repeated. Interrupted artifacts are preserved.
"""
import argparse
import gc
import json
from pathlib import Path
import resource
import shutil
import sys
sys.dont_write_bytecode=True
import extended_high_power as e

DISK_ERROR='Less than 512 MiB free before a ride/artifact reuse; stopped without discarding completed cases'

def check(condition,message):
    if not condition: raise ValueError(message)

def load(path):
    with path.open() as f:return json.load(f)

def hashes(root):
    return {str(p.relative_to(root)):e.sha256(p) for p in sorted(root.rglob('*')) if p.is_file()}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--reuse-baseline-from',type=Path,required=True);p.add_argument('--expected-source',required=True);p.add_argument('--verify-only',action='store_true');a=p.parse_args()
    helper_sha=e.sha256(__file__)
    root=a.output.resolve();report=root/'extended_high_power.json';m=load(report);original_sha=e.sha256(report)
    check(m['status']=='failed' and m.get('error')==DISK_ERROR,'Only a completed-pair diskguard interruption is supported')
    check(m['runner_sha256']==e.sha256(e.__file__)==e.sha256(root/'extended_high_power.runner.py'),'Original frozen runner changed')
    check(m['firmware']['candidate']['source_sha256']==a.expected_source,'Wrong expected source')
    selection,roots,sources,adapter,ainfo,profile,bike,fixture=e.preflight(Path(m['firmware']['candidate']['root']),m['additional_candidate_paths'])
    check(sources==m['firmware'] and ainfo==m['adapter'],'Source/adapter changed')
    norm=lambda v:json.loads(json.dumps(v))
    check(selection==m['baseline_selection'] and m['baseline_selection_file_sha256']==e.SELECTION_FILE_SHA,'Selection pins differ')
    check(m['simulator_tree']==e.SIMULATOR_TREE and m['simulator_sha256']==e.SIMULATOR_PACKAGE_SHA,'Simulator pins differ')
    check(m['profile_sha256']==e.PROFILE_SHA and m['bike']==norm(e.asdict(bike)),'Profile/model differs')
    check(m['archived_full_fixture_file_sha256']==e.FIXTURE_FILE_SHA and m['archived_full_fixture']==norm(fixture),'Fixture differs')
    check(m['absolute_budgets']==e.DEFAULT_BUDGET and m['relative_allowances']==norm(e.RELATIVE_ALLOWANCE),'Budgets differ')
    check(m['additional_candidate_paths']==['src/PowerTable_Helpers.cpp'],'Unexpected explicit extra source paths')
    check(e.candidate_changed_file_hashes(roots['candidate'])==m['candidate_changed_file_sha256'],'Changed file pins differ')
    check(e.sha256(root/'candidate-source.patch')==m['candidate_patch_sha256'],'Original patch changed')
    for v,x in m['executables'].items():check(e.sha256(x['path'])==x['sha256'],'Original executable changed')
    requested=m['requested_cases'];count=len(m['runs']);check(0<count<len(requested),'No incomplete work')
    expected=[{'target_w':t,'initial_state':state,'sensitivity':s,'seed':7} for t in (500,700,1000) for state in ('empty','full') for s in (3,5,10)]
    check(requested==expected,'Unexpected requested Cartesian matrix')
    origin_matrix=load(a.reuse_baseline_from/'extended_high_power.json')
    check(origin_matrix['status']=='complete','Baseline reuse origin matrix is not complete')
    for key in ('baseline_selection','baseline_selection_file_sha256','adapter','simulator_sha256','profile_sha256','archived_full_fixture_file_sha256','duration_s','stress_enabled','workouts'):
        check(origin_matrix[key]==m[key],'Remaining baseline origin provenance differs: '+key)
    check(origin_matrix['firmware']['baseline']==sources['baseline'],'Remaining baseline origin source differs')
    for case in requested[count:]:
        name=f"extended_{case['target_w']}w_{case['initial_state']}_sensitivity{case['sensitivity']}-{case['seed']}"
        origin=a.reuse_baseline_from/'baseline'/name
        check(all((origin/f).is_file() for f in ('metadata.json','score.json','diagnostic.json','trace.csv','learning_snapshots.json','power_table.json','sensor_packets.csv','firmware.log','ftms_control_packets.json','cadence.csv')),'Remaining reusable baseline missing or incomplete: '+name)
    projected=[{k:r[k] for k in ('target_w','initial_state','sensitivity','seed')} for r in m['runs']]
    check(projected==requested[:count],'Completed cases are not the exact requested prefix')
    names=[r['case'] for r in m['runs']]
    for v in ('baseline','candidate'):
        check({q.name for q in (root/v).iterdir() if q.is_dir()}=={'build',*names},'Unexpected partial/unrecorded variant directory')
    ledger={}
    for r in m['runs']:
        name=r['case'];workout,_=e.extended_workout(r['target_w']);ledger[name]={}
        for v in ('baseline','candidate'):
            dest=root/v/name;ledger[name][v]=hashes(dest)
            for f,key in [('metadata.json','metadata'),('score.json','scores'),('diagnostic.json','diagnostics')]:check(load(dest/f)==r[key][v],'Recorded completed artifact differs')
            meta=r['metadata'][v]
            check(meta['controller_processes']==1 and meta['state_resets_during_ride']==0 and meta['tick_ms']==20 and meta['simulation_seconds']==5400,'Retained clock/process metadata differs')
            check(meta['sensor_schedules']=={'power':{**e.SensorSchedule(1,.2,.1).to_dict(),'dropouts':[[1350,1362]]},'cadence':{**e.SensorSchedule(.25,.1).to_dict(),'dropouts':[]}},'Retained sensor schedule differs')
            check(meta['pause_events']==[{'time_s':1380,'active':False},{'time_s':1440,'active':True}],'Retained pause schedule differs')
            check(meta['cadence_plan_sha256']==e.CadencePlan(workout).sha256,'Retained cadence plan hash differs')
            if v=='candidate':
                cfg=load(dest/'worker_config.json')
                expected_cfg={'candidate_root':str(roots['candidate']),'variant':v,'destination':str(dest.resolve()),'exe':m['executables'][v]['path'],'exe_sha256':m['executables'][v]['sha256'],'case':{k:r[k] for k in ('target_w','initial_state','sensitivity','seed')},'source_provenance':sources[v],'runner_sha256':m['runner_sha256'],'additional_candidate_paths':m['additional_candidate_paths'],'candidate_changed_file_sha256':m['candidate_changed_file_sha256']}
                check(cfg==expected_cfg,'Retained worker config differs from complete expected pin set')
                check(cfg['source_provenance']==sources[v] and cfg['exe_sha256']==m['executables'][v]['sha256'],'Worker pins differ')
                check(e.verify_worker_result(cfg)==r['artifacts'][v]['resource_usage'],'Candidate worker differs')
            else:
                info=load(dest/'reuse-provenance.json');check(info==r['artifacts'][v],'Reuse info differs')
                origin=Path(info['source_path']);original=hashes(origin)
                check(set(original)==set(ledger[name][v]),'Reused file set differs')
                for f,h in ledger[name][v].items():
                    if f!='reuse-provenance.json':check(original[f]==h,'Reused baseline content differs')
        e.verify_pair_files(r['metadata'],root/'baseline'/name/'trace.csv',root/'candidate'/name/'trace.csv',workout)
        check(e.low_probe_comparison(r['scores'])==r['low_power_probe_comparison'],'Completed score gates differ')
        print('Verified retained '+name,flush=True)
    check(e.sha256(report)==original_sha,'Interrupted manifest changed during verification')
    if a.verify_only:
        print(json.dumps({'verified_completed_pairs':count,'remaining_pairs':len(requested)-count,'interrupted_manifest_sha256':original_sha}));return 0
    check(e.sha256(__file__)==helper_sha,'Continuation helper changed during preflight')
    check(shutil.disk_usage(root).free>=1024**3,'Need at least1GiB free before continuation launch')
    archive=root/f'interrupted_after_{count}_pairs';archive.mkdir(exist_ok=False)
    shutil.copy2(report,archive/report.name)
    for filename in ('extended_summary.json','extended_phase_comparison.csv','all_low_phase_budget_comparisons.json'):
        path=root/filename
        if path.exists():path.rename(archive/filename)
    record={'reason':DISK_ERROR,'interrupted_manifest_sha256':original_sha,'interrupted_manifest_path':str(archive/report.name),'completed_pairs_retained':count,'remaining_pairs':len(requested)-count,'runner_sha256':m['runner_sha256'],'continuation_helper_sha256':helper_sha,'executables':m['executables'],'source':sources,'preexisting_artifact_sha256':ledger,'no_completed_rides_repeated':True,'no_new_builds':True}
    e.write_json(archive/'continuation_record.json',record)
    m.setdefault('continuations',[]).append({'record_path':str(archive/'continuation_record.json'),'record_sha256':e.sha256(archive/'continuation_record.json'),'completed_pairs_retained':count,'helper_sha256':helper_sha})
    m['status']='incomplete';m.pop('error',None);e.write_json(report,m)
    try:
        for case in requested[count:]:
            workout,_=e.extended_workout(case['target_w']);name=f'extended_{case["target_w"]}w_{case["initial_state"]}_sensitivity{case["sensitivity"]}-{case["seed"]}'
            diagnostics={};scores={};metadata={};artifacts={}
            for v in ('baseline','candidate'):
                if shutil.disk_usage(root).free<512*1024**2:raise RuntimeError(DISK_ERROR)
                dest=root/v/name;x=m['executables'][v]
                cfg={'candidate_root':str(roots['candidate']),'variant':v,'destination':str(dest.resolve()),'exe':x['path'],'exe_sha256':x['sha256'],'case':case,'source_provenance':sources[v],'runner_sha256':m['runner_sha256'],'additional_candidate_paths':m['additional_candidate_paths'],'candidate_changed_file_sha256':m['candidate_changed_file_sha256']}
                reused=e.try_reuse_baseline(a.reuse_baseline_from.resolve(),dest,case,name,m) if v=='baseline' else None
                if v=='baseline':check(reused is not None,'Required reusable baseline disappeared; refusing a fresh baseline ride')
                if reused is None:
                    e.execute_variant(cfg);artifacts[v]={'reused':False,'path':str(dest.resolve()),'resource_usage':e.verify_worker_result(cfg)}
                else:artifacts[v]=reused
                metadata[v]=load(dest/'metadata.json');scores[v]=load(dest/'score.json');diagnostics[v]=load(dest/'diagnostic.json');gc.collect()
            verified=e.verify_pair_files(metadata,root/'baseline'/name/'trace.csv',root/'candidate'/name/'trace.csv',workout)
            low=e.low_probe_comparison(scores)
            m['runs'].append({**case,'case':name,'diagnostics':diagnostics,'scores':scores,'metadata':metadata,'low_power_probe_comparison':low,'artifacts':artifacts,'paired_trace_verification':verified})
            m['parent_peak_rss_kib']=max(m.get('parent_peak_rss_kib',0),resource.getrusage(resource.RUSAGE_SELF).ru_maxrss);e.write_json(report,m);gc.collect()
            print(f'{name}: low-probe relative={low["relative_passed"]}, absolute={low["absolute_passed"]}',flush=True)
        check(e.preflight(roots['candidate'],m['additional_candidate_paths'])[2]==sources,'Source changed during continuation')
        check(e.candidate_changed_file_hashes(roots['candidate'])==m['candidate_changed_file_sha256'],'Changed files differ after continuation')
        check(e.sha256(e.__file__)==m['runner_sha256'] and e.sha256(__file__)==record['continuation_helper_sha256'],'Runner/helper changed during continuation')
        for v,x in m['executables'].items():check(e.sha256(x['path'])==x['sha256'],'Executable changed during continuation')
        for name,variants in ledger.items():
            for v,original in variants.items():check(hashes(root/v/name)==original,'Retained artifact changed during continuation')
        m['status']='complete';m['low_power_probe_relative_passed']=all(r['low_power_probe_comparison']['relative_passed'] for r in m['runs']);m['low_power_probe_absolute_passed']=all(r['low_power_probe_comparison']['absolute_passed'] for r in m['runs'])
    except BaseException as exc:
        m['status']='failed';m['error']=str(exc);raise
    finally:e.write_json(report,m)
    return 0 if all(r['low_power_probe_comparison']['passed'] for r in m['runs']) else 1

if __name__=='__main__':raise SystemExit(main())
