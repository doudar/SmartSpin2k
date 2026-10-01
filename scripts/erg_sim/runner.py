"""One deterministic clock for firmware, rider, motor pulses, and sensor lag."""

import csv
from dataclasses import asdict
import math
from pathlib import Path
import time

import numpy as np

from .bridge import Bridge
from .model import Bike, Rider, RiderConfig
from .cadence import PlannedRider
from .power_table import save_table


def run(workout, config, exe, output, *, seed=27, speed=0, tick_ms=20, sensitivity=5,
        initial_position=7433, rider_config=None, cadence_trace=None, initial_power=None,
        rider_mode="planned", progress=None, sleep=time.sleep, monotonic=time.monotonic):
    if speed < 0 or not math.isfinite(speed):
        raise ValueError("Speed must be zero (unlimited) or a finite positive multiplier")
    if tick_ms < 1 or tick_ms > 100 or 1000 % tick_ms:
        raise ValueError("Tick must divide 1000 and be between 1 and 100 ms")
    if not math.isfinite(sensitivity) or sensitivity <= 0:
        raise ValueError("ERG sensitivity must be finite and positive")
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    rider_config = rider_config or RiderConfig()
    rider_config.__post_init__()
    if rider_mode not in ("planned", "stochastic"): raise ValueError("Unknown rider mode")
    planned = cadence_trace is None and rider_mode == "planned"
    rider = PlannedRider(workout) if planned else Rider(rider_config, seed)
    cadence = float(cadence_trace[0, 1]) if cadence_trace is not None else rider.cadence
    bike = Bike(config, initial_position, cadence, seed+1, initial_power)
    rows, cadence_rows, fresh, previous_target = [], [], True, None
    started = monotonic()
    with Bridge(exe, output/"firmware.log", position=initial_position, lo=config.min_position,
                hi=config.max_position, sensitivity=sensitivity) as bridge:
        for stamp in range(0, math.ceil(workout.duration*1000), tick_ms):
            now = stamp/1000
            watts, cue = workout.at(now)
            # Targets are app control input. Only cadence/power/load cross back
            # from the bike; no measured position, target watts, or table oracle.
            command = watts if watts != previous_target else -1
            result = bridge.tick(1000+stamp, bike.power, bike.cadence, fresh, command, bike.load)
            pulses, counter, target, cells, seeking, effective_target = result
            if fresh:
                rows.append((now, watts, bike.power, bike.cadence, bike.position, counter, target, cells, seeking, effective_target))
                if planned:
                    callout, desired, index = rider.plan.at(now)
                    cadence_rows.append((now, index, callout, desired, bike.cadence))
            previous_target = watts
            dt = tick_ms/1000
            if cadence_trace is not None:
                cadence = float(np.interp(now+dt, cadence_trace[:, 0], cadence_trace[:, 1]))
            elif planned:
                cadence = rider.advance(now+dt, dt)
            else:
                cadence = rider.advance(now+dt, dt, bike.filtered, cue)
            fresh = bike.advance(now+dt, dt, pulses, cadence)
            if speed:
                remaining = started+(now+dt)/speed-monotonic()
                if remaining > 0: sleep(remaining)
            if progress and stamp % 60000 == 0: progress(now, workout.duration)
        table = bridge.power_table()
        save_table(output, table)
    if planned:
        import json
        (output/"cadence_plan.json").write_text(json.dumps(rider.plan.data, indent=2)+'\n', encoding="utf-8")
        with (output/"cadence.csv").open("w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f); writer.writerow(["time_s", "segment", "callout_rpm", "desired_rpm", "reported_rpm"])
            writer.writerows(cadence_rows)
    else:
        for name in ("cadence.csv", "cadence_plan.json"):
            (output/name).unlink(missing_ok=True)
    fields = ["time_s", "target_w", "power_w", "cadence_rpm", "physical_steps", "motor_steps", "command_steps", "learned_cells", "table_seeking", "effective_target_w"]
    with (output/"trace.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f); writer.writerow(fields); writer.writerows(rows)
    metadata = {"seed": seed, "wall_seconds": monotonic()-started, "simulation_seconds": workout.duration,
                "requested_speed": speed, "tick_ms": tick_ms, "sensitivity": sensitivity,
                "initial_position_steps": initial_position, "initial_table": "empty; production online learning enabled",
                "rider": rider.plan.data if planned else asdict(rider_config),
                "cadence_source": "recorded trace" if cadence_trace is not None else "fixed interval plan" if planned else "seeded stochastic rider",
                "cadence_plan_sha256": rider.plan.sha256 if planned else None,
                "power_table": {"supported_cells": sum(p != -32768 and n >= 2 for p, n in table["cells"]),
                                "total_cells": table["rows"]*table["columns"], "quality": table["quality"]}}
    return np.array(rows), metadata
