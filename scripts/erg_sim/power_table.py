"""Export and plot the native controller's final table, including empty cells."""

import csv
import json
from pathlib import Path
import struct

import numpy as np


def save_table(output, table):
    output = Path(output)
    # ESP32 PTAB prefix: little-endian int32 version/quality, bool homed,
    # then row-major int16 stored position + int8 readings (no struct padding).
    # This simulation has no FTMS calibration trailer.
    blob = struct.pack("<ii?", table["version"], table["quality"], bool(table["homed"]))
    blob += b"".join(struct.pack("<hb", *cell) for cell in table["cells"])
    (output/"learned.ptab").write_bytes(blob)
    (output/"power_table.json").write_text(json.dumps(table, indent=2)+'\n', encoding="utf-8")
    with (output/"power_table.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["cadence_rpm", "watts", "position_steps", "stored_position", "readings"])
        for i, (position, readings) in enumerate(table["cells"]):
            writer.writerow([table["cadence_min"]+(i//table["columns"])*table["cadence_increment"],
                             (i % table["columns"])*table["watt_increment"],
                             position*table["position_divisor"] if position != -32768 else "", position, readings])


def read_ptab(path, rows, columns):
    """Decode the exact prefix used by firmware; dimensions live in settings."""
    blob = Path(path).read_bytes()
    expected = 9+3*rows*columns
    if len(blob) != expected:
        raise ValueError(f"Expected {expected} bytes for simulator PTAB without FTMS trailer")
    version, quality, homed = struct.unpack_from("<ii?", blob)
    cells = [list(struct.unpack_from("<hb", blob, 9+3*i)) for i in range(rows*columns)]
    return version, quality, homed, cells


def plot_table(output, trace=None):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.ticker import MaxNLocator

    output = Path(output)
    table = json.loads((output/"power_table.json").read_text(encoding="utf-8"))
    _, _, _, cells = read_ptab(output/"learned.ptab", table["rows"], table["columns"])
    data = np.array(cells).reshape(table["rows"], table["columns"], 2)
    position = data[:, :, 0]*table["position_divisor"]
    readings = data[:, :, 1]
    valid = (data[:, :, 0] != -32768) & (readings > 0)
    cadences = table["cadence_min"]+np.arange(table["rows"])*table["cadence_increment"]
    watts = np.arange(table["columns"])*table["watt_increment"]
    bg, panel, fg = "#101b26", "#192735", "#dbe8f0"
    with plt.rc_context({"text.color": fg, "axes.labelcolor": fg, "xtick.color": fg, "ytick.color": fg,
                         "axes.edgecolor": "#658095", "font.size": 10}):
        fig = plt.figure(figsize=(16, 8), facecolor=bg, layout="constrained")
        gs = fig.add_gridspec(2, 1, height_ratios=[3.8, 1])
        ax = fig.add_subplot(gs[0], facecolor=panel)
        colors = ["#bd9af7", "#a6a2fa", "#91bafa", "#85d3fa", "#7de1e4",
                  "#74e9cb", "#b0e68d", "#ecd479", "#efb37b", "#ed8da5"]
        for row, cadence in enumerate(cadences):
            populated = valid[row]
            if not populated.any():
                continue
            # Join the stored knots for readability, without extrapolating
            # beyond a row's observed power range or populating missing cells.
            ax.plot(watts[populated], position[row, populated], color=colors[row % len(colors)],
                    linewidth=1.8, marker="o", markersize=3.5, label=f"{cadence:g} RPM")
        if not valid.any():
            ax.text(.5, .5, "No learned entries yet", transform=ax.transAxes, ha="center", color=fg)
        ax.set(xlabel="Power (W)", ylabel="Motor position (steps)",
               title="POWER MAP · cadence curves", xlim=(0, table["columns"]*table["watt_increment"]))
        if valid.any():
            ax.set_ylim(min(0, float(position[valid].min())*1.1), max(1, float(position[valid].max())*1.18))
        else:
            ax.set_ylim(0, 1)
        ax.yaxis.set_major_locator(MaxNLocator(nbins=6))
        ax.grid(alpha=.18)
        if valid.any():
            ax.legend(loc="upper left", ncol=min(10, int(np.any(valid, axis=1).sum())), frameon=False,
                      labelcolor=fg, fontsize=9, title="CADENCE · RPM")
        ax.text(.99, .015, "Dots: stored entries · Lines: joins between entries · No extrapolation",
                transform=ax.transAxes, ha="right", va="bottom", color="#9eb4c5", fontsize=9)
        ax3 = fig.add_subplot(gs[1], facecolor=panel)
        if trace is not None:
            ax3.plot(trace[:, 0]/60, trace[:, 7], color="#78e3ed")
        ax3.set(xlabel="Ride time (min)", ylabel="Supported cells", title="Table learning during the ride")
        ax3.grid(alpha=.2)
        supported = int(((readings >= 2) & valid).sum())
        fig.suptitle(f"SmartSpin2k learned power table — {supported}/{valid.size} supported cells, {np.any(valid, axis=1).sum()} cadence rows",
                     fontsize=17, weight="bold")
        fig.savefig(output/"power_table.png", dpi=150, facecolor=bg)
        fig.savefig(output/"power_table.svg", facecolor=bg)
        plt.close(fig)
