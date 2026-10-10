# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only

"""Exercise production scan delivery with a congested peer and minimum ATT MTU."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestBleScanDelivery(unittest.TestCase):
    def test_fragment_retry_peer_fairness_end_and_capacity(self):
        source = (ROOT / "src/BLE_Custom_Characteristic.cpp").read_text()
        declarations = source[source.index("struct ScanDelivery {"):source.index("void resetSettingsSnapshot()")]
        pump = function(source, "void pumpScanResults(")
        harness = r'''
#include <atomic>
#include <deque>
#include <cassert>
#include "CustomCharacteristicEvents.h"
#include "ScanResultProtocol.h"
constexpr size_t CONFIG_BT_NIMBLE_MAX_CONNECTIONS = 2;
constexpr int SMARTSPIN2K_SERVICE_UUID = 1, SMARTSPIN2K_CHARACTERISTIC_UUID = 2;
uint32_t clockNow = 1;
uint32_t millis() { return clockNow; }
CustomCharacteristicEvents<2> bleEvents;
struct NimBLECharacteristic {
  std::vector<std::vector<uint8_t>> delivered;
  unsigned failures = 0;
  bool notify(const uint8_t* data, size_t size, uint16_t peer) {
    assert(size <= 20);
    if (peer == 1) { ++failures; return false; }
    delivered.emplace_back(data, data + size);
    return true;
  }
} characteristic;
struct DirConManager {
  static void notifyCharacteristic(int, int, const uint8_t*, size_t) {}
};
DECLARATIONS
PUMP
int main() {
  bleEvents.connect(1);
  bleEvents.connect(2);
  bleEvents.subscribe(1, 23, 1);
  bleEvents.subscribe(2, 23, 2); // Indication-only peers retain legacy scan notification delivery.
  queueScanResult(ScanResultProtocol::Event::Begin, 0);
  std::vector<uint8_t> body(25);
  for (unsigned i = 0; i < body.size(); ++i) body[i] = i;
  queueScanResult(ScanResultProtocol::Event::Device, 0, body);
  queueScanResult(ScanResultProtocol::Event::End, 1);
  for (clockNow = 20; clockNow < 3000; clockNow += 20) pumpScanResults(&characteristic);
  assert(!scanDeliveryActive && scanDeliveries.empty());
  assert(characteristic.failures > 3); // Retry buffer pressure instead of abandoning immediately.
  assert(characteristic.delivered.size() == 5);
  assert(characteristic.delivered.front()[3] == 0);
  assert(characteristic.delivered.back()[3] == 2);
  std::vector<uint8_t> rebuilt;
  for (unsigned i = 1; i <= 3; ++i) {
    const auto& packet = characteristic.delivered[i];
    assert(packet[8] == i - 1 && packet[9] == 3);
    rebuilt.insert(rebuilt.end(), packet.begin() + 10, packet.end());
  }
  assert(rebuilt == body);
  // A reused connection handle never receives records captured for the old peer.
  queueScanResult(ScanResultProtocol::Event::Device, 2, body);
  bleEvents.disconnect(2);
  bleEvents.connect(2);
  for (; clockNow < 4000; clockNow += 20) pumpScanResults(&characteristic);
  assert(characteristic.delivered.size() == 5);
  for (unsigned i = 0; i < SCAN_QUEUE_CAPACITY + 10; ++i) queueScanResult(ScanResultProtocol::Event::Device, i, body);
  assert(scanDeliveries.size() == SCAN_QUEUE_CAPACITY - 1);
  queueScanResult(ScanResultProtocol::Event::End, 100);
  assert(scanDeliveries.size() == SCAN_QUEUE_CAPACITY && scanDeliveries.back().event == ScanResultProtocol::Event::End);
  // Stale devices drain, and the end marker is still attempted.
  bleEvents.subscribe(2, 23, 2); // Indication-only peers retain legacy scan notification delivery.
  for (auto& item : scanDeliveries) item.peers = bleEvents.recipients();
  for (clockNow = 20000; clockNow < 22000; clockNow += 20) pumpScanResults(&characteristic);
  assert(!scanDeliveryActive && scanDeliveries.empty());
  assert(characteristic.delivered.back()[3] == 2 && characteristic.delivered.back()[6] == 100);
}
'''.replace("DECLARATIONS", declarations).replace("PUMP", pump)
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "scan.cpp"
            exe = Path(directory) / "scan"
            cpp.write_text(harness)
            subprocess.run([compiler, "-std=c++17", "-pthread", "-I", str(ROOT / "lib/SS2K/include"), str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
