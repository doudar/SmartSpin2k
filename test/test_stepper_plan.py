"""Compile and exercise the production native C++ single-axis planner."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StepperPlannerTests(unittest.TestCase):
    def test_positions_retargets_braking_limits_and_timing(self):
        harness = r'''
#include "StepperPlan.h"
#include <cassert>
#include <random>
#include <cstdio>

double finish(StepperPlan& p, int32_t expected, bool speedLimit = true) {
  StepperPlan::Batch b;
  double seconds = 0;
  unsigned commands = 0;
  while (p.next(b)) {
    assert(b.steps > 0 && b.ticks > 0 && b.steps * uint64_t(b.ticks) >= 3200);
    if (speedLimit) assert(double(StepperPlan::clockHz) / b.ticks <= p.speed * 1.000001);
    seconds += b.steps * double(b.ticks) / StepperPlan::clockHz;
    assert(++commands < 200000);
  }
  assert(!p.active() && p.position == expected);
  return seconds;
}
int main() {
  StepperPlan p;
  for (int32_t target : {1, -1, 0, 4, -4, 400, -400, 14000, -14000}) {
    p.reset(0); p.moveTo(target); finish(p, target);
  }
  // At 14,000 pulses/s and 12,000 pulses/s², a 28,000 pulse move has
  // 1.167 s ramps and 0.833 s cruise: about 3.167 seconds in total.
  p.reset(0); p.moveTo(28000);
  double duration = finish(p, 28000);
  std::printf("28,000-pulse duration: %.6f seconds\n", duration); std::fflush(stdout);
  assert(duration > 3.1 && duration < 3.3);
  // Repeated maintenance targets must not reset the acceleration state.
  p.reset(0); p.moveTo(14000);
  StepperPlan::Batch b; unsigned commands = 0;
  while (p.next(b)) { p.moveTo(14000); assert(++commands < 20000); }
  assert(p.position == 14000 && !p.active());
  // Reversal from cruise, retarget behind the stopping distance, soft stop,
  // emergency stop/restart, and continuous homing in either direction.
  for (bool forward : {false, true}) {
    p.reset(0); p.run(forward);
    for (int i = 0; i < 1000; ++i) assert(p.next(b));
    int32_t old = p.position;
    p.moveTo(forward ? -400 : 400);
    finish(p, forward ? -400 : 400);
    p.run(forward);
    for (int i = 0; i < 1000; ++i) assert(p.next(b));
    old = p.position;
    p.stop();
    while (p.next(b)) assert(b.forward == forward);
    assert(!p.active() && (forward ? p.position > old : p.position < old));
    p.reset(123); p.moveTo(-321); finish(p, -321);
  }
  // Very low speed, one-step rest-to-rest moves, and signed coordinate edges.
  for (uint32_t speed : {4u, 40u, 1400u, 14000u, 50000u, 200000u}) {
    p.speed = speed;
    p.reset(0); p.moveTo(1); finish(p, 1);
    p.moveTo(-5); finish(p, -5);
  }
  p.speed = 14000;
  p.reset(INT32_MAX - 2); p.run(true); finish(p, INT32_MAX);
  p.reset(INT32_MIN + 2); p.run(false); finish(p, INT32_MIN);
  // Many finite moves with mid-ramp target and speed changes must terminate
  // exactly, including targets closer than the current stopping distance.
  std::mt19937 random(7);
  for (unsigned i = 0; i < 500; ++i) {
    p.reset(0); p.speed = 14000;
    p.moveTo(int32_t(random() % 20001) - 10000);
    for (unsigned n = random() % 300; n && p.next(b); --n) {}
    int32_t target = int32_t(random() % 20001) - 10000;
    p.speed = 400 + random() % 13601;
    p.moveTo(target); finish(p, target, false);
  }
}
'''
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "planner.cpp"
            exe = Path(directory) / "planner.exe"
            source.write_text(harness, encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "lib/SS2KStepper"), str(source), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
