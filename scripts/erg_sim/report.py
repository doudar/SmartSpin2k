"""Portable JSON/Markdown results and static matplotlib figures."""

import json
from pathlib import Path


def save_run(output, trace, report, metadata):
    output = Path(output)
    (output/"score.json").write_text(json.dumps({"run": metadata, "score": report}, indent=2), encoding="utf-8")
    text = ["# ERG simulator result", "", f"Workout: {metadata.get('workout', 'reference replay')}", "",
            f"FTP: {metadata.get('ftp_w', 'n/a')} W; duration: {report['duration_s']/60:.1f} min; seed: {metadata['seed']}.", "",
            f"Maximum absolute error: **{report['max_absolute_error_w']:.1f} W**. "
            f"95th percentile: **{report['p95_absolute_error_w']:.1f} W**. "
            f"Mean absolute error: **{report['mae_w']:.1f} W**.", "",
            "| Strictly over | Samples | Episodes | Seconds | Time | Longest episode |",
            "|---|---:|---:|---:|---:|---:|"]
    for threshold, values in report["deviations_over_w"].items():
        text.append(f"| {threshold} W | {values['samples']} | {values['episodes']} | {values['seconds']:.1f} | {values['percent_time']:.2f}% | {values['longest_episode_s']:.1f} s |")
    text += ["", "Counts are one per sensor report, not per firmware tick. Episodes group adjacent reports; durations use sample-and-hold. "
             "All warmup/ramp/transition errors count in the full-workout grade. The JSON also contains signed peaks, RMS error, "
             "steady tracking, and time to stay within 20 W for five seconds after each large transition.", "",
             "The firmware starts with an empty power table and learns during the ride. "
             "Cadence variability and assumed mechanical limits are configurable. This is software simulation, not hardware validation."]
    if "power_table" in metadata:
        table = metadata["power_table"]
        text += ["", f"Final learned table: **{table['supported_cells']}/{table['total_cells']} supported cells**. "
                 "[Table graph](power_table.png) · [PTAB snapshot](learned.ptab) · [CSV](power_table.csv).",
                 "", f"Cadence source: {metadata['cadence_source']}. Plan fingerprint: `{metadata.get('cadence_plan_sha256') or 'n/a'}`."]
    (output/"report.md").write_text('\n'.join(text)+'\n', encoding="utf-8")


def plot_run(output, trace, title="Random Attacks — production ERG + simulated bike", *, workout=None, ftp=305, score=None):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    from matplotlib.collections import PolyCollection
    from matplotlib.patches import FancyBboxPatch
    from matplotlib.lines import Line2D

    # Workout overview: segment silhouette behind independently scaled power
    # and cadence. Color is determined by FTP-relative intensity, including ramps.
    background, panel, text = "#101b26", "#192735", "#dbe8f0"
    fig = plt.figure(figsize=(16, 7.5), facecolor=background)
    fig.suptitle(title, x=.065, y=.965, ha="left", color=text, fontsize=17, fontweight="bold")
    duration = workout.duration if workout is not None else trace[-1, 0]+1
    summary = [("DURATION", f"{int(duration)//60}:{int(duration)%60:02d}"), ("FTP", f"{ftp:g} W"),
               ("AVG POWER", f"{np.mean(trace[:, 2]):.0f} W"), ("AVG CADENCE", f"{np.mean(trace[:, 3]):.0f} RPM"),
               ("95% ABS ERROR", f"{score['p95_absolute_error_w']:.0f} W" if score else "—"),
               ("MAX ABS ERROR", f"{score['max_absolute_error_w']:.0f} W" if score else "—")]
    for i, (label, value) in enumerate(summary):
        x = .065+i*.147
        fig.add_artist(FancyBboxPatch((x, .805), .136, .105, boxstyle="round,pad=.008,rounding_size=.009",
                                      transform=fig.transFigure, facecolor=panel, edgecolor="#304253", linewidth=.8))
        fig.text(x+.011, .88, label, color="#91a6b7", fontsize=8, weight="bold")
        fig.text(x+.011, .837, value, color="#78e3ed" if i == 2 else "#55e39b" if i == 3 else text, fontsize=20, weight="bold")
    ax = fig.add_axes((.065, .13, .88, .59), facecolor=panel)
    colors = ["#536c88", "#297eb8", "#299b72", "#beaa35", "#d88432", "#c24e51"]
    def zone(watts):
        return colors[int(np.searchsorted([.55, .75, .9, 1.05, 1.2], watts/ftp))]
    vertices, facecolors = [], []
    if workout is not None:
        start = 0.0
        for segment in workout.segments:
            count = max(1, int(segment.duration_s/3)) if segment.start_w != segment.end_w else 1
            edges = np.linspace(start, start+segment.duration_s, count+1)
            watts = np.linspace(segment.start_w, segment.end_w, count+1)
            for a, b, wa, wb in zip(edges, edges[1:], watts, watts[1:]):
                vertices.append([(a/60, 0), (a/60, wa), (b/60, wb), (b/60, 0)])
                facecolors.append(zone((wa+wb)/2))
            start += segment.duration_s
        duration = workout.duration
    else:
        duration = trace[-1, 0]+1
        for a, b in zip(trace, np.vstack((trace[1:], trace[-1:]+np.array([1]+[0]*9)))):
            vertices.append([(a[0]/60, 0), (a[0]/60, a[1]), (b[0]/60, a[1]), (b[0]/60, 0)])
            facecolors.append(zone(a[1]))
    ax.add_collection(PolyCollection(vertices, facecolors=facecolors, edgecolors="none", alpha=.58))
    t = trace[:, 0]/60
    ax.step(t, trace[:, 1], where="post", color="#c4d6e1", alpha=.65, linewidth=.7)
    ax.plot(t, trace[:, 2], color="#78e3ed", linewidth=1.0, label="Power · W")
    ax.set(xlim=(0, duration/60), ylim=(0, max(trace[:, 1].max(), trace[:, 2].max())*1.13), xlabel="Elapsed time (min)", ylabel="Power (W)")
    ax2 = ax.twinx()
    ax2.plot(t, trace[:, 3], color="#55e39b", linewidth=1.0, alpha=.95, label="Cadence · RPM")
    cadence_path = Path(output)/"cadence.csv"
    if cadence_path.exists():
        callouts = np.genfromtxt(cadence_path, delimiter=",", skip_header=1)
        if callouts.ndim == 2:
            ax2.plot(callouts[:, 0]/60, callouts[:, 2], color="#bdffd9", linestyle="--", linewidth=.8, alpha=.7)
    ax2.set(ylim=(0, max(160, trace[:, 3].max()*1.2)), ylabel="Cadence (RPM)")
    for axis in (ax, ax2):
        axis.tick_params(colors="#9eb4c5", labelsize=9)
        axis.xaxis.label.set_color(text); axis.yaxis.label.set_color(text)
        for spine in axis.spines.values(): spine.set_color("#344857")
    ax2.yaxis.label.set_color("#55e39b")
    ax.grid(axis="y", color="#70818e", alpha=.22, linewidth=.8)
    legend = ax.legend(handles=[Line2D([0], [0], color="#78e3ed", label="Power · W"),
                                Line2D([0], [0], color="#55e39b", label="Cadence · RPM"),
                                *([Line2D([0], [0], color="#bdffd9", linestyle="--", label="Cadence callout")] if cadence_path.exists() else []),
                                Line2D([0], [0], color="#c4d6e1", label="ERG segments · FTP zones")],
                       loc="lower center", bbox_to_anchor=(.5, 1.025), ncol=4, frameon=False, fontsize=10)
    for label in legend.get_texts(): label.set_color(text)
    rider_label = "fixed interval cadence" if cadence_path.exists() else "recorded or stochastic cadence"
    fig.text(.065, .038, f"Production SmartSpin2k ERG  /  fitted bike + {rider_label}  /  raw 1 Hz feedback", color="#8da5b7", fontsize=9)
    fig.savefig(Path(output)/"response.png", dpi=160, facecolor=background)
    fig.savefig(Path(output)/"response.svg", facecolor=background)
    plt.close(fig)

    # Separate detail view for debugging changes that are hard to see at scale.
    fig, axes = plt.subplots(3, 1, figsize=(15, 9), sharex=True, constrained_layout=True)
    t = trace[:, 0]/60
    axes[0].plot(t, trace[:, 1], color="#222222", label="Workout target", linewidth=1.5)
    axes[0].plot(t, trace[:, 2], color="#187cba", label="Simulated reported power", linewidth=.8)
    axes[0].set(ylabel="Power (W)", title=title); axes[0].legend()
    axes[1].plot(t, trace[:, 2]-trace[:, 1], color="#b64a31", linewidth=.7)
    for band in (20, -20): axes[1].axhline(band, color="gray", linestyle="--", linewidth=.6)
    axes[1].set(ylabel="Error (W)")
    axes[2].plot(t, trace[:, 3], color="#2b8756", linewidth=.8)
    axes[2].set(ylabel="Cadence (RPM)", xlabel="Simulation time (min)")
    for ax in axes: ax.grid(alpha=.2)
    fig.savefig(Path(output)/"diagnostics.png", dpi=140)
    plt.close(fig)
    if (Path(output)/"learned.ptab").exists():
        from .power_table import plot_table
        plot_table(output, trace)


def plot_reference(output, data, prediction, baseline, split):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(2, 1, figsize=(15, 7), constrained_layout=True)
    for ax in axes:
        ax.plot(data[:, 0]/60, data[:, 1], color="#222222", label="Device log", linewidth=.8)
        ax.plot(data[:, 0]/60, prediction, color="#187cba", label="Fitted delayed bike", linewidth=.8)
        ax.axvline(split/60, color="#b64a31", linestyle="--", label="Held-out portion starts")
        ax.set(ylabel="Power (W)", xlabel="Time (min)"); ax.grid(alpha=.2)
    axes[0].set(title="Recorded motor/cadence → predicted bike power; chronological holdout")
    axes[0].legend()
    axes[1].plot(data[:, 0]/60, baseline, color="#ac8537", linewidth=.6, alpha=.6, label="Independently fitted zero-lag baseline")
    axes[1].set(xlim=(10, 17), title="Motor response detail"); axes[1].legend()
    fig.savefig(Path(output)/"fit.png", dpi=140); plt.close(fig)
