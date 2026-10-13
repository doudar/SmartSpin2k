"""Run bikes x homing state x physical start positions and tabulate results.

The native bridge is built once; each scenario runs as its own `run`
subprocess so scenarios execute in parallel without sharing firmware globals.
"""

import concurrent.futures
import csv
import json
from pathlib import Path
import subprocess
import sys

from .bridge import ROOT, build

COLUMNS = ["bike", "floor_w", "homing", "start", "seed", "mae_w", "p95_w", "max_w", "over_50w_s", "over_100w_s",
           "steady_mae_w", "settle_median_s", "settle_missed", "min_stop_s", "max_stop_s", "lost_pulses_max",
           "beyond_stop_max", "min_limit_at_s", "max_limit_at_s", "table_cells"]


def _scenario_runs(profiles, args):
    for profile in profiles:
        for homing in args.homing:
            for start in (["profile"] if homing == "homed" else args.starts):
                for floor in args.floors:
                    for seed in args.seeds:
                        yield profile, floor, homing, start, seed


def _summarize(bike, floor, homing, start, seed, data):
    score, run = data["score"], data["run"]
    tracking, travel = score["after_interval_grace"], score["travel"]
    settles = [t["settling_20w_for_5s_s"] for t in score["transitions"]]
    reached = sorted(s for s in settles if s is not None)
    return {"bike": bike, "floor_w": floor, "homing": homing, "start": f"{start}@{run['initial_position_steps']:.0f}", "seed": seed,
            "mae_w": tracking["mae_w"], "p95_w": tracking["p95_absolute_error_w"], "max_w": tracking["max_absolute_error_w"],
            "over_50w_s": tracking["deviations_over_w"]["50"]["seconds"], "over_100w_s": tracking["deviations_over_w"]["100"]["seconds"],
            "steady_mae_w": score.get("steady_after_15s", {}).get("mae_w"),
            "settle_median_s": reached[len(reached)//2] if reached else None, "settle_missed": sum(s is None for s in settles),
            "min_stop_s": travel["seconds_at_min_stop"], "max_stop_s": travel["seconds_at_max_stop"],
            "lost_pulses_max": travel["lost_pulses_max_abs"], "beyond_stop_max": travel["max_command_beyond_stop_steps"],
            "min_limit_at_s": travel.get("first_min_limit_s"), "max_limit_at_s": travel.get("first_max_limit_s"),
            "table_cells": run["power_table"]["supported_cells"]}


def _format(value):
    if value is None: return "—"
    if isinstance(value, float): return f"{value:.1f}" if abs(value) < 1000 else f"{value:.0f}"
    return str(value)


def run_matrix(profiles, args, setup):
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    exe = build(output/"native")
    jobs = []
    for profile, floor, homing, start, seed in _scenario_runs(profiles, args):
        bike = json.loads(Path(profile).read_text(encoding="utf-8")).get("name", Path(profile).stem)
        folder = output/bike/f"floor{floor:g}-{homing}-{start}-seed{seed}"
        command = [sys.executable, "-B", "-m", "scripts.erg_sim", "run", "--exe", str(exe), "--profile", str(profile),
                   "--workout", str(args.workout), "--ftp", str(args.ftp), "--seed", str(seed), "--sensitivity", str(args.sensitivity),
                   "--homing", homing, "--start-position", start, "--floor-watts", str(floor), "--min-watts", str(setup["min_watts"]),
                   "--max-watts", str(setup["max_watts"]), "--shift-step", str(setup["shift_step"]), "--output", str(folder)]
        if not args.plot: command.append("--no-plot")
        jobs.append(((bike, floor, homing, start, seed), folder, command))

    def execute(job):
        key, folder, command = job
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"{key} failed:\n{result.stderr[-2000:]}")
        print(f"done: {' / '.join(map(str, key))}", flush=True)
        return _summarize(*key, json.loads((folder/"score.json").read_text(encoding="utf-8")))

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        rows = list(pool.map(execute, jobs))

    with (output/"summary.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=COLUMNS); writer.writeheader(); writer.writerows(rows)
    lines = ["# ERG scenario matrix", "",
             f"Workout `{Path(args.workout).name}` at FTP {args.ftp:g} W; sensitivity {args.sensitivity:g}; "
             f"min/max watts {setup['min_watts']}/{setup['max_watts']}; shift step {setup['shift_step']}.", "",
             "Error columns use the 3-second interval allowance (`after_interval_grace`). Settling is time to stay within 20 W "
             "for 5 s after transitions of at least 20 W; *missed* counts transitions that never settled. Stop columns are seconds "
             "the physical knob sat on a stop; lost pulses are steps the firmware counted while the bike could not move. "
             "Limit columns are when setStepperMinMax() first replaced the provisional ±200,000,000-step travel.", "",
             "| " + " | ".join(COLUMNS) + " |", "|" + "---|"*len(COLUMNS)]
    lines += ["| " + " | ".join(_format(row[c]) for c in COLUMNS) + " |" for row in rows]
    (output/"summary.md").write_text("\n".join(lines)+"\n", encoding="utf-8")
    print("\n".join(lines))
    print(f"\nPer-run results: {output}")
