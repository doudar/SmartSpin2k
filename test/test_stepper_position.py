"""Exercise the production position reader with PCNT rollover between register reads."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
unsigned reads = 0, rolloverAfter = 0;
uint32_t count = 0, status = 0;
constexpr uint8_t counterId = 2;
constexpr uint32_t limitBit = 1u << counterId;
struct Register {
  bool isStatus;
  operator uint32_t() const {
    const uint32_t value = isStatus ? status : count;
    if (++reads == rolloverAfter) { count = 0; status |= limitBit; }
    return value;
  }
};
struct {
  struct { Register COUNT_VALUE{false}; } cnt_unit[4];
  struct { Register val{true}; } int_st;
} PCNT;
struct Pulse { int32_t start; uint16_t ticks; uint8_t steps; bool forward; };
struct PulseQueue {
  static constexpr unsigned capacity = 32;
  uint8_t read = 0, write = 1, counterId = ::counterId;
  int32_t tailPosition = 999;
  Pulse entries[capacity]{};
};
/* PRODUCTION */
int main() {
  PulseQueue q;
  for (bool forward : {false, true}) {
    for (uint8_t steps : {1, 28, 255}) {
      q.entries[0] = {-100, 400, steps, forward};
      auto expected = [&](uint32_t pulses) { return -100 + (forward ? int(pulses) : -int(pulses)); };
      rolloverAfter = 0; reads = 0; count = steps - 1; status = 0;
      assert(currentPosition(&q) == expected(steps - 1));
      // Inject the last rising edge after each read in the snapshot. If it
      // arrives after the last status sample, the pre-edge position is valid.
      for (unsigned boundary = 1; boundary <= 3; ++boundary) {
        rolloverAfter = boundary; reads = 0; count = steps - 1; status = 0;
        assert(currentPosition(&q) == expected(boundary == 3 ? steps - 1 : steps));
        assert(reads <= 6); // The latched flag cannot change again while mux is held.
      }
      // Pending ISR plus subsequent pulses, and an unrelated unit's interrupt.
      rolloverAfter = 0; reads = 0; count = 3; status = limitBit;
      assert(currentPosition(&q) == expected(steps + 3));
      reads = 0; count = 0; status = limitBit;
      assert(currentPosition(&q) == expected(steps));
      reads = 0; count = steps - 1; status = 1u << 1;
      assert(currentPosition(&q) == expected(steps - 1));
    }
  }
  reads = 0; q.entries[0].steps = 0;
  assert(currentPosition(&q) == -100 && reads == 0);
  q.read = q.write;
  assert(currentPosition(&q) == q.tailPosition && reads == 0);
  std::puts("PCNT coherent position snapshot tests passed.");
}
'''


class PositionTests(unittest.TestCase):
    def test_rollover_between_register_reads(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        source = (ROOT / "lib/SS2KStepper/PulseQueue.cpp").read_text(encoding="utf-8")
        production = function(source, "int32_t currentPosition(PulseQueue* q)")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            harness = path / "position.cpp"
            harness.write_text("#include <initializer_list>\n" + HARNESS.replace("/* PRODUCTION */", production), encoding="utf-8")
            exe = path / "position.exe"
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(harness), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
