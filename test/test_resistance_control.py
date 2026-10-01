"""Regression tests for resistance holding and adaptive overshoot damping."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TestResistanceControl(unittest.TestCase):
    def test_adaptive_damping(self):
        source = r'''
#include <cassert>
#include "ResistanceControl.h"
int main() {
  using ResistanceControl::Controller;
  for (float sensitivity : {0.0f, 3.0f, 10.0f}) {
    Controller hold;
    for (uint32_t t = 0; t < 5000; t += 10)
      assert(hold.update(9100, 50, 50, t, t, 1200, sensitivity) == 9100);
  }
  Controller control;
  auto command = [&](int r, uint32_t t) { return control.update(9100, r, 50, t, t, 1200, 3); };
  assert(command(40, 0) > 9100);
  assert(command(54, 1000) < 9100); // First genuine overshoot, still return toward target.
  assert(control.damping() == 0.5f);
  assert(command(46, 2000) > 9100);
  assert(control.damping() == 1.0f);
  assert(command(49, 3000) == 9100); // Brake an approach before its next overshoot.
  for (uint32_t t = 3010; t < 3500; t += 10) {
    assert(control.update(9100, 49, 50, 3000, t, 1200, 3) == 9100);
    assert(control.damping() == 1.0f); // Re-reading a sample is not another overshoot.
  }
  assert(command(49, 4000) > 9100); // No permanent stall when resistance stops changing.
  assert(command(50, 5000) == 9100);
  for (uint32_t t = 6000; t < 30000; t += 1000) command(t % 2000 ? 46 : 54, t);
  assert(control.damping() == 2.5f); // Bounded even with repeated crossings.
  control.update(9100, 50, 60, 30000, 30000, 1200, 3);
  assert(control.damping() == 0); // A new target starts with normal responsiveness.

  Controller noise;
  for (uint32_t t = 0; t < 30000; t += 1000) {
    noise.update(9100, 48 + (t / 1000) % 5, 50, t, t, 1200, 3);
    assert(noise.damping() == 0); // Quantization/noise around the target is not training.
  }
  Controller reverse;
  reverse.update(1000, 60, 50, 0, 0, 1200, 3);
  reverse.update(1000, 46, 50, 1000, 1000, 1200, 3);
  reverse.update(1000, 54, 50, 2000, 2000, 1200, 3);
  assert(reverse.update(1000, 51, 50, 3000, 3000, 1200, 3) == 1000);
  // A held derivative expires, and millis rollover is handled by unsigned ages.
  assert(reverse.update(1000, 51, 50, 3000, 5000, 1200, 3) < 1000);
  Controller wrapped;
  wrapped.update(1000, 40, 50, UINT32_MAX - 500, UINT32_MAX - 500, 1200, 3);
  wrapped.update(1000, 54, 50, 499, 499, 1200, 3);
  assert(wrapped.damping() == 0.5f);
  assert(ResistanceControl::target(INT32_MAX - 1, 50, 1200, 3) == INT32_MAX);
  assert(ResistanceControl::target(INT32_MIN + 1, -50, 1200, 3) == INT32_MIN);
  // Homing uses the same controller with measured travel, bounded per move.
  Controller calibrated;
  assert(calibrated.update(20000, 80, 50, 0, 0, 6000, 3, 250) == 14000);
}
'''
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "resistance.cpp"
            exe = Path(directory) / "resistance.exe"
            cpp.write_text(source, encoding="utf-8")
            subprocess.run([compiler, "-std=c++11", "-I" + str(ROOT / "include"), str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
