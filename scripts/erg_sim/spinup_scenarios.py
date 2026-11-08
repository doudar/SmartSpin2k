"""Start-from-rest ERG scenarios for SS2K's native simulator.
Run from the SS2K repo root:  python3 scripts/erg_sim/spinup_scenarios.py <profile.json> <out_dir> [exe]"""
import sys, json, numpy as np
from pathlib import Path
sys.path.insert(0, '.')
from scripts.erg_sim.workout import Workout, Segment
from scripts.erg_sim.model import BikeConfig
from scripts.erg_sim.bridge import build
from scripts.erg_sim.runner import run

profile = json.loads(Path(sys.argv[1]).read_text()); out = Path(sys.argv[2])
cfg = BikeConfig(**profile["bike"])
exe = Path(sys.argv[3]) if len(sys.argv) > 3 else build(out/"native")
start = profile.get("start_position", 7433)

def metrics(tr, t0, target=165):
    t, p, pos = tr[:, 0], tr[:, 2], tr[:, 4]
    m = t >= t0; t, p, pos = t[m], p[m], pos[m]
    above = float(np.sum(p > target+7) * np.median(np.diff(t)))
    ok = np.abs(p-target) <= 7; settle = None
    for i in range(len(t)):
        j = np.searchsorted(t, t[i]+5)
        if j < len(t) and ok[i:j].all(): settle = t[i]-t0; break
    return dict(peak_w=round(float(p.max())), above_172_s=round(above, 1), settle_s=None if settle is None else round(settle, 1),
                max_pos=int(pos.max()), final_pos=int(np.median(pos[-50:])))

def ramp(t0, top=80, dur=4.0):
    return [(t0, 0), (t0+dur, top)]

results = {}
# A: learned table (8 min varied ERG at 85 rpm), 30 s stop at 0 rpm, restart 0->80 rpm in 4 s with 165 W already set.
warm = [Segment(80, w, w) for w in (150, 200, 250, 180, 220, 160)]
wA = Workout(warm + [Segment(30, 165, 165), Segment(90, 165, 165)])
t0 = 480+30
cadA = np.array([(0, 85), (t0-31, 85), (t0-30.5, 0), (t0, 0), (t0+4, 80), (t0+200, 80)], float)
trA, _ = run(wA, cfg, exe, out/"A", cadence_trace=cadA, initial_position=start)
results["A_restart_target_set"] = metrics(trA, t0)
# B: like A but the app holds 50 W while stopped and sends 165 W 5 s after pedaling resumes (the Oct 31 Grupetto case).
wB = Workout(warm + [Segment(35, 50, 50), Segment(85, 165, 165)])
trB, _ = run(wB, cfg, exe, out/"B", cadence_trace=cadA, initial_position=start)
results["B_restart_target_after_5s"] = metrics(trB, t0+5)
# C: cold start, empty table: 0 rpm 5 s, ramp to 80 in 4 s, 165 W set from the start.
wC = Workout([Segment(120, 165, 165)])
cadC = np.array([(0, 0), (5, 0), (9, 80), (200, 80)], float)
trC, _ = run(wC, cfg, exe, out/"C", cadence_trace=cadC, initial_position=start)
results["C_cold_start"] = metrics(trC, 5)
# D: a 0.5 s drop to 0 rpm and a 4 s ramp back to 80 (not a real stop), 165 W set.
cadD = np.array([(0, 85), (t0-1, 85), (t0-0.5, 0), (t0, 0), (t0+4, 80), (t0+200, 80)], float)
trD, _ = run(wA, cfg, exe, out/"D", cadence_trace=cadD, initial_position=start)
results["D_half_second_drop"] = metrics(trD, t0)
# E: steady 200 W at 85 rpm, a 1 s coast (0 rpm), then a +50 W step 2 s later (mid-ride coast must not freeze ERG).
wE = Workout(warm + [Segment(30, 200, 200), Segment(60, 250, 250)])
cadE = np.array([(0, 85), (480+27, 85), (480+27.2, 0), (480+28.2, 0), (480+28.4, 85), (480+300, 85)], float)
trE, _ = run(wE, cfg, exe, out/"E", cadence_trace=cadE, initial_position=start)
results["E_coast_then_step"] = metrics(trE, 480+30, target=250)
# F: stop during a 300 W interval; the app lowers the target to 150 W while the rider is stopped; restart 0->80 in 4 s.
wF = Workout(warm + [Segment(20, 300, 300), Segment(30, 150, 150), Segment(90, 150, 150)])
tF = 480+20+30
cadF = np.array([(0, 85), (tF-31, 85), (tF-30.5, 0), (tF, 0), (tF+4, 80), (tF+200, 80)], float)
trF, _ = run(wF, cfg, exe, out/"F", cadence_trace=cadF, initial_position=start)
results["F_stop_in_hard_restart_easy"] = metrics(trF, tF, target=150)
# peak_w is the headline for overshoot; settle_s (5 s within +-7 W) shows how quickly the target is reached.
print(json.dumps(results, indent=1))
for name, tr, t in (("A", trA, t0), ("B", trB, t0+5), ("C", trC, 5), ("D", trD, t0), ("E", trE, 480+30), ("F", trF, tF)):
    m = (tr[:, 0] >= t-1) & (tr[:, 0] <= t+30)
    rows = tr[m][::max(1, int(1/np.median(np.diff(tr[:, 0]))))]
    print(name, " ".join(f"{r[0]-t:.0f}s:{r[2]:.0f}W/{r[3]:.0f}rpm/{r[4]:.0f}" for r in rows[:32]))
