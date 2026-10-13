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
from .sampling import SensorChannel, SensorSchedule


UNLIMITED_START_GEAR = 8


def run(workout, config, exe, output, *, seed=27, speed=0, tick_ms=20, sensitivity=5,
        initial_position=7433, rider_config=None, cadence_trace=None, initial_power=None,
        rider_mode="planned", progress=None, sleep=time.sleep, monotonic=time.monotonic,
        homed=True, firmware_position=None, min_watts=50, max_watts=1000, shift_step=1000,
        power_schedule=None, cadence_schedule=None, packet_ingress=False, connection_events=()):
    """initial_position is the bike's physical position (0 = minimum stop).

    Homed firmware shares that coordinate. Unhomed firmware boots at the
    Unlimited start-gear coordinate (8 x shiftStep) wherever the knob really
    is, with no travel limits until setStepperMinMax() learns them from
    min/max watts. Pulses past a physical stop are lost by the bike but still
    counted by the firmware, so its coordinate drifts like a skipping motor.
    """
    if firmware_position is None:
        firmware_position = initial_position if homed else UNLIMITED_START_GEAR*shift_step
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
    asynchronous = packet_ingress or power_schedule is not None or cadence_schedule is not None
    if packet_ingress:
        from scripts.bike_sim.protocol import CrankState, encode_csc, encode_power
        crank = CrankState()
    power_channel = SensorChannel(power_schedule or SensorSchedule(), seed+100)
    cadence_channel = SensorChannel(cadence_schedule or SensorSchedule(), seed+200)
    if asynchronous and min(power_channel.schedule.period_s, cadence_channel.schedule.period_s) < tick_ms/1000:
        raise ValueError("Sensor period must be at least one simulation tick")
    held_power, held_cadence = bike.power, bike.cadence
    latest_sampled_power = bike.power
    def acquire_power(at):
        nonlocal latest_sampled_power
        latest_sampled_power = bike.sample_power(at)
        return encode_power(latest_sampled_power) if packet_ingress else latest_sampled_power
    packet_rows = []
    events = dict(connection_events)
    if events and not packet_ingress:
        raise ValueError("Sensor sessions require the packet ingress path")
    start_offset = round(firmware_position)-bike.position
    started = monotonic()
    with Bridge(exe, output/"firmware.log", position=firmware_position, lo=config.min_position,
                hi=config.max_position, sensitivity=sensitivity, min_watts=min_watts, max_watts=max_watts,
                shift_step=shift_step, homed=homed, packet_ingress=packet_ingress) as bridge:
        for stamp in range(0, math.ceil(workout.duration*1000), tick_ms):
            now = stamp/1000
            watts, cue = workout.at(now)
            if now in events: bridge.connection(events[now])
            # Targets are app control input. Only cadence/power/load cross back
            # from the bike; no measured position, target watts, or table oracle.
            command = watts if watts != previous_target else -1
            if asynchronous:
                if packet_ingress:
                    crank.advance(now, cadence)
                power_samples = power_channel.update(now, acquire_power)
                cadence_samples = cadence_channel.update(now, encode_csc(crank) if packet_ingress else cadence)
                fresh, cadence_fresh = bool(power_samples), bool(cadence_samples)
                if packet_ingress:
                    for characteristic, samples in ((0x2a63, power_samples), (0x2a5b, cadence_samples)):
                        for acquired, payload in samples:
                            bridge.notify(characteristic, payload)
                            packet_rows.append((now, acquired, f"{characteristic:04x}", payload.hex()))
                            if characteristic == 0x2a63:
                                import struct
                                held_power = struct.unpack_from("<h", payload, 2)[0] # report-only telemetry, never plant input
                else:
                    if fresh: held_power = power_samples[-1][1]
                    if cadence_fresh: held_cadence = cadence_samples[-1][1]
            else:
                held_power, held_cadence, cadence_fresh = bike.power, bike.cadence, fresh
            result = bridge.tick(1000+stamp, held_power, held_cadence, fresh, command, bike.load,
                                 cadence_fresh=cadence_fresh)
            pulses, counter, target, cells, seeking, effective_target, min_step, max_step = result
            if fresh or asynchronous:
                # bike.position has not yet received this tick's pulses; align the counter with it.
                lost = (counter-pulses-bike.position)-start_offset
                rows.append((now, watts, bike.filtered if asynchronous else bike.power, cadence if asynchronous else bike.cadence, bike.position, counter, target, cells, seeking, effective_target,
                             min_step, max_step, lost, latest_sampled_power if asynchronous else bike.power, held_power))
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
        ingress = bridge.ingress_stats() if packet_ingress else None
        if ingress is not None and (ingress["processed"] == 0 or ingress["rejected"] or ingress["queue_dropped"]):
            raise RuntimeError(f"Incomplete firmware packet ingestion: {ingress}")
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
    if packet_ingress:
        with (output/"sensor_packets.csv").open("w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f); writer.writerow(["delivered_s", "acquired_s", "characteristic", "payload_hex"])
            writer.writerows(packet_rows)
    fields = ["time_s", "target_w", "power_w", "cadence_rpm", "physical_steps", "motor_steps", "command_steps", "learned_cells", "table_seeking", "effective_target_w",
              "min_step", "max_step", "lost_pulses", "latest_acquired_sensor_power_w", "latest_delivered_sensor_power_w"]
    with (output/"trace.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f); writer.writerow(fields); writer.writerows(rows)
    metadata = {"coverage_level": ("BLE-packet firmware ingestion + FTMS control + virtual motor; no radio or ESP32 scheduler"
                                   if packet_ingress else "native-controller-component; no BLE/DirCon transport"),
                "packet_ingress_evidence": ingress,
                "power_evaluation_signal": "continuous effective plant output before sensor delay/noise" if asynchronous else "legacy fixed-rate reported sensor power",
                "sensor_timing": "independent acquisition, quantized forward to integration tick; immutable sample then delivery delay",
                "connection_events": list(connection_events),
                "sensor_schedules": {"power": power_channel.schedule.to_dict(), "cadence": cadence_channel.schedule.to_dict()} if asynchronous else None,
                "sensor_counts": {"power": {k: getattr(power_channel, k) for k in ("acquired", "delivered", "dropped")},
                                  "cadence": {k: getattr(cadence_channel, k) for k in ("acquired", "delivered", "dropped")}} if asynchronous else None,
                "seed": seed, "wall_seconds": monotonic()-started, "simulation_seconds": workout.duration,
                "requested_speed": speed, "tick_ms": tick_ms, "sensitivity": sensitivity,
                "initial_position_steps": initial_position, "initial_table": "empty; production online learning enabled",
                "homing": "homed" if homed else "unhomed", "firmware_start_steps": firmware_position,
                "physical_travel_steps": [config.min_position, config.max_position],
                "min_watts": min_watts, "max_watts": max_watts, "shift_step": shift_step,
                "rider": rider.plan.data if planned else asdict(rider_config),
                "cadence_source": "recorded trace" if cadence_trace is not None else "fixed interval plan" if planned else "seeded stochastic rider",
                "cadence_plan_sha256": rider.plan.sha256 if planned else None,
                "power_table": {"supported_cells": sum(p != -32768 and n >= 2 for p, n in table["cells"]),
                                "total_cells": table["rows"]*table["columns"], "quality": table["quality"]}}
    return np.array(rows), metadata
