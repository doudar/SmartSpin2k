#!/usr/bin/env python3
"""Independent checks on delivered CSV/JSON exports; no simulation or builds."""
import csv
import argparse
import gzip
import hashlib
import json
import math
from collections import Counter
from pathlib import Path

ROOT=Path(__file__).resolve().parent

def main():
    parser=argparse.ArgumentParser();parser.add_argument("--root",type=Path,default=ROOT.parent);args=parser.parse_args()
    rows=list(csv.DictReader((ROOT/'all_diagnosed_phase_metrics.csv').open()))
    changes=list(csv.DictReader((ROOT/'all_paired_phase_changes.csv').open()))
    counts=list(csv.DictReader((ROOT/'aggregate_counts.csv').open()))
    budgets=json.loads(gzip.decompress((ROOT/'preserved_budget_checks.json.gz').read_bytes()))
    assert len(rows)==9576 and len(changes)==4788
    ids=[(r['cohort'],r['case'],r['variant'],r['signal'],r['phase']) for r in rows]
    assert len(set(ids))==len(ids)
    for cohort,n,phases in [('short',90,9),('long',18,52),('cadence',36,9),('timing',36,9)]:
        rs=[r for r in rows if r['cohort']==cohort]
        assert len({r['case'] for r in rs})==n and len(rs)==n*phases*4
    numerical=('target_w','start_s','duration_s','peak_w','overshoot_w','minimum_w','mae_w','outside_10w_s','above_10w_s')
    nulls=0
    for r in rows:
        assert all(r[k]!='' and math.isfinite(float(r[k])) for k in numerical)
        assert r['final_tail_s']!='' and r['first_dwell_s']!=''
        f=r['final_tail_s'];nulls+=f=='null'
        assert (r['goal_met']=='True') == (f!='null' and float(f)<10)
        if f!='null':assert float(f)<=float(r['duration_s'])-5+1e-9
    checks=0
    for a in counts:
        rs=[r for r in rows if all(r[k]==a[k] for k in ('cohort','signal','role','variant')) and (a['sprint_target_w']=='all' or float(r['sprint_target_w'])==float(a['sprint_target_w']))]
        expect={'attempts':len(rs),'goal_met':sum(r['goal_met']=='True' for r in rs),'final_tail_found':sum(r['final_tail_s']!='null' for r in rs),'null_final_tail':sum(r['final_tail_s']=='null' for r in rs)}
        for band in [0,10,20]:expect[f'overshoot_above_{band}w']=sum(float(r['overshoot_w'])>band for r in rs)
        for k,v in expect.items():assert int(a[k])==v;checks+=1
        assert abs(float(a['worst_peak_w'])-max(float(r['peak_w']) for r in rs))<1e-9;checks+=1
    low=[b for b in budgets if b['kind']=='all_low_phase_budget_comparisons'];assert len(low)==1764
    assert sum(not b['checks']['relative_passed'] for b in low)==12
    assert sum(not b['checks']['absolute_passed'] for b in low)==355
    for item in json.loads((ROOT/'reproduction_manifest.json').read_text())['dependencies']:
        assert hashlib.sha256((ROOT/item['path']).read_bytes()).hexdigest()==item['sha256']
    # True extrema are verified independently of nonnegative overshoot/undershoot.
    config=json.loads((ROOT/'build_config.json').read_text());actual={}
    for cohort in config['cohorts']:
        p=args.root/cohort['extrema_audit']['path'];assert hashlib.sha256(p.read_bytes()).hexdigest()==cohort['extrema_audit']['sha256']
        d=json.loads(gzip.decompress(p.read_bytes()))
        if cohort['name']=='short':
            for case in d['cases']:
                for variant,arm in [('baseline','baseline'),('candidate','near_flat_anchored')]:
                    for signal,ps in case['responses'][arm].items():
                        for v in ps.values():actual[(cohort['name'],case['case'],variant,signal,float(v['start_s']))]=(v['minimum_w'],v['peak_w'])
        elif cohort['name']=='long':
            for phase in d['rows']+d['high_power_rows']:
                if phase['band_w']!=10:continue
                for variant in ('baseline','candidate'):
                    v=phase[variant];actual[(cohort['name'],phase['case'],variant,phase['signal'],float(phase['phase_start_s']))]=(v['phase_minimum_w'],v['phase_maximum_w'])
        else:
            for phase in d['phase_responses']:
                for variant in ('baseline','candidate'):
                    v=phase['variants'][variant]['10'];actual[(cohort['name'],phase['case'],variant,phase['signal'],float(phase['start_s']))]=(v['phase_minimum_w'],v['phase_maximum_w'])
    for row in rows:
        lowval,highval=actual[(row['cohort'],row['case'],row['variant'],row['signal'],float(row['start_s']))]
        assert abs(float(row['minimum_w'])-lowval)<1e-9
        assert abs(float(row['peak_w'])-highval)<1e-9
    above_target=sum(float(r['minimum_w'])>float(r['target_w']) for r in rows)
    summary={'status':'PASS','phase_rows':len(rows),'paired_signal_phases':len(changes),'explicit_null_final_tails':nulls,'aggregate_field_checks':checks,'independent_actual_extrema_checks':len(rows)*2,'true_minima_above_target':above_target,'low_budget_rows':len(low),'low_relative_failures':12,'low_absolute_failures':355,'duplicate_rows':0,'blank_tail_cells':0,'all_reproduction_hashes_verified':True,'scope':'Independent export consistency checks; no raw simulation or firmware build.'}
    (ROOT/'export_validation.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary))

if __name__=='__main__':main()
