"""Behavioral packet tests for the production FTMS queue processor, without RF."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts.erg_sim.virtual_ftms import prepare


class TestVirtualFtms(unittest.TestCase):
    def test_production_control_packets_and_responses(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required for production packet tests")
        declarations, implementation = prepare(None, ROOT)
        prefix = r'''
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
  std::vector<uint8_t> forwarded;
  void FTMSControlPointWrite(uint8_t* data, int length) { forwarded.assign(data, data + length); }
} spinBLEClient;
struct {
  int spinDownFlag = 0;
  std::queue<std::string> writeCache;
} spinBLEServer;
'''
        scenarios = r'''
int main() {
  config.setPowerCorrectionFactor(1.0f);
  auto expect = [](std::vector<uint8_t> request, uint8_t result) {
    const auto before = fitnessMachineService.controlPoint.notifications.size();
    const auto response = virtualFtmsWrite(request);
    assert((response == std::vector<uint8_t>{0x80, request[0], result}));
    assert(spinBLEServer.writeCache.empty());
    assert(fitnessMachineService.controlPoint.notifications.size() == before + 1);
  };
  runtime.setSimTargetWatts(true);
  expect({0x00}, 0x01);
  assert(runtime.watts.getTarget() == 0 && !runtime.getSimTargetWatts());
  expect({0x07}, 0x01);
  assert((fitnessMachineService.status.value == std::vector<uint8_t>{0x04}));
  // Real little-endian wire bytes must set the target through production code.
  expect({0x05, 0x2c, 0x01}, 0x01);
  assert(runtime.watts.getTarget() == 300);
  assert(runtime.getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetPower);
  assert((fitnessMachineService.status.value == std::vector<uint8_t>{0x08, 0x2c, 0x01}));
  assert((spinBLEClient.forwarded == std::vector<uint8_t>{0x05, 0x2c, 0x01}));
  auto training = fitnessMachineService.trainingStatus.value;
  expect({0x08, 0x02}, 0x01);
  assert(fitnessMachineService.trainingStatus.value == training);
  assert((fitnessMachineService.status.value == std::vector<uint8_t>{0x02, 0x02}));
  expect({0x08, 0x01}, 0x01);
  assert(fitnessMachineService.trainingStatus.value.at(1) == FitnessMachineTrainingStatus::Idle);
  // Stop is a training-status change in current firmware, not a zero-power command.
  assert(runtime.watts.getTarget() == 300);
  spinBLEClient.connectedPM = false;
  expect({0x05, 0xc8, 0x00}, 0x02);
  assert(runtime.watts.getTarget() == 300);
  spinBLEClient.connectedCD = true;
  expect({0x05, 0xc8, 0x00}, 0x01);
  assert(runtime.watts.getTarget() == 200);
  // Drain real queued packets in arrival order, including embedded zero bytes.
  const auto queuedBefore = fitnessMachineService.controlPoint.notifications.size();
  spinBLEServer.writeCache.push(std::string("\x05\x00\x01", 3));
  spinBLEServer.writeCache.push(std::string("\x05\x96\x00", 3));
  fitnessMachineService.processFTMSWrite();
  assert(spinBLEServer.writeCache.empty());
  assert(runtime.watts.getTarget() == 150);
  assert(fitnessMachineService.controlPoint.notifications.size() == queuedBefore + 2);
  expect({0x00}, 0x01);
  assert(runtime.watts.getTarget() == 0);
  // Malformed target packets are rejected by the test adapter, not presented as
  // safe handling by firmware (which lacks a target-power length check).
  bool rejected = false;
  try { virtualFtmsWrite({0x05}); } catch (const std::invalid_argument&) { rejected = true; }
  assert(rejected);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "ftms.cpp"
            exe = Path(directory) / "ftms.exe"
            cpp.write_text(prefix + declarations + implementation + scenarios, encoding="utf-8")
            endian = Path(directory) / "endian.o"
            subprocess.run([compiler, "-x", "c", "-I" + str(ROOT / "lib/ArduinoCompat/include"),
                            "-c", str(ROOT / "lib/ArduinoCompat/src/os/endian.c"), "-o", str(endian)], check=True)
            subprocess.run([compiler, "-std=c++17", "-DPLATFORMIO_ENV_NATIVE",
                            "-I" + str(ROOT / "include"), "-I" + str(ROOT / "lib/SS2K/include"),
                            "-I" + str(ROOT / "lib/ArduinoCompat/include"), str(cpp),
                            str(ROOT / "lib/ArduinoCompat/src/NimBLEUUID.cpp"),
                            str(endian), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
