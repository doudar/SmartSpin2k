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


def build(folder):
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
    source = (ROOT/"src/ERG_Mode.cpp").read_text(encoding="utf-8")
    for header in ['"SS2KLog.h"', '"Main.h"', '"Power_Table.h"', '<LittleFS.h>']:
        source = source.replace('#include '+header, '')
    table = (ROOT/"src/Power_Table.cpp").read_text(encoding="utf-8")
    table = '\n'.join(function(table, signature) for signature in (
        "void PowerBuffer::set(", "void PowerBuffer::clearSamples(", "void PowerBuffer::reset(", "int PowerBuffer::getReadings(",
        "void PowerTable::processPowerValue(", "void PowerTable::newEntry(", "void PowerTable::setStepperMinMax(",
        "void PowerTable::clearRuntime(", "bool PowerTable::reset("))
    stepper = (ROOT/"src/Stepper.cpp").read_text(encoding="utf-8")
    homing = "static int lastHomingSgThreshold=0;\n" + '\n'.join(function(stepper, signature) for signature in (
        "static int getScaledHomingSensitivity(", "static HomingSgBaseline getHomingSgBaseline(", "bool SS2K::_findEndStop("))
    template = Path(__file__).with_name("native.cpp").read_text(encoding="utf-8")
    cpp = folder/"erg_bridge.cpp"
    cpp.write_text(template.replace("/* PRODUCTION_ERG */", source).replace("/* PRODUCTION_TABLE */", table)
                   .replace("/* PRODUCTION_STEPPER */", function(stepper, "void SS2K::moveStepper("))
                   .replace("/* PRODUCTION_HOMING */", homing), encoding="utf-8")
    exe = folder/"erg_bridge.exe"
    subprocess.run([compiler, "-O2", "-std=c++17", "-DPLATFORMIO_ENV_NATIVE", "-I"+str(folder),
                    "-I"+str(ROOT/"include"), "-I"+str(ROOT/"lib/SS2K/include"), "-I"+str(ROOT/"lib/ArduinoCompat/include"),
                    str(cpp), str(ROOT/"src/PowerTable_Helpers.cpp"), "-o", str(exe)], check=True)
    return exe


class Bridge:
    def __init__(self, exe, log, position=7433, lo=0, hi=24482, speed=3500, sensitivity=5, min_watts=50):
        self.log = Path(log).open("w", encoding="utf-8")
        self.process = subprocess.Popen([str(exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=self.log, text=True, bufsize=1)
        self.now_ms = 1000
        try:
            if self.exchange(f"I {round(position)} {round(lo)} {round(hi)} {speed} {sensitivity} {min_watts}") != ["I", "1"]:
                raise RuntimeError("Bridge protocol version mismatch")
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

    def tick(self, now_ms, watts, cadence, fresh, target=-1, load=0):
        row = self.exchange(f"T {now_ms} {watts} {cadence} {int(fresh)} {target} {load}")
        if row[0] != "T" or len(row) != 7:
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
