"""Build production C++ with fake peripherals and a tiny synchronous pipe."""

from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    # Same narrow extraction used by the repository's motor integration tests.
    start = source.index(signature)
    end = source.index("{", start)+1
    depth = 1
    while depth:
        depth += (source[end] == "{")-(source[end] == "}")
        end += 1
    return source[start:end]


def build(folder, *, source_root=None, packet_ingress=False):
    """Compile one firmware revision using this same test adapter.

    This is a controller/component experiment, not BLE end-to-end coverage.
    """
    root = Path(source_root).resolve() if source_root is not None else ROOT
    compiler = shutil.which("g++")
    if not compiler:
        raise RuntimeError("g++ is required to run the production ERG controller")
    folder = Path(folder).resolve()
    folder.mkdir(parents=True, exist_ok=True)
    (folder/"Arduino.h").write_text('''#pragma once
#include <cstdint>
#include <string>
using String = std::string;
extern uint32_t clockMs;
inline unsigned long millis() { return clockMs; }
''', encoding="utf-8")
    source = (root/"src/ERG_Mode.cpp").read_text(encoding="utf-8")
    for header in ['"SS2KLog.h"', '"Main.h"', '"Power_Table.h"', '<LittleFS.h>']:
        source = source.replace('#include '+header, '')
    table = (root/"src/Power_Table.cpp").read_text(encoding="utf-8")
    table = '\n'.join(function(table, signature) for signature in (
        "void PowerBuffer::set(", "void PowerBuffer::clearSamples(", "void PowerBuffer::reset(", "int PowerBuffer::getReadings(",
        "void PowerTable::processPowerValue(", "void PowerTable::newEntry(", "void PowerTable::setStepperMinMax(",
        "void PowerTable::clearRuntime(", "bool PowerTable::reset("))
    stepper = (root/"src/Stepper.cpp").read_text(encoding="utf-8")
    homing = "static int lastHomingSgThreshold=0;\n" + '\n'.join(function(stepper, signature) for signature in (
        "static int getScaledHomingSensitivity(", "static HomingSgBaseline getHomingSgBaseline(", "bool SS2K::_findEndStop("))
    template = Path(__file__).with_name("native.cpp").read_text(encoding="utf-8")
    sources, flags = [], []
    if packet_ingress:
        from . import virtual_ble, virtual_ftms
        # The adapter modules inventory the exact production functions compiled.
        ble = virtual_ble.prepare(folder, root)
        ftms_declarations, ftms_implementation = virtual_ftms.prepare(folder, root)
        template = (template.replace("/* VIRTUAL_BLE_DECLARATIONS */", ble["declarations"])
                    .replace("/* VIRTUAL_BLE_IMPLEMENTATION */", ble["implementation"])
                    .replace("/* VIRTUAL_FTMS_DECLARATIONS */", ftms_declarations)
                    .replace("/* VIRTUAL_FTMS_IMPLEMENTATION */", ftms_implementation))
        sources = list(map(str, ble["sources"]))
        flags = ["-DSS2K_PACKET_INGRESS", *ble.get("flags", [])]
    cpp = folder/"erg_bridge.cpp"
    cpp.write_text(template.replace("/* PRODUCTION_ERG */", source).replace("/* PRODUCTION_TABLE */", table)
                   .replace("/* PRODUCTION_STEPPER */", function(stepper, "void SS2K::moveStepper("))
                   .replace("/* PRODUCTION_HOMING */", homing), encoding="utf-8")
    exe = folder/"erg_bridge.exe"
    subprocess.run([compiler, "-O2", "-std=c++17", "-DPLATFORMIO_ENV_NATIVE", "-I"+str(folder),
                    "-I"+str(root/"include"), "-I"+str(root/"lib/SS2K/include"), "-I"+str(root/"lib/ArduinoCompat/include"),
                    str(cpp), str(root/"src/PowerTable_Helpers.cpp"), *sources, *flags, "-o", str(exe)], check=True)
    return exe


class Bridge:
    def __init__(self, exe, log, position=7433, lo=0, hi=24482, speed=3500, sensitivity=5, min_watts=50,
                 max_watts=2000, shift_step=1000, homed=True, packet_ingress=False):
        self.packet_ingress = packet_ingress
        self.log = Path(log).open("w", encoding="utf-8")
        self.process = subprocess.Popen([str(exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=self.log, text=True, bufsize=1)
        self.now_ms = 1000
        try:
            # lo/hi are saved homing limits; unhomed sessions ignore them.
            if self.exchange(f"I {round(position)} {round(lo)} {round(hi)} {speed} {sensitivity} {min_watts} "
                             f"{max_watts} {shift_step} {int(homed)}") != ["I", "1"]:
                raise RuntimeError("Bridge protocol version mismatch")
            if packet_ingress:
                self.control(bytes([0]))
                self.control(bytes([7]))
        except BaseException:
            self.close()
            raise

    def exchange(self, text):
        self.process.stdin.write(text+'\n')
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError("Native ERG bridge stopped; inspect its firmware log")
        return line.split()

    def connection(self, connected):
        command = "C" if connected else "X"
        if self.exchange(command) != [command, "1"]:
            raise RuntimeError("Virtual sensor session transition failed")

    def notify(self, characteristic, payload):
        if self.exchange(f"B {characteristic:04x} {bytes(payload).hex()}") != ["B", "1"]:
            raise RuntimeError("Sensor packet rejected")

    def control(self, payload):
        row = self.exchange("F "+bytes(payload).hex())
        if row != ["F", "128", str(payload[0]), "1"]:
            raise RuntimeError(f"FTMS procedure failed: {row}")

    def tick(self, now_ms, watts, cadence, fresh, target=-1, load=0, *, cadence_fresh=None):
        # Separate component-level samples. This pipe is NOT a BLE transport.
        mask = (1 if fresh else 0) | (2 if (fresh if cadence_fresh is None else cadence_fresh) else 0)
        if self.packet_ingress:
            if target >= 0:
                import struct
                self.control(struct.pack("<Bh", 5, round(target)))
            watts = cadence = mask = 0
            target = -1
        row = self.exchange(f"T {now_ms} {round(watts)} {round(cadence)} {mask} {target} {load}")
        if row[0] != "T" or len(row) != 9:
            raise RuntimeError(f"Malformed bridge reply: {row}")
        self.now_ms = now_ms
        return tuple(map(int, row[1:]))

    def home(self, bike, forward=False, cadence=0):
        now = 0
        row = self.exchange(f"H {int(forward)}")
        started_ms = previous_ms = self.now_ms
        while row[0] == "D":
            stamp, pulses = map(int, row[1:])
            now = (stamp-started_ms)/1000
            bike.advance((stamp-1000)/1000, (stamp-previous_ms)/1000, pulses, cadence)
            previous_ms = stamp
            row = self.exchange(f"L {bike.load}")
        if row[0] != "H":
            raise RuntimeError(f"Malformed homing reply: {row}")
        self.now_ms = int(row[2])
        return {"found": bool(int(row[1])), "duration_s": now, "command_position": int(row[3]), "physical_position": bike.position}

    def ingress_stats(self):
        row = self.exchange("S")
        if row[0] != "S" or len(row) != 9:
            raise RuntimeError("Malformed firmware ingress statistics")
        return dict(zip(("attempted", "accepted", "rejected", "queue_dropped", "processed", "bytes", "final_power_w", "final_cadence_rpm"), map(float, row[1:])))

    def power_table(self):
        row = self.exchange("P")
        if row[0] != "P" or len(row) < 10:
            raise RuntimeError("Malformed power-table snapshot")
        keys = ("version", "quality", "homed", "rows", "columns", "cadence_min", "cadence_increment", "watt_increment", "position_divisor")
        table = dict(zip(keys, map(int, row[1:10])))
        values = list(map(int, row[10:]))
        if len(values) != 2*table["rows"]*table["columns"]:
            raise RuntimeError("Incomplete power-table snapshot")
        table["cells"] = [values[i:i+2] for i in range(0, len(values), 2)]
        return table

    def load_power_table(self, snapshot):
        """Load one initial saved-table analog into RAM before ride activity.

        Use a separately recorded production-learned snapshot for ride tests.
        This setup seam never reads the plant, cannot update a running ride,
        and does not exercise flash persistence or coordinate normalization.
        """
        keys = ("version", "quality", "homed", "rows", "columns", "cadence_min", "cadence_increment", "watt_increment", "position_divisor")
        if not isinstance(snapshot, dict) or any(key not in snapshot for key in (*keys, "cells")):
            raise ValueError("Incomplete initial power-table snapshot")
        if any(type(snapshot[key]) is not int for key in keys):
            raise ValueError("Power-table metadata must be integers")
        current = self.power_table()
        if any(snapshot[key] != current[key] for key in keys if key != "quality"):
            raise ValueError("Power-table version, grid or homing state does not match firmware")
        cells = snapshot["cells"]
        if not isinstance(cells, (list, tuple)) or len(cells) != current["rows"] * current["columns"]:
            raise ValueError("Power-table cell count does not match grid")
        values = []
        for cell in cells:
            if not isinstance(cell, (list, tuple)) or len(cell) != 2 or any(type(x) is not int for x in cell):
                raise ValueError("Each power-table cell requires integer position/readings")
            position, readings = cell
            if not (-32768 <= position <= 32767 and 0 <= readings <= 127) or ((position == -32768) != (readings == 0)):
                raise ValueError("Invalid power-table position, readings or empty-cell sentinel")
            values.extend(cell)
        if snapshot["quality"] != sum(cells[i][1] for i in range(len(cells))):
            raise ValueError("Power-table quality must equal the recorded reading count")
        payload = [snapshot[key] for key in keys] + values
        if self.exchange("U " + " ".join(map(str, payload))) != ["U", "1"]:
            raise RuntimeError("Initial power-table import rejected: already loaded or ride activity has started")

    def close(self):
        if self.process.poll() is None:
            try:
                self.process.stdin.write("Q\n"); self.process.stdin.flush()
                self.process.wait(timeout=5)
            except (OSError, subprocess.TimeoutExpired):
                self.process.kill(); self.process.wait()
        self.process.stdin.close(); self.process.stdout.close(); self.log.close()

    def __enter__(self): return self
    def __exit__(self, *_): self.close()
