#!/usr/bin/env python3
"""Offline evidence renderer: no simulator, firmware build, network, or repo writes.

Only final-source complete manifests with pinned audit hashes can pass the gate.
Read raw traces locally or stream indexed tar members; never extract an archive.
"""
import argparse
import csv
import hashlib
import gzip
import io
import json
from collections import defaultdict
from pathlib import Path
import tarfile

SIGNALS = {'effective_plant': 'Effective fitted output', 'delivered_sensor': 'Delivered sensor'}
ROLES = ('sprint', 'recovery')
VARIANTS = ('baseline', 'candidate')


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''): h.update(chunk)
    return h.hexdigest()


def read_json(path, expected=None):
    if expected is not None and sha(path) != expected:
        raise ValueError(f'Hash mismatch: {path.name}')
    return json.loads(gzip.decompress(path.read_bytes())) if path.suffix == '.gz' else json.loads(path.read_text())


def write_json(path, data):
    payload=(json.dumps(data, indent=2, allow_nan=False) + '\n').encode()
    if path.suffix=='.gz':path.write_bytes(gzip.compress(payload,mtime=0))
    else:path.write_bytes(payload)


def write_csv(path, rows):
    if not rows: raise ValueError(f'Empty export: {path.name}')
    with path.open('w', newline='') as f:
        w = csv.DictWriter(f, list(rows[0])); w.writeheader()
        w.writerows({k: 'null' if v is None else v for k, v in r.items()} for r in rows)


def nested(value, dotpath):
    for key in dotpath.split('.'):
        value = value[int(key)] if isinstance(value, list) else value[key]
    return value


def phases(signal):
    if 'phases' in signal:
        for p in signal['phases']:
            yield p['name'], p['role'], p.get('contains_zero_cadence_pause', False), p['response']
    else:
        for i, p in enumerate(signal['low_power_prefix']):
            yield f'prefix_{i:02d}', 'low_power', False, p
        if 'preparation' in signal: yield 'preparation', 'low_power', False, signal['preparation']
        for role in ROLES: yield role, role, False, signal[role]


def audited_extrema(root, cohort):
    pin=cohort['extrema_audit'];d=read_json(root/pin['path'],pin['sha256'])
    if d['source_manifest_sha256']!=cohort['manifest_sha256']:raise ValueError('Extrema audit source mismatch')
    result={}
    if cohort['name']=='short':
        for case in d['cases']:
            for variant,arm in [('baseline','baseline'),('candidate','near_flat_anchored')]:
                for signal,phases in case['responses'][arm].items():
                    for p in phases.values():result[(case['case'],variant,signal,p['start_s'])]=(p['minimum_w'],p['peak_w'])
    elif cohort['name']=='long':
        for r in d['rows']+d['high_power_rows']:
            if r['band_w']!=10:continue
            for variant in VARIANTS:
                p=r[variant];result[(r['case'],variant,r['signal'],r['phase_start_s'])]=(p['phase_minimum_w'],p['phase_maximum_w'])
    else:
        for r in d['phase_responses']:
            for variant in VARIANTS:
                p=r['variants'][variant]['10'];result[(r['case'],variant,r['signal'],r['start_s'])]=(p['phase_minimum_w'],p['phase_maximum_w'])
    return result


def load_all(root, cfg):
    if cfg.get('status') != 'ready_for_evidence':
        raise ValueError('Evidence remains incomplete: config is not ready_for_evidence')
    if {c['name']: c['expected_pairs'] for c in cfg['cohorts']} != {'short':90, 'long':18, 'cadence':36, 'timing':36}:
        raise ValueError('Final evidence requires the complete predefined four-cohort matrix')
    rows, cohorts, audits, budgets = [], {}, [], []
    for c in cfg['cohorts']:
        if not c.get('manifest') or not c.get('manifest_sha256') or not c.get('audits'):
            raise ValueError(f'Unpinned or unaudited cohort: {c["name"]}')
        p = root / c['manifest']; d = read_json(p, c['manifest_sha256'])
        if d['status'] != 'complete' or len(d['runs']) != c['expected_pairs']:
            raise ValueError(f'Incomplete cohort: {c["name"]}')
        if len(d['requested_cases']) != c['expected_pairs'] or len({r['case'] for r in d['runs']}) != c['expected_pairs']:
            raise ValueError('Requested/completed case coverage differs')
        for variant in VARIANTS:
            f = d['firmware'][variant]
            if f['commit'] != cfg['baseline_commit'] or f['source_sha256'] != cfg[variant + '_sha256']:
                raise ValueError(f'Source mismatch: {c["name"]} {variant}')
        for field, key in [('simulator_sha256', 'simulator_package_sha256'), ('profile_sha256', 'profile_sha256')]:
            if d[field] != cfg[key]: raise ValueError('Model/simulator mismatch')
        for a in c['audits']:
            if not a.get('assertions'): raise ValueError('Audit needs explicit verified assertions')
            value = read_json(root/a['path'], a['sha256'])
            for key, expected in a['assertions'].items():
                if nested(value, key) != expected: raise ValueError(f'Audit assertion differs: {key}')
            audits.append({'cohort': c['name'], 'file': Path(a['path']).name, 'sha256': a['sha256'], 'assertions': a['assertions']})
        if c['name'] == 'long' and not c.get('supplemental_budget_files'):
            raise ValueError('Long cohort needs the complete low-phase budget comparison audit')
        for budget_file in c.get('supplemental_budget_files', []):
            value = read_json(root/budget_file['path'], budget_file['sha256'])
            if value.get('status') != 'complete' or len(value['rows']) != budget_file['expected_rows']:
                raise ValueError('Incomplete supplemental low-power budget evidence')
            for item in value['rows']:
                budgets.append({'cohort':c['name'], 'case':item['case'], 'kind':'all_low_phase_budget_comparisons',
                    'phase':item['phase'], 'role':item['role'], 'signal':item['signal'],
                    'pause':item['pause'], 'scores':item['scores'], 'checks':item['comparison']})
        extra_preparation={}
        if c['name'] in ('short','timing'):
            pa=c.get('preparation_audit')
            if not pa: raise ValueError('All-phase evidence needs the independently audited preparation interval')
            av=read_json(root/pa['path'],pa['sha256'])
            if av['source_manifest_sha256']!=c['manifest_sha256']: raise ValueError('Preparation audit source mismatch')
            if c['name']=='short':
                for item in av['cases']:
                    for variant,arm in [('baseline','baseline'),('candidate','near_flat_anchored')]:
                        for signal in SIGNALS:
                            t=item['responses'][arm][signal]['lead_in'];b=t['bands']['10']
                            extra_preparation[(item['case'],variant,signal)]={'target_w':t['target_w'],'start_s':t['start_s'],'duration_s':t['end_s']-t['start_s'],
                                'peak_power_w':t['peak_w'],'peak_overshoot_w':t['overshoot_w'],'max_undershoot_w':t['undershoot_w'],'mae_w':t['mae_w'],
                                'bands':{'10':dict(b,band_w=10,dwell_s=5)},'goal':{'met':b['final_goal_met']}}
            else:
                for item in av['phase_responses']:
                    if item['phase']!='preparation':continue
                    for variant in VARIANTS:
                        t=item['variants'][variant]['10']
                        extra_preparation[(item['case'],variant,item['signal'])]={'target_w':item['phase_target_w'],'start_s':item['start_s'],'duration_s':item['end_s']-item['start_s'],
                            'peak_power_w':t['phase_maximum_w'],'peak_overshoot_w':t['peak_overshoot_w'],'max_undershoot_w':t['max_undershoot_w'],'mae_w':t['phase_mae_w'],
                            'bands':{'10':t},'goal':{'met':t['strict_final_under_10s']}}
        extrema=audited_extrema(root,c)
        cohorts[c['name']] = (c, p.parent, d)
        for r in d['runs']:
            for key in ('low_power_regression', 'low_power_probe_comparison'):
                if key in r: budgets.append({'cohort': c['name'], 'case': r['case'], 'kind': key, 'checks': r[key]})
            for variant in VARIANTS:
                for signal in SIGNALS:
                    signal_data=dict(r['diagnostics'][variant]['signals'][signal])
                    if extra_preparation:signal_data['preparation']=extra_preparation[(r['case'],variant,signal)]
                    allphases=list(phases(signal_data))
                    if len(allphases)!=(52 if c['name']=='long' else 9):raise ValueError('Missing or duplicated workout phase')
                    for phase, role, pause, v in allphases:
                        b = v['bands']['10']; final = b['final_settling_s']
                        observed_min,observed_max=extrema[(r['case'],variant,signal,v['start_s'])]
                        if abs(observed_max-v['peak_power_w'])>1e-7:raise ValueError('Stored peak differs from independently audited actual maximum')
                        assert v['goal']['met'] == (final is not None and final < 10)
                        assert b['band_w'] == 10 and b['dwell_s'] == 5
                        rows.append({'cohort': c['name'], 'case': r['case'], 'variant': variant, 'signal': signal,
                            'phase': phase, 'role': role, 'zero_cadence_pause': pause,
                            'sprint_target_w': r['target_w'], 'initial_state': r['initial_state'],
                            'sensitivity': r['sensitivity'], 'seed': r['seed'],
                            'cadence_cue_rpm': r.get('sprint_cadence_cue_rpm'), 'timing_regime': r.get('regime'),
                            'target_w': v['target_w'], 'start_s': v['start_s'], 'duration_s': v['duration_s'],
                            'peak_w': v['peak_power_w'], 'overshoot_w': v['peak_overshoot_w'],
                            'minimum_w': observed_min, 'mae_w': v['mae_w'],
                            'outside_10w_s': b['outside_band_s'], 'above_10w_s': b['above_band_s'],
                            'first_dwell_s': b['first_dwell_entry_s'], 'final_tail_s': final,
                            'goal_met': v['goal']['met']})
    return rows, cohorts, audits, budgets


def pair_rows(rows):
    pairs = defaultdict(dict)
    for r in rows: pairs[(r['cohort'], r['case'], r['signal'], r['phase'])][r['variant']] = r
    assert all(set(v) == set(VARIANTS) for v in pairs.values())
    return list(pairs.values())


def aggregate(rows):
    groups = defaultdict(list)
    for r in rows:
        if r['role'] in ROLES:
            for target in ('all', str(r['sprint_target_w'])):
                groups[(r['cohort'], r['signal'], r['role'], target, r['variant'])].append(r)
    result = []
    for keys, values in sorted(groups.items()):
        result.append(dict(zip(('cohort', 'signal', 'role', 'sprint_target_w', 'variant'), keys),
            attempts=len(values), goal_met=sum(r['goal_met'] for r in values),
            final_tail_found=sum(r['final_tail_s'] is not None for r in values),
            null_final_tail=sum(r['final_tail_s'] is None for r in values),
            overshoot_above_0w=sum(r['overshoot_w'] > 0 for r in values),
            overshoot_above_10w=sum(r['overshoot_w'] > 10 for r in values),
            overshoot_above_20w=sum(r['overshoot_w'] > 20 for r in values),
            worst_peak_w=max(r['peak_w'] for r in values),
            worst_overshoot_w=max(r['overshoot_w'] for r in values),
            mean_phase_mae_w=sum(r['mae_w'] for r in values) / len(values)))
    return result


def choose_examples(pairs):
    def key(p):
        r = p['candidate']; return (r['cohort'], r['case'], r['phase'])
    high = [p for p in pairs if p['candidate']['signal'] == 'effective_plant' and p['candidate']['role'] == 'sprint']
    gains = sorted([p for p in high if p['baseline']['overshoot_w'] > p['candidate']['overshoot_w']],
                   key=lambda p: (p['baseline']['overshoot_w'] - p['candidate']['overshoot_w'], key(p)))
    choices = []
    if gains:
        choices += [('Median positive sprint overshoot reduction', gains[(len(gains)-1)//2]),
                    ('Largest sprint overshoot reduction', gains[-1])]
    for title, pool in [('Worst remaining fitted sprint MAE', high),
                        ('Worst remaining fitted recovery MAE', [p for p in pairs if p['candidate']['signal'] == 'effective_plant' and p['candidate']['role'] == 'recovery']),
                        ('Worst remaining delivered sprint MAE', [p for p in pairs if p['candidate']['signal'] == 'delivered_sensor' and p['candidate']['role'] == 'sprint'])]:
        choices.append((title, max(pool, key=lambda p: (p['candidate']['mae_w'], key(p)))))
    low = [p for p in pairs if p['candidate']['signal'] == 'effective_plant' and p['candidate']['role'] == 'low_power' and not p['candidate']['zero_cadence_pause']]
    choices.append(('Largest ordinary low-power MAE increase', max(low, key=lambda p: (p['candidate']['mae_w']-p['baseline']['mae_w'], key(p)))))
    unique={}
    for title,pair in choices:
        r=pair['candidate'];identity=(r['cohort'],r['case'],r['phase'])
        if identity in unique:
            unique[identity]['selection']+=' + '+title
            unique[identity]['selection_metrics'].append(title)
        else:unique[identity]={'selection':title,'selection_metrics':[title],'baseline':pair['baseline'],'candidate':r}
    return list(unique.values())


def trace_stream(path, wanted, expected_hash=None):
    """Read every raw byte, retaining only a phase. Hash check before return."""
    h = hashlib.sha256(); data = []
    for i, line in enumerate(path):
        h.update(line)
        if i == 0: continue
        # End-to-end member hashing continues after the plotted window.
        t = float(line.split(b',', 1)[0])
        if wanted[0] <= t < wanted[1]:
            parts = line.split(b','); data.append([float(parts[j]) for j in (0, 1, 2, 14)])
    digest = h.hexdigest()
    if expected_hash and digest != expected_hash: raise ValueError('Selected trace digest mismatch')
    return data, digest


def selected_trace(root, folder, case, variant, interval, config):
    member = f'{variant}/{case}/trace.csv'; path = folder/member
    if path.is_file():
        with path.open('rb') as f: return trace_stream(f, interval)
    if not config.get('archive_index'): raise ValueError('Missing trace and no verified archive index')
    index = read_json(root/config['archive_index'], config.get('archive_index_sha256'))
    if not index['streamed_tar_member_sha256_passed']: raise ValueError('Archive member audit not verified')
    entries = {e['path']: e for e in index['files']}
    entry = entries[member]
    archive = root/config['archive']
    if sha(archive) != index['archive_sha256']: raise ValueError('Archive digest mismatch')
    with tarfile.open(archive, 'r|gz') as tf:
        for ti in tf:
            if ti.name.removeprefix('./') == member:
                if ti.size != entry['bytes']: raise ValueError('Archive member size mismatch')
                return trace_stream(tf.extractfile(ti), interval, entry['sha256'])
    raise ValueError('Selected member absent')


def plot_all(out, rows, pairs, examples, cohorts, root):
    import numpy as np
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    plt.rcParams.update({'font.family':'DejaVu Sans', 'font.size':10, 'axes.spines.top':False,
        'axes.spines.right':False, 'axes.titleweight':'bold', 'figure.facecolor':'white',
        'axes.grid':True, 'grid.alpha':.16, 'savefig.facecolor':'white', 'lines.linewidth':1.8})
    colors = {'baseline':'#7b8495', 'candidate':'#067a8a'}
    labels = list(cohorts)
    for role in ROLES:
        fig, axes = plt.subplots(len(labels), 4, figsize=(16, 3*len(labels)), squeeze=False)
        for row, cohort in enumerate(labels):
            for col, (signal, metric) in enumerate((('effective_plant','overshoot_w'),('effective_plant','mae_w'),('delivered_sensor','overshoot_w'),('delivered_sensor','mae_w'))):
                ax = axes[row,col]
                for variant in VARIANTS:
                    values = sorted(r[metric] for r in rows if r['cohort']==cohort and r['role']==role and r['signal']==signal and r['variant']==variant)
                    ax.step(values, np.arange(1,len(values)+1)/len(values),where='post',color=colors[variant], label=f'{variant.title()} (n={len(values)})')
                ax.set_title(f'{cohort.title()} · {SIGNALS[signal]}',fontsize=10)
                ax.set_xlabel(('Peak overshoot' if metric=='overshoot_w' else 'Phase MAE')+' (W)')
                ax.set_ylim(0,1.04);ax.set_xlim(left=0)
                if col==0: ax.set_ylabel('Share of all efforts at or below')
                ax.legend(loc='lower right',frameon=False,fontsize=8)
        fig.suptitle(f'{role.title()} distributions · every requested effort, unsmoothed',fontsize=17,y=1.01)
        fig.tight_layout();fig.savefig(out/'plots'/f'{role}_distributions.png',dpi=160,bbox_inches='tight');plt.close(fig)
    fig, axes = plt.subplots(2,2,figsize=(14,9))
    status_colors = ['#067a8a','#e4ab4b','#c5cbd5']
    for row, signal in enumerate(SIGNALS):
        for col, role in enumerate(ROLES):
            ax=axes[row,col];pos=0;yt=[];yl=[]
            for cohort in labels:
                for variant in VARIANTS:
                    rs=[r for r in rows if r['cohort']==cohort and r['role']==role and r['signal']==signal and r['variant']==variant]
                    counts=[sum(r['goal_met'] for r in rs),sum(r['final_tail_s'] is not None and not r['goal_met'] for r in rs),sum(r['final_tail_s'] is None for r in rs)]
                    left=0
                    for i,count in enumerate(counts):
                        ax.barh(pos,count/len(rs)*100,left=left,color=status_colors[i],height=.78,label=['Goal <10 s','Late qualified tail','No final tail'][i] if pos==0 else None)
                        if count: ax.text(left+count/len(rs)*50,pos,str(count),ha='center',va='center',fontsize=9)
                        left+=count/len(rs)*100
                    yt.append(pos);yl.append(f'{cohort} · {variant} /{len(rs)}');pos+=1
                pos+=.3
            ax.set_yticks(yt,yl);ax.invert_yaxis();ax.set_xlim(0,100);ax.set_xlabel('Percent of every effort (numbers = counts)')
            ax.set_title(f'{SIGNALS[signal]} · {role}');ax.grid(axis='x')
    handles,legend=axes[0,0].get_legend_handles_labels();fig.legend(handles,legend,loc='lower center',ncol=3,frameon=False)
    fig.suptitle('Strict ±10 W final tails · nulls stay in the denominator',fontsize=17)
    fig.tight_layout(rect=(0,.06,1,.97));fig.savefig(out/'plots'/'final_tail_outcomes.png',dpi=160);plt.close(fig)
    fig, axes = plt.subplots(2,2,figsize=(14,10))
    cohort_colors={'short':'#067a8a','long':'#7053a3','cadence':'#c87535','timing':'#5079ad'}
    for i,signal in enumerate(SIGNALS):
        for j,role in enumerate(('low_power','recovery')):
            ax=axes[i,j]; allvalues=[]
            for cohort in labels:
                selected=[p for p in pairs if p['candidate']['signal']==signal and p['candidate']['role']==role and p['candidate']['cohort']==cohort and not p['candidate']['zero_cadence_pause']]
                b=[p['baseline']['mae_w'] for p in selected];c=[p['candidate']['mae_w'] for p in selected];allvalues+=b+c
                ax.scatter(b,c,s=19,alpha=.45,color=cohort_colors[cohort],label=f'{cohort} (n={len(selected)})',edgecolors='none')
            limit=max(allvalues)*1.04;ax.plot([0,limit],[0,limit],color='#7b8495',ls='--',lw=1)
            ax.set_xlim(0,limit);ax.set_ylim(0,limit);ax.set_aspect('equal',adjustable='box')
            ax.set_xlabel('Baseline phase MAE (W)');ax.set_ylabel('Candidate phase MAE (W)')
            ax.set_title(f'{SIGNALS[signal]} · {"ordinary low target" if role=="low_power" else "recovery"}')
            ax.legend(frameon=False,fontsize=8)
    fig.suptitle('All diagnosed low-power phases · above the diagonal is worse',fontsize=17)
    fig.text(.5,.015,'Every paired phase is plotted; coincident points overlap. Scheduled zero-cadence pauses remain in the CSV, excluded here.',ha='center',fontsize=9)
    fig.tight_layout(rect=(0,.035,1,.96));fig.savefig(out/'plots'/'low_power_tradeoffs.png',dpi=160);plt.close(fig)
    trace_hashes=[]
    for number, ex in enumerate(examples,1):
        r=ex['candidate']; c, folder, _=cohorts[r['cohort']];start=r['start_s'];end=start+r['duration_s']
        traces={}
        for variant in VARIANTS:
            data,digest=selected_trace(root,folder,r['case'],variant,(start,end),c);traces[variant]=np.array(data)
            trace_hashes.append({'cohort':r['cohort'],'case':r['case'],'variant':variant,'trace_sha256':digest,'phase':r['phase']})
        fig,axes=plt.subplots(2,1,figsize=(12,7),sharex=True)
        for ax,(signal,column) in zip(axes,(('effective_plant',2),('delivered_sensor',3))):
            ax.axhspan(r['target_w']-10,r['target_w']+10,color='#b1dfcc',alpha=.35,label='±10 W band')
            ax.axhline(r['target_w'],color='#24364c',ls=':',lw=1.2,label='Target')
            for variant in VARIANTS:
                t=traces[variant]
                ax.plot(t[:,0]-start,t[:,column],color=colors[variant],label=variant.title(),drawstyle='steps-post' if signal=='delivered_sensor' else 'default')
                match=next(x for x in rows if x['cohort']==r['cohort'] and x['case']==r['case'] and x['phase']==r['phase'] and x['signal']==signal and x['variant']==variant)
                dt=np.diff(np.r_[t[:,0],end]);actual=float(np.average(abs(t[:,column]-r['target_w']),weights=dt))
                if abs(actual-match['mae_w'])>1e-6 or abs(max(t[:,column])-match['peak_w'])>1e-6 or abs(min(t[:,column])-match['minimum_w'])>1e-6:raise ValueError('Trace/manifest metric mismatch')
                inside=abs(t[:,column]-r['target_w'])<=10;outside=np.flatnonzero(~inside);tail=(outside[-1]+1) if len(outside) else 0
                final=float(t[tail,0]-start) if tail<len(t) and end-t[tail,0]>=5-1e-9 else None
                if final is None:assert match['final_tail_s'] is None
                else:assert match['final_tail_s'] is not None and abs(final-match['final_tail_s'])<1e-7
            ax.set_ylabel(f'{SIGNALS[signal]} (W)');ax.legend(loc='best',ncol=4,frameon=False)
        axes[-1].set_xlabel(f'Seconds from {r["phase"]} entry at {start:g} s');axes[-1].set_xlim(0,r['duration_s'])
        import textwrap
        fig.suptitle(textwrap.fill(ex['selection'],85),fontsize=16)
        axes[0].set_title(f'{r["cohort"]} · {r["case"]} · {r["phase"]}',fontsize=10,fontweight='normal')
        fig.tight_layout();fig.savefig(out/'plots'/f'example_{number:02d}.png',dpi=160);plt.close(fig)
    return trace_hashes


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True);p.add_argument('--config',type=Path,default=Path(__file__).with_name('build_config.json'));p.add_argument('--output',type=Path,default=Path(__file__).parent);a=p.parse_args()
    cfg=read_json(a.config);rows,cohorts,audits,budgets=load_all(a.root,cfg);out=a.output;out.mkdir(exist_ok=True);(out/'plots').mkdir(exist_ok=True)
    pairs=pair_rows(rows);counts=aggregate(rows);examples=choose_examples(pairs)
    write_csv(out/'all_diagnosed_phase_metrics.csv',rows);write_csv(out/'aggregate_counts.csv',counts)
    changes=[]
    for pair in pairs:
        b,c=pair['baseline'],pair['candidate'];changes.append({k:c[k] for k in ('cohort','case','signal','phase','role','target_w','zero_cadence_pause')} | {'baseline_mae_w':b['mae_w'],'candidate_mae_w':c['mae_w'],'mae_delta_w':c['mae_w']-b['mae_w'],'baseline_final_tail_s':b['final_tail_s'],'candidate_final_tail_s':c['final_tail_s'],'goal_lost':b['goal_met'] and not c['goal_met'],'goal_gained':c['goal_met'] and not b['goal_met'],'tail_lost':b['final_tail_s'] is not None and c['final_tail_s'] is None})
    write_csv(out/'all_paired_phase_changes.csv',changes);write_json(out/'preserved_budget_checks.json.gz',budgets);write_json(out/'example_selection.json',examples)
    traces=plot_all(out,rows,pairs,examples,cohorts,a.root)
    write_json(out/'evidence_build_audit.json',{'status':'complete','rows':len(rows),'paired_phases':len(pairs),'cohorts':{k:{'pairs':len(d['runs']),'manifest_sha256':c['manifest_sha256']} for k,(c,_,d) in cohorts.items()},'audits':audits,'selected_trace_checks':traces,'candidate_sha256':cfg['candidate_sha256'],'policy':'Null final tails retained; <10 s entry, >=5 s final continuous ±10 W tail; no smoothing; no acceptance decision.'})
    print(json.dumps({'status':'complete','rows':len(rows),'plots':len(list((out/'plots').glob('*.png')))}))

if __name__=='__main__':main()
