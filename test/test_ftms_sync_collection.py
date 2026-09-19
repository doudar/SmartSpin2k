"""Check production watts collection while an FTMS position offset is pending."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestFtmsSyncCollection(unittest.TestCase):
    def test_pending_offset_discards_buffer_and_blocks_learning(self):
        source = (ROOT / "src/Power_Table.cpp").read_text(encoding="utf-8")
        production = "\n".join(function(source, signature) for signature in (
            "void PowerBuffer::set(", "void PowerBuffer::reset(",
            "int PowerBuffer::getReadings(", "void PowerTable::processPowerValue("))
        harness = r'''
#include <cassert>
#include <cstdint>
#define SS2K_LOG(...) ((void)0)
constexpr int POWER_SAMPLES = 3, TABLE_DIVISOR = 10;
constexpr int POWERTABLE_WATT_SIZE = 20, POWERTABLE_WATT_INCREMENT = 25;
unsigned long millis() { return 1; }
struct Measurement { int value; int getValue() const { return value; } };
struct Runtime { Measurement watts{150}, cad{60}; } runtime;
Runtime* rtConfig = &runtime;
struct Controller { int position = 9100; int getCurrentPosition() { return position; } } controller;
Controller* ss2k = &controller;
struct Config { int getShiftStep() { return 1210; } } config;
Config* userConfig = &config;
struct Entry { int readings = 0, watts = 0, cad = 0, targetPosition = 0; };
struct PowerBuffer {
  Entry powerEntry[POWER_SAMPLES];
  void set(int); void reset(); int getReadings();
};
struct Helpers { bool cadenceIsWithinTable(int cadence) { return cadence == 60; } };
struct PowerTable {
  uint32_t positionEpoch = 0;
  bool ftmsPositionUncertain = false;
  Helpers ptHelpers;
  int entries = 0, learnedPosition = 0;
  void processPowerValue(PowerBuffer&, int, Measurement);
  void newEntry(PowerBuffer& buffer) {
    ++entries;
    learnedPosition = buffer.powerEntry[0].targetPosition;
    for (auto& entry : buffer.powerEntry) assert(entry.targetPosition == learnedPosition);
  }
  void toLog() {}
  void _manageSaveState() {}
};
/* PRODUCTION */
int main() {
  PowerTable table;
  PowerBuffer buffer;
  table.processPowerValue(buffer, 60, runtime.watts);
  assert(buffer.getReadings() > 0 && table.entries == 0);
  table.ftmsPositionUncertain = true;
  for (int i = 0; i < 50; ++i) table.processPowerValue(buffer, 60, runtime.watts);
  assert(buffer.getReadings() == 0 && table.entries == 0);
  controller.position = 7440;
  ++table.positionEpoch;
  table.ftmsPositionUncertain = false;
  table.processPowerValue(buffer, 60, runtime.watts);
  table.processPowerValue(buffer, 60, runtime.watts);
  assert(table.entries == 1 && table.learnedPosition == 744);
}
'''.replace("/* PRODUCTION */", production)
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required for integration regressions")
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / "collection.cpp"
            exe = Path(tmp) / "collection.exe"
            cpp.write_text(harness, encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
