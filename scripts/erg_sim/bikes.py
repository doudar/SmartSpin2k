"""Bike plants derived from learned power-table CSV exports.

A learned table maps (cadence, watts) to a stored position divided by
TABLE_DIVISOR. Inverting each row gives the bike's watts(position, cadence)
surface. Cells are a mix of measured and firmware-extrapolated values (the CSV
export has no reading weights), so the surface is a plausible plant shape, not
a measured identification of every operating point.
"""

import hashlib
from pathlib import Path
import re

TABLE_DIVISOR = 10


def load_ptab(path):
    lines = [line.strip() for line in Path(path).read_text(encoding="utf-8").splitlines() if line.strip()]
    metadata = {}
    for line in lines:
        if line.startswith("#"):
            metadata.update(re.findall(r"(\w+)=(-?\d+)", line))
    header = next(i for i, line in enumerate(lines) if line.startswith("Cadence/Power"))
    watts = [int(cell.rstrip("Ww")) for cell in lines[header].split(",")[1:]]
    cadences, positions = [], []
    for line in lines[header+1:]:
        cells = line.split(",")
        cadences.append(int(re.sub(r"\D", "", cells[0])))
        positions.append([int(cell)*TABLE_DIVISOR for cell in cells[1:]])
    if any(len(row) != len(watts) or any(not cell for cell in row) for row in positions):
        raise ValueError(f"{path}: every row needs one position per watt column")
    return {"metadata": {k: int(v) for k, v in metadata.items()}, "watts": watts, "cadences": cadences, "positions": positions}


def travel_max(table, hmax=None):
    """Physical maximum in full-scale steps.

    Some exports record HMax in stored (divided) units; recognize that when the
    table's clamp plateau equals HMax x TABLE_DIVISOR.
    """
    table_max = max(max(row) for row in table["positions"])
    if hmax is not None:
        return hmax, "explicit"
    saved = table["metadata"].get("HMax")
    if saved is None:
        return table_max, "table maximum (no HMax metadata)"
    if saved*TABLE_DIVISOR == table_max and saved < table_max/2:
        return table_max, f"HMax={saved} interpreted as stored units (x{TABLE_DIVISOR}); it equals the table clamp"
    return saved, "HMax metadata"


def profile_from_ptab(path, base_profile, hmax=None, name=None):
    from .model import BikeConfig, equilibrium
    table = load_ptab(path)
    maximum, travel_source = travel_max(table, hmax)
    bike = dict(base_profile["bike"])
    bike.update(min_position=0, max_position=maximum, backlash_steps=0,
                surface={"cadences": table["cadences"], "watts": table["watts"], "positions": table["positions"]})
    cfg = BikeConfig(**bike)
    # Start where a typical rider would sit before ERG begins: ~100 W at 80 RPM.
    start = next((p for p in range(0, int(maximum)+1, 50) if equilibrium(cfg, p, 80) >= 100), int(maximum//2))
    floor = {rpm: round(equilibrium(cfg, 0, rpm)) for rpm in (60, 80, 100)}
    ceiling = {rpm: round(equilibrium(cfg, maximum, rpm)) for rpm in (60, 80, 100)}
    return {
        "name": name or Path(path).stem,
        "bike": bike,
        "rider": base_profile.get("rider", {}),
        "start_position": start,
        "source": {"table": Path(path).name, "sha256": hashlib.sha256(Path(path).read_bytes()).hexdigest(),
                   "travel_max_source": travel_source,
                   "dynamics": "lag, delay, noise and sample period copied from the fitted ride profile"},
        "physical_watts_at_travel_limits": {"min_position": floor, "max_position": ceiling},
        "notes": ["Surface comes from a learned table that mixes measured and extrapolated cells.",
                  "Homed coordinate 0 is the physical minimum stop; max_position is the physical maximum stop."],
    }
