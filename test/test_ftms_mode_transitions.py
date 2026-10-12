"""Production FTMS mode changes at the packet boundary, without RF or motor motion.

These deliberately bypass the ERG-only virtualFtmsWrite convenience guard and
queue well-formed wire packets directly. They do not establish ATT ownership,
disconnect behavior, malformed-packet safety, or physical trainer response.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts.erg_sim.virtual_ftms import prepare


PREFIX = r'''
#include <cassert>
#include <cstdio>
#include <queue>
#include <string>
#include <vector>
#include "SmartSpin_parameters.h"
#include "BLE_Definitions.h"
#include "Constants.h"
#define SS2K_LOG(...) ((void)0)
RuntimeParameters runtime;
RuntimeParameters* rtConfig = &runtime;
userParameters config;
userParameters* userConfig = &config;
struct {
  bool connectedPM = true, connectedCD = false;
  std::vector<std::vector<uint8_t>> forwarded;
  void FTMSControlPointWrite(uint8_t* data, int length) {
    forwarded.emplace_back(data, data + length);
  }
} spinBLEClient;
struct {
  int spinDownFlag = 0;
  std::queue<std::string> writeCache;
} spinBLEServer;
'''

SCENARIOS = r'''
void enqueue(std::vector<uint8_t> packet) {
  spinBLEServer.writeCache.push(std::string(packet.begin(), packet.end()));
}
void expect(std::vector<uint8_t> packet, uint8_t result = 1) {
  const auto before = fitnessMachineService.controlPoint.notifications.size();
  enqueue(packet);
  fitnessMachineService.processFTMSWrite();
  assert(spinBLEServer.writeCache.empty());
  assert(fitnessMachineService.controlPoint.notifications.size() == before + 1);
  assert((fitnessMachineService.controlPoint.value == std::vector<uint8_t>{0x80, packet[0], result}));
}
void mode(uint8_t opcode, uint8_t training) {
  assert(runtime.getFTMSMode() == opcode);
  assert((fitnessMachineService.trainingStatus.value == std::vector<uint8_t>{0, training}));
}
int main() {
  config.setPowerCorrectionFactor(1.25f);
  runtime.setMinResistance(10);
  runtime.setMaxResistance(80);
  expect({0x00});
  expect({0x07});
  // Repeated full mode cycles must not retain a previous mode or apply power
  // correction to the user's displayed target. Only the remote target changes.
  for (int cycle = 0; cycle < 3; ++cycle) {
    expect({0x05, 0x2c, 0x01}); // 300 W local, 240 W forwarded.
    mode(0x05, FitnessMachineTrainingStatus::WattControl);
    assert(runtime.watts.getTarget() == 300);
    assert((spinBLEClient.forwarded.back() == std::vector<uint8_t>{0x05, 0xf0, 0x00}));

    const std::vector<uint8_t> downhill{0x11, 0x9c, 0xff, 0x0c, 0xfe, 0x28, 0x33};
    expect(downhill); // -5% grade in signed 0.01% units.
    mode(0x11, FitnessMachineTrainingStatus::ManualMode);
    assert(runtime.getTargetIncline() == -500);
    assert(spinBLEClient.forwarded.back() == downhill);
    assert((fitnessMachineService.status.value == std::vector<uint8_t>{0x12, 0x9c, 0xff, 0x0c, 0xfe, 0x28, 0x33}));
    const auto statuses = fitnessMachineService.status.notifications.size();
    const auto forwarded = spinBLEClient.forwarded.size();
    expect(downhill); // Repeat requests still acknowledge and forward.
    assert(fitnessMachineService.status.notifications.size() == statuses);
    assert(spinBLEClient.forwarded.size() == forwarded + 1);
    assert(runtime.getTargetIncline() == -500);

    // Pause/resume changes training state, without undoing the active grade.
    expect({0x08, 0x02});
    mode(0x11, FitnessMachineTrainingStatus::ManualMode);
    expect({0x07});
    mode(0x11, FitnessMachineTrainingStatus::WarmingUp);
    assert(runtime.getTargetIncline() == -500);
    expect({0x03, 0xe7, 0xff}); // -2.5% in signed 0.1% units.
    mode(0x03, FitnessMachineTrainingStatus::ManualMode);
    assert(runtime.getTargetIncline() == -250);
    assert((fitnessMachineService.status.value == std::vector<uint8_t>{0x06, 0xe7, 0xff}));

    expect({0x04, 0x32, 0x00});
    mode(0x04, FitnessMachineTrainingStatus::ManualMode);
    assert(runtime.resistance.getTarget() == 50);
    // Current firmware clamps out-of-range requests, reports the applied value,
    // and responds InvalidParameter rather than acknowledging the raw request.
    expect({0x04, 0xff, 0xff}, 3);
    assert(runtime.resistance.getTarget() == 10);
    assert((fitnessMachineService.status.value == std::vector<uint8_t>{0x07, 10, 0}));
    expect({0x04, 0x64, 0x00}, 3);
    assert(runtime.resistance.getTarget() == 80);
    assert((fitnessMachineService.status.value == std::vector<uint8_t>{0x07, 80, 0}));

    expect({0x08, 0x01});
    mode(0x04, FitnessMachineTrainingStatus::Idle);
    expect({0x07});
    expect({0x05, 0xfa, 0x00});
    mode(0x05, FitnessMachineTrainingStatus::WattControl);
    assert(runtime.watts.getTarget() == 250);
    assert((spinBLEClient.forwarded.back() == std::vector<uint8_t>{0x05, 200, 0}));
  }
  // Several app writes before one maintenance pass must execute in order.
  const auto replies = fitnessMachineService.controlPoint.notifications.size();
  const auto forwards = spinBLEClient.forwarded.size();
  enqueue({0x11, 0, 0, 0xc8, 0, 0x28, 0x33});
  enqueue({0x04, 60, 0});
  enqueue({0x05, 0x90, 0x01});
  fitnessMachineService.processFTMSWrite();
  assert(spinBLEServer.writeCache.empty());
  mode(0x05, FitnessMachineTrainingStatus::WattControl);
  assert(runtime.getTargetIncline() == 200);
  assert(runtime.resistance.getTarget() == 60);
  assert(runtime.watts.getTarget() == 400);
  const auto& notifications = fitnessMachineService.controlPoint.notifications;
  assert(notifications.size() == replies + 3);
  assert((notifications[replies] == std::vector<uint8_t>{0x80, 0x11, 1}));
  assert((notifications[replies + 1] == std::vector<uint8_t>{0x80, 0x04, 1}));
  assert((notifications[replies + 2] == std::vector<uint8_t>{0x80, 0x05, 1}));
  assert(spinBLEClient.forwarded.size() == forwards + 2);
  assert((spinBLEClient.forwarded.back() == std::vector<uint8_t>{0x05, 0x40, 0x01}));
}
'''


class TestFtmsModeTransitions(unittest.TestCase):
    def run_harness(self, implementation, scenarios=SCENARIOS):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required for production FTMS tests")
        declarations, _ = prepare(None, ROOT)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "ftms_modes.cpp"
            exe = Path(directory) / "ftms_modes.exe"
            endian = Path(directory) / "endian.o"
            cpp.write_text(PREFIX + declarations + implementation + scenarios, encoding="utf-8")
            subprocess.run([compiler, "-x", "c", "-I" + str(ROOT / "lib/ArduinoCompat/include"),
                            "-c", str(ROOT / "lib/ArduinoCompat/src/os/endian.c"), "-o", str(endian)], check=True)
            subprocess.run([compiler, "-std=c++17", "-DPLATFORMIO_ENV_NATIVE",
                            "-I" + str(ROOT / "include"), "-I" + str(ROOT / "lib/SS2K/include"),
                            "-I" + str(ROOT / "lib/ArduinoCompat/include"), str(cpp),
                            str(ROOT / "lib/ArduinoCompat/src/NimBLEUUID.cpp"), str(endian),
                            "-o", str(exe)], check=True)
            return subprocess.run([str(exe)], capture_output=True, text=True, cwd=directory)

    def test_repeated_interrupted_and_queued_mode_changes(self):
        _, implementation = prepare(None, ROOT)
        result = self.run_harness(implementation)
        self.assertEqual(result.returncode, 0, result.stderr)

    def setUp(self):
        if self._testMethodName != "test_rejected_erg_request_preserves_active_simulation_mode":
            return
        # Compile and verify the specific failure in setUp, outside the expected
        # failure wrapper, so build errors or unrelated assertions remain errors.
        # Known production defect: SetTargetPower in processFTMSWrite assigns
        # FTMSMode before checking whether a usable power/cadence source exists.
        # Keep the desired behavior executable until that ordering is fixed.
        _, implementation = prepare(None, ROOT)
        scenario = SCENARIOS[:SCENARIOS.index("int main()")] + r'''
int main() {
  config.setPowerCorrectionFactor(1.0f);
  expect({0x05, 0x2c, 0x01}); // Leave an earlier 300 W ERG target behind.
  expect({0x11, 0, 0, 0x0c, 0xfe, 0x28, 0x33});
  spinBLEClient.connectedPM = false;
  spinBLEClient.connectedCD = false;
  runtime.watts.setSimulate(false);
  const auto forwards = spinBLEClient.forwarded.size();
  expect({0x05, 0xc8, 0x00}, 2); // Firmware rejects unsupported ERG.
  assert(runtime.watts.getTarget() == 300);
  assert(spinBLEClient.forwarded.size() == forwards);
  fprintf(stderr, "Rejected ERG: active mode=%u, expected SIM=17, held target=%d W\n",
          unsigned(runtime.getFTMSMode()), runtime.watts.getTarget());
  assert(runtime.getFTMSMode() == FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters);
}
'''
        self.rejected_result = self.run_harness(implementation, scenario)
        if self.rejected_result.returncode != 0:
            self.assertIn("Rejected ERG: active mode=5, expected SIM=17, held target=300 W", self.rejected_result.stderr)
            self.assertIn("runtime.getFTMSMode() == FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters",
                          self.rejected_result.stderr)
            self.assertIn("assertion", self.rejected_result.stderr.lower())

    @unittest.expectedFailure
    def test_rejected_erg_request_preserves_active_simulation_mode(self):
        self.assertEqual(self.rejected_result.returncode, 0, self.rejected_result.stderr)

    def test_detects_lost_mode_and_unsigned_downhill_mutations(self):
        _, implementation = prepare(None, ROOT)
        mutations = {
            "lost mode change": ("rtConfig->setFTMSMode((uint8_t)rxValue[0]);", "/* lost mode change */"),
            "unsigned downhill": ("port = get_le16s(&pData[3]);", "port = get_le16(&pData[3]);"),
        }
        for name, (original, replacement) in mutations.items():
            with self.subTest(mutation=name):
                self.assertIn(original, implementation)
                result = self.run_harness(implementation.replace(original, replacement))
                self.assertNotEqual(result.returncode, 0, "Behavioral checks missed " + name)
                self.assertIn("assertion", result.stderr.lower())
