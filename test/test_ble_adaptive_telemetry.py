# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only

"""Native timing and production orchestration regressions for adaptive BLE telemetry."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestAdaptiveTelemetry(unittest.TestCase):
    def compile_and_run(self, source, cases=((),)):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required")
        with tempfile.TemporaryDirectory(prefix="ss2k-telemetry-") as directory:
            cpp = Path(directory) / "telemetry.cpp"
            exe = Path(directory) / "telemetry"
            cpp.write_text(source)
            subprocess.run([compiler, "-std=c++17", "-pthread", "-Wall", "-Wextra", "-I", str(ROOT / "lib/SS2K/include"), str(cpp), "-o", str(exe)], check=True)
            for args in cases:
                with self.subTest(args=args):
                    subprocess.run([str(exe), *map(str, args)], check=True)

    def test_nimble_subscriptions_only_back_off_on_real_send_failures(self):
        libraries = sorted((ROOT / ".pio/libdeps").glob("*/esp-nimble-cpp/src/NimBLECharacteristic.cpp"))
        if not libraries:
            self.skipTest("Install firmware dependencies to exercise NimBLE notification semantics")
        # Exercise the installed library, not a fake that assumes what notify()
        # returns for an empty subscription list. Recheck each distinct version.
        sources = {path.read_text() for path in libraries}
        server = function((ROOT / "src/BLE_Server.cpp").read_text(), "void SpinBLEServer::updatePowerTelemetry()")
        for source in sources:
            methods = "\n".join(function(source, signature) for signature in (
                "bool NimBLECharacteristic::notify(uint16_t", "bool NimBLECharacteristic::sendValue("))
            self.compile_and_run(r'''
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <vector>
#include "AdaptiveTelemetry.h"
constexpr uint16_t BLE_HS_CONN_HANDLE_NONE = 65535;
constexpr int BLE_HS_ENOENT = 1, BLE_HS_ENOMEM = 2;
constexpr int BLE_GATT_CHR_F_READ_ENC = 1, BLE_GATT_CHR_F_READ_AUTHEN = 2, BLE_GATT_CHR_F_READ_AUTHOR = 4;
constexpr uint32_t BLE_POWER_NOTIFY_MIN_INTERVAL = 200, BLE_POWER_NOTIFY_MAX_INTERVAL = 1100;
#define NIMBLE_LOGW(...) ((void)0)
#define NIMBLE_LOGE(...) ((void)0)
void ble_npl_hw_enter_critical() {}
void ble_npl_hw_exit_critical(int) {}
void ble_npl_time_delay(int) {}
int ble_npl_time_ms_to_ticks32(int ms) { return ms; }
struct os_mbuf {} buffer;
bool allocationFails = false;
os_mbuf* ble_hs_mbuf_from_flat(const uint8_t*, size_t) { return allocationFails ? nullptr : &buffer; }
uint32_t now = 0;
uint32_t millis() { return now; }
std::array<unsigned, 2> notifications{};
std::array<int, 2> sendErrors{};
int ble_gatts_notify_custom(uint16_t, uint16_t handle, os_mbuf*) { ++notifications[handle]; return sendErrors[handle]; }
int ble_gatts_indicate_custom(uint16_t, uint16_t, os_mbuf*) { assert(false); return 0; }
struct Peer {
  uint16_t handle = BLE_HS_CONN_HANDLE_NONE;
  uint16_t getConnHandle() const { return handle; }
  bool isSecured() const { return true; }
};
struct NimBLECharacteristic {
  std::array<Peer, 2> subscribers{};
  std::vector<uint8_t> m_value{0};
  uint16_t m_properties = 0, m_handle = 0;
  auto getSubscribers() const { return subscribers; }
  bool notify(uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE) const;
  bool sendValue(const uint8_t*, size_t, bool, uint16_t) const;
};
METHODS
struct Controller { bool isUpdating = false; } controller;
auto* ss2k = &controller;
struct Client { std::atomic<bool> connectionSetupActive{false}; } spinBLEClient;
struct Scanner { bool isScanning() { return false; } } scanner;
struct NimBLEDevice { static Scanner* getScan() { return &scanner; } };
struct Service {
  NimBLECharacteristic characteristic;
  unsigned updates = 0;
  bool update() { ++updates; return characteristic.notify(); }
} cyclingPowerService, fitnessMachineService;
struct SpinBLEServer {
  std::atomic<uint32_t> powerSampleSequence{0};
  void updatePowerTelemetry();
  void updateWheelAndCrankRev() {}
} spinBLEServer;
SERVER
int main(int argc, char** argv) {
  assert(argc == 3);
  const unsigned subscriptions = std::atoi(argv[1]);
  fitnessMachineService.characteristic.m_handle = 1;
  if (subscriptions & 1) cyclingPowerService.characteristic.subscribers[0].handle = 7;
  if (subscriptions & 2) fitnessMachineService.characteristic.subscribers[0].handle = 7;
  for (now = 0; now <= 2000; now += 10) {
    ++spinBLEServer.powerSampleSequence;
    spinBLEServer.updatePowerTelemetry();
  }
  // CPS-only, FTMS-only, both and no BLE subscribers all retain 5 Hz updates.
  // Both publishers must still run for readable values and DirCon delivery.
  assert(cyclingPowerService.updates == 10 && fitnessMachineService.updates == 10);
  assert(notifications[0] == ((subscriptions & 1) ? 10u : 0u));
  assert(notifications[1] == ((subscriptions & 2) ? 10u : 0u));

  // Test failure of every subscribed service independently in separate runs.
  const unsigned failedService = std::atoi(argv[2]);
  if (subscriptions) sendErrors[failedService] = BLE_HS_ENOMEM;
  now = 2200;
  ++spinBLEServer.powerSampleSequence;
  spinBLEServer.updatePowerTelemetry();
  assert(cyclingPowerService.updates == 11 && fitnessMachineService.updates == 11);
  now = 2400;
  ++spinBLEServer.powerSampleSequence;
  spinBLEServer.updatePowerTelemetry();
  assert(cyclingPowerService.updates == (subscriptions ? 11u : 12u));
  now = 2600;
  spinBLEServer.updatePowerTelemetry();
  assert(cyclingPowerService.updates == 12 && fitnessMachineService.updates == 12);
  // Drop all subscriptions while allocation is failing: no send is attempted,
  // and three successful empty broadcasts recover the existing backoff.
  cyclingPowerService.characteristic.subscribers = {};
  fitnessMachineService.characteristic.subscribers = {};
  allocationFails = true;
  for (now = 3700; now <= 5900; now += 1100) spinBLEServer.updatePowerTelemetry();
  const auto before = cyclingPowerService.updates;
  now = 6300;
  ++spinBLEServer.powerSampleSequence;
  spinBLEServer.updatePowerTelemetry();
  assert(cyclingPowerService.updates == before + 1);
}
'''.replace("METHODS", methods).replace("SERVER", server), cases=((0, 0), (1, 0), (2, 1), (3, 0), (3, 1)))

    def test_heartbeat_cap_backpressure_and_revolution_timing(self):
        self.compile_and_run(r'''
#include <cassert>
#include <cmath>
#include "AdaptiveTelemetry.h"
#include "CyclingRevolutions.h"
int main() {
  AdaptiveTelemetry quiet(1100, 200);
  assert(!quiet.due(0, 0, false));
  assert(!quiet.due(1099, 0, false));
  assert(quiet.due(1100, 0, false));
  quiet.attempted(1100, 0, true);
  assert(!quiet.due(1299, 1, false));
  assert(quiet.due(1300, 1, false));
  quiet.attempted(1300, 1, false);
  assert(!quiet.due(1699, 2, false));
  assert(quiet.due(1700, 2, false)); // Failure backs off to 400 ms.
  quiet.attempted(1700, 2, false);
  assert(!quiet.due(2499, 3, false));
  assert(quiet.due(2500, 3, false)); // Then 800 ms.
  quiet.attempted(2500, 3, false);
  assert(!quiet.due(3599, 4, false));
  assert(quiet.due(3600, 4, false)); // Capped at the 1100 ms heartbeat.
  for (uint32_t t : {3600u, 4700u, 5800u}) quiet.attempted(t, 4, true);
  assert(!quiet.due(6349, 5, false));
  assert(quiet.due(6350, 5, false)); // Three successes recover to 550 ms.
  quiet.attempted(6350, 5, true);
  assert(!quiet.due(7449, 6, true)); // Scan/setup constrains fresh output.
  assert(quiet.due(7450, 6, true));
  AdaptiveTelemetry wrap(1100, 200);
  const uint32_t start = UINT32_MAX - 100;
  assert(!wrap.due(start, 0, false));
  assert(wrap.due(start + 200, 1, false));
  wrap.attempted(start + 200, 1, true);
  assert(!wrap.due(start + 399, 2, false));
  assert(wrap.due(start + 400, 2, false));

  // Notification frequency must not affect accumulated distance/cadence.
  CyclingRevolutions fast, slow;
  fast.update(0, 90); slow.update(0, 90);
  for (uint32_t t = 10; t <= 10000; t += 10) fast.update(t, 90);
  for (uint32_t t = 500; t <= 10000; t += 500) slow.update(t, 90);
  assert(fast.count() == 15 && slow.count() == 15);
  assert(fast.eventTime(1024) == slow.eventTime(1024));
  assert(fast.eventTime(2048) == 20480);
  fast.update(10000, 0);
  auto last = fast.eventTime(1024);
  fast.update(15000, 0);
  assert(fast.count() == 15 && fast.eventTime(1024) == last);
  fast.update(15000, 60);
  fast.update(15999, 60);
  assert(fast.count() == 15);
  fast.update(16000, 60);
  assert(fast.count() == 16 && fast.eventTime(1024) == 16384);
  CyclingRevolutions changing;
  changing.update(0, 60);
  changing.update(500, 120); // Half a turn at the previous rate.
  changing.update(750, 120);
  assert(changing.count() == 1 && changing.eventTime(1024) == 768);
  CyclingRevolutions rollover;
  rollover.update(UINT32_MAX - 500, 60);
  rollover.update(499, 60);
  assert(rollover.count() == 1);
  const uint16_t before = rollover.eventTime(1024);
  rollover.update(1499, 60);
  assert(rollover.count() == 2 && uint16_t(rollover.eventTime(1024) - before) == 1024);
}
''')

    def test_production_server_keeps_baseline_services_separate(self):
        source = (ROOT / "src/BLE_Server.cpp").read_text()
        methods = "\n".join(function(source, signature) for signature in (
            "void SpinBLEServer::update()", "void SpinBLEServer::updatePowerTelemetry()", "void SpinBLEServer::updateWheelAndCrankRev()"))
        self.compile_and_run(r'''
#include <cassert>
#include <atomic>
#include <vector>
#include "AdaptiveTelemetry.h"
#include "CyclingRevolutions.h"
constexpr uint32_t BLE_NOTIFY_DELAY = 503, BLE_POWER_NOTIFY_MIN_INTERVAL = 200, BLE_POWER_NOTIFY_MAX_INTERVAL = 1100;
uint32_t now = 0;
uint32_t millis() { return now; }
struct Controller { bool isUpdating = false; } controller;
auto* ss2k = &controller;
struct Measurement { int getValue() { return 60; } };
unsigned speedCalculations = 0;
struct Runtime { Measurement cad; double getSimulatedSpeed() { return 0; } } runtime;
auto* rtConfig = &runtime;
struct Client {
  std::atomic<bool> connectionSetupActive{false};
  uint32_t cscCumulativeWheelRev = 0;
  uint16_t cscLastWheelEvtTime = 0, cpsLastWheelEvtTime = 0, cscCumulativeCrankRev = 0, cscLastCrankEvtTime = 0;
} spinBLEClient;
struct Scanner { bool active = false; bool isScanning() { return active; } } scanner;
struct NimBLEDevice { static Scanner* getScan() { return &scanner; } };
struct Service {
  std::vector<uint32_t> sent;
  bool success = true;
  unsigned controls = 0;
  bool update() { sent.push_back(now); return success; }
  void processFTMSWrite() { ++controls; }
} heartService, cyclingSpeedCadenceService, cyclingPowerService, fitnessMachineService, ss2kCustomCharacteristic;
struct SpinBLEServer {
  std::atomic<uint32_t> powerSampleSequence{0};
  void update(); void updatePowerTelemetry(); void updateWheelAndCrankRev();
  double calculateSpeed() { ++speedCalculations; return 30; }
} spinBLEServer;
METHODS
int main() {
  uint32_t lastBaseline = 0;
  for (now = 0; now <= 5000; now += 10) {
    // Equal-watt packets are still distinct samples; all are coalesced to <=5 Hz.
    if (now % 50 == 0) ++spinBLEServer.powerSampleSequence;
    if (now - lastBaseline >= BLE_NOTIFY_DELAY) { spinBLEServer.update(); lastBaseline = now; }
    spinBLEServer.updatePowerTelemetry();
  }
  assert(cyclingPowerService.sent.size() == 25);
  assert(fitnessMachineService.sent == cyclingPowerService.sent);
  assert(heartService.sent.size() == 9 && cyclingSpeedCadenceService.sent.size() == 9);
  assert(fitnessMachineService.controls == 9 && ss2kCustomCharacteristic.sent.size() == 9);
  assert(speedCalculations == 34); // Only 25 power outputs plus 9 CSC outputs, not 501 loop passes.
  assert(spinBLEClient.cscCumulativeCrankRev == 4);
  assert(spinBLEClient.cscLastCrankEvtTime == 4300);
  auto assertSpacing = [] {
    for (size_t i = 1; i < cyclingPowerService.sent.size(); ++i)
      assert(cyclingPowerService.sent[i] - cyclingPowerService.sent[i - 1] >= 200);
  };
  // A long pause sends once, never a catch-up burst.
  now = 10000;
  spinBLEServer.updatePowerTelemetry();
  spinBLEServer.updatePowerTelemetry();
  assert(cyclingPowerService.sent.size() == 26);
  now = 11100;
  spinBLEServer.updatePowerTelemetry();
  assert(cyclingPowerService.sent.size() == 27); // Baseline heartbeat with no new power.
  scanner.active = true;
  ++spinBLEServer.powerSampleSequence;
  now = 11300;
  spinBLEServer.updatePowerTelemetry();
  assert(cyclingPowerService.sent.size() == 27);
  now = 12200;
  spinBLEServer.updatePowerTelemetry();
  assert(cyclingPowerService.sent.size() == 28);
  scanner.active = false;
  spinBLEClient.connectionSetupActive = true;
  ++spinBLEServer.powerSampleSequence;
  now = 12400;
  spinBLEServer.updatePowerTelemetry();
  assert(cyclingPowerService.sent.size() == 28);
  spinBLEClient.connectionSetupActive = false;
  ss2k->isUpdating = true;
  now = 14000;
  spinBLEServer.updatePowerTelemetry();
  assert(cyclingPowerService.sent.size() == 28); // OTA remains quiet.
  ss2k->isUpdating = false;
  cyclingPowerService.success = false;
  spinBLEServer.updatePowerTelemetry();
  assert(fitnessMachineService.sent.size() == 29); // CPS failure does not skip FTMS.
  ++spinBLEServer.powerSampleSequence;
  now = 14200;
  spinBLEServer.updatePowerTelemetry();
  assert(cyclingPowerService.sent.size() == 29);
  now = 14400;
  spinBLEServer.updatePowerTelemetry();
  assert(cyclingPowerService.sent.size() == 30);
  assertSpacing();
}
'''.replace("METHODS", methods))

    def test_sensor_ingress_budget_and_accepted_sample_sequences(self):
        common = (ROOT / "src/BLE_Common.cpp").read_text()
        collector = (ROOT / "src/SensorCollector.cpp").read_text()
        methods = function(collector, "void collectAndSet(") + "\n" + function(common, "void BLEProcessSensorData()")
        self.compile_and_run(r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <deque>
#include <memory>
#include <string>
#include <vector>
using NimBLEUUID = int;
constexpr int NUM_BLE_DEVICES = 3, BLE_SENSOR_PACKETS_PER_PASS = 4, BLE_HS_CONN_HANDLE_NONE = 65535;
constexpr int PELOTON_DATA_UUID = 7, HID_SERVICE_UUID = 9;
constexpr const char* NONE = "none";
constexpr const char* ANY = "any";
#define SS2K_LOGD(...) ((void)0)
uint32_t millis() { return 100; } // Deliberately identical timestamps for every packet.
struct Measurement {
  int value = 0; bool simulate = false;
  void setValue(int v, bool sim = false) { value = v; simulate = sim; }
  bool getSimulate() { return simulate; }
};
struct Runtime {
  Measurement watts, cad, hr, resistance;
  void setSimulatedSpeed(float) {}
} runtime;
auto* rtConfig = &runtime;
struct Config {
  const char* meter = ANY;
  const char* getConnectedPowerMeter() { return meter; }
  float getPowerCorrectionFactor() { return 1; }
} config;
auto* userConfig = &config;
struct Controller { bool table = false; bool usePowerTableForPower() { return table; } } controller;
auto* ss2k = &controller;
struct Server { unsigned samples = 0; void notePowerSample() { ++samples; } } spinBLEServer;
struct NotifyData { int charUUID = 1, serviceUUID = 1; uint8_t data[25] = {}; size_t length = 0; };
struct Slot {
  int connectedClientID = 1, serviceUUID = 1;
  bool doConnect = false, isGrupetto = false, assigned = true;
  std::string uniqueName;
  uint32_t lastDataUpdateTime = 0;
  std::deque<NotifyData> queue;
  bool getAdvertisement() { return assigned; }
  NotifyData dequeueData() { if (queue.empty()) return {}; auto data = queue.front(); queue.pop_front(); return data; }
};
struct Client {
  Slot myBLEDevices[NUM_BLE_DEVICES];
  bool connectedPM = false, connectedHRM = false, connectedCD = false, connectedSpeed = false;
} spinBLEClient;
struct SensorData {
  int power = 0;
  bool hasPower() { return true; }
  bool hasHeartRate() { return false; }
  bool hasCadence() { return false; }
  bool hasSpeed() { return false; }
  bool hasResistance() { return false; }
  int getPower() { return power; }
  int getHeartRate() { return 0; }
  float getCadence() { return 0; }
  float getSpeed() { return 0; }
  int getResistance() { return 0; }
};
struct Factory {
  std::vector<int> parsed;
  std::shared_ptr<SensorData> getSensorData(int, const std::string&, uint8_t* data, size_t) {
    parsed.push_back(*data);
    auto sensor = std::make_shared<SensorData>();
    sensor->power = *data == 255 ? 5000 : *data;
    return sensor;
  }
} sensorDataFactory;
METHODS
int main() {
  for (int i = 0; i < NUM_BLE_DEVICES; ++i) {
    auto& slot = spinBLEClient.myBLEDevices[i];
    slot.uniqueName = std::to_string(i);
    for (int j = 0; j < 6; ++j) {
      NotifyData packet;
      packet.length = 1; packet.data[0] = i * 10 + j;
      slot.queue.push_back(packet);
    }
  }
  BLEProcessSensorData();
  assert(spinBLEServer.samples == 12); // Four per device: none can monopolize the pass.
  for (const auto& slot : spinBLEClient.myBLEDevices) assert(slot.queue.size() == 2);
  assert((sensorDataFactory.parsed == std::vector<int>{0,1,2,3,10,11,12,13,20,21,22,23}));
  BLEProcessSensorData();
  assert(spinBLEServer.samples == 18 && rtConfig->watts.value == 25);
  uint8_t power = 200;
  collectAndSet(1, 1, "0", &power, 1);
  collectAndSet(1, 1, "0", &power, 1);
  assert(spinBLEServer.samples == 20); // Repeated watts in the same millisecond remain fresh samples.
  power = 0;
  collectAndSet(1, 1, "0", &power, 1);
  assert(spinBLEServer.samples == 21 && rtConfig->watts.value == 0);
  power = 255;
  collectAndSet(1, 1, "0", &power, 1);
  assert(spinBLEServer.samples == 21); // Out-of-range data does not accelerate output.
  power = 200;
  rtConfig->watts.simulate = true;
  collectAndSet(1, 1, "0", &power, 1);
  rtConfig->watts.simulate = false;
  ss2k->table = true;
  collectAndSet(1, 1, "0", &power, 1);
  ss2k->table = false;
  userConfig->meter = "external";
  collectAndSet(PELOTON_DATA_UUID, PELOTON_DATA_UUID, "0", &power, 1);
  assert(spinBLEServer.samples == 21); // Honor simulation, power-table, and source-selection gates.
}
'''.replace("METHODS", methods))
