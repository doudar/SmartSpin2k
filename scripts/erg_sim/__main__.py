"""python -m scripts.erg_sim {fit,run,validate} --help"""

import argparse
from dataclasses import asdict
import json
import hashlib
from pathlib import Path

import numpy as np

from .bridge import ROOT, build
from .calibrate import fit_bike, metrics, predict_reference
from .model import BikeConfig, RiderConfig
from .reference import first_erg_section, import_reference, load_reference
from .report import plot_reference, plot_run, save_run
from .runner import run
from .scoring import grade
from .workout import Workout

HERE = Path(__file__).resolve().parent


def write_json(path, data):
    Path(path).write_text(json.dumps(data, indent=2)+'\n', encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    fit = sub.add_parser("fit", help="align FIT/log, fit only the first 65%, evaluate the held-out remainder")
    fit.add_argument("--log", required=True, type=Path)
    fit.add_argument("--fit", required=True, type=Path)
    fit.add_argument("--train-fraction", default=.65, type=float)
    fit.add_argument("--output", type=Path, default=ROOT/"test/output/erg_fit")
    fit.add_argument("--no-plot", action="store_true")
    sim = sub.add_parser("run", help="run an ERG workout through production C++ with simulated bike feedback")
    sim.add_argument("--workout", type=Path, default=HERE/"workouts/Random_Attacks.zwo")
    sim.add_argument("--profile", type=Path, default=HERE/"profiles/ride_20260927.json")
    sim.add_argument("--ftp", type=float, default=305)
    sim.add_argument("--seed", type=int, default=27)
    sim.add_argument("--speed", type=float, default=0, help="0: unlimited; 1: real time; 20: 20x real time")
    sim.add_argument("--tick-ms", type=int, default=20)
    sim.add_argument("--sensitivity", type=float, default=5)
    sim.add_argument("--rider-mode", choices=("planned", "stochastic"), default="planned",
                     help="fixed interval callouts for comparisons, or the original stochastic stress rider")
    sim.add_argument("--cadence", type=float, default=None)
    sim.add_argument("--variation", type=float, default=None)
    sim.add_argument("--lag-scale", type=float, default=1, help="scale both fitted delay and lag for robustness testing")
    sim.add_argument("--output", type=Path, default=ROOT/"test/output/erg_sim")
    sim.add_argument("--no-plot", action="store_true")
    validate = sub.add_parser("validate", help="evaluate stored fit and close the loop on the first continuous recorded ERG interval")
    validate.add_argument("--reference", type=Path, default=ROOT/"test/data/erg_reference_20260927.csv")
    validate.add_argument("--profile", type=Path, default=HERE/"profiles/ride_20260927.json")
    validate.add_argument("--output", type=Path, default=ROOT/"test/output/erg_validation")
    validate.add_argument("--no-plot", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.command == "fit":
        data, alignment, events = import_reference(args.log, args.fit, args.output/"ride.csv")
        profile, prediction, baseline = fit_bike(data, args.train_fraction)
        profile["alignment"] = alignment
        # Identify baseline cadence statistics only from the training portion.
        cad = data[(data[:, 0] < profile["identification"]["training_end_s"]) & (data[:, 2] >= 40), 2]
        cadence_rho = float(np.corrcoef(cad[:-1], cad[1:])[0, 1])
        rider = RiderConfig(preferred_cadence=float(np.median(cad)), variation_rpm=float(np.std(cad)),
                             correlation_s=float(-np.median(np.diff(data[:, 0]))/np.log(np.clip(cadence_rho, .01, .999))))
        profile["rider"] = asdict(rider)
        profile["rider_identification"] = {"training_cadence_median_rpm": float(np.median(cad)),
                                             "training_cadence_std_rpm": float(np.std(cad)),
                                             "lag_one_correlation": cadence_rho,
                                             "note": "Cadence distribution and autocorrelation estimated from training data. Intentional changes are mixed with wander; load response and fatigue remain assumptions."}
        profile["sources"] = {"log_sha256": hashlib.sha256(args.log.read_bytes()).hexdigest(),
                              "fit_sha256": hashlib.sha256(args.fit.read_bytes()).hexdigest()}
        write_json(args.output/"profile.json", profile)
        write_json(args.output/"target_events.json", events.tolist())
        if not args.no_plot: plot_reference(args.output, data, prediction, baseline, profile["identification"]["training_end_s"])
        print(json.dumps(profile, indent=2))
    elif args.command == "run":
        profile = json.loads(args.profile.read_text(encoding="utf-8"))
        cfg = BikeConfig(**profile["bike"])
        if not np.isfinite(args.lag_scale) or args.lag_scale < 0: parser.error("lag-scale must be finite and nonnegative")
        cfg.delay_s *= args.lag_scale; cfg.lag_s *= args.lag_scale
        rider = RiderConfig(**profile.get("rider", {}))
        if args.rider_mode == "planned" and (args.cadence is not None or args.variation is not None):
            parser.error("Use --rider-mode stochastic with --cadence/--variation, or edit the workout cadence sidecar")
        if args.cadence is not None: rider.preferred_cadence = args.cadence
        if args.variation is not None: rider.variation_rpm = args.variation
        rider.__post_init__()
        workout = Workout.load(args.workout, args.ftp)
        exe = build(args.output/"native")
        trace, metadata = run(workout, cfg, exe, args.output, seed=args.seed, speed=args.speed, tick_ms=args.tick_ms,
                              sensitivity=args.sensitivity, rider_config=rider, rider_mode=args.rider_mode)
        metadata.update(workout=args.workout.name, ftp_w=args.ftp, profile=str(args.profile), bike=asdict(cfg))
        scores = grade(trace, workout.duration, workout)
        save_run(args.output, trace, scores, metadata)
        if not args.no_plot: plot_run(args.output, trace, f"{args.workout.stem} — FTP {args.ftp:g} W, seed {args.seed}", workout=workout, ftp=args.ftp, score=scores)
        print((args.output/"report.md").read_text(encoding="utf-8"))
        print(f"Simulated {workout.duration:.0f}s in {metadata['wall_seconds']:.2f}s. Results: {args.output}")
    else:
        data = load_reference(args.reference)
        profile = json.loads(args.profile.read_text(encoding="utf-8"))
        cfg = BikeConfig(**profile["bike"])
        pred = predict_reference(data, cfg)
        holdout = (data[:, 0] >= profile["identification"]["training_end_s"]) & (data[:, 2] >= 40) & (data[:, 1] > 0)
        report = {"plant_holdout": metrics(data[holdout, 1], pred[holdout])}
        section, workout = first_erg_section(data)
        duration = workout.duration
        exe = build(args.output/"native")
        trace, metadata = run(workout, cfg, exe, args.output, cadence_trace=section[:, [0, 2]],
                              initial_position=round(section[0, 3]), initial_power=round(section[0, 1]))
        actual_power = np.interp(trace[:, 0], section[:, 0], section[:, 1])
        actual_pos = np.interp(trace[:, 0], section[:, 0], section[:, 3])
        report["closed_loop_power_vs_recording"] = metrics(actual_power, trace[:, 2])
        report["closed_loop_position_mae_steps"] = float(np.mean(np.abs(trace[:, 4]-actual_pos)))
        report["closed_loop_duration_s"] = duration
        report["limitations"] = ["Original table, firmware commit and settings snapshot are unavailable; native starts with an empty table.",
                                  "Targets reconstructed at one-second log resolution; cadence is replayed, power is generated by the bike.",
                                  "Plant fit and closed-loop reproduction are separate validations; neither promises exact historical commands."]
        write_json(args.output/"validation.json", report)
        save_run(args.output, trace, grade(trace, duration, workout), metadata)
        if not args.no_plot:
            plot_run(args.output, trace, "Recorded workout/cadence → current production ERG + fitted bike", workout=workout,
                     score=grade(trace, duration, workout))
            import matplotlib.pyplot as plt
            fig, ax = plt.subplots(2, 1, figsize=(15, 7), sharex=True, constrained_layout=True)
            ax[0].plot(trace[:, 0]/60, actual_power, label="Recorded power", linewidth=.8)
            ax[0].plot(trace[:, 0]/60, trace[:, 2], label="Closed-loop power", linewidth=.8)
            ax[0].set(ylabel="Power (W)"); ax[0].legend()
            ax[1].plot(trace[:, 0]/60, actual_pos, label="Recorded position", linewidth=.8)
            ax[1].plot(trace[:, 0]/60, trace[:, 4], label="Closed-loop position", linewidth=.8)
            ax[1].set(ylabel="Steps", xlabel="Time (min)"); ax[1].legend()
            fig.savefig(args.output/"comparison.png", dpi=140); plt.close(fig)
        print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
