"""Exercise production BLE slot ownership and scan scheduling with fake peripherals."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestBleScanLifetime(unittest.TestCase):
    def test_scan_results_survive_restart_and_pending_connections_take_priority(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("g++ is required")

        source = (ROOT / "src/BLE_Client.cpp").read_text(encoding="utf-8")
        header = (ROOT / "include/BLE_Common.h").read_text(encoding="utf-8")
        slot_class = function(header, "class SpinBLEAdvertisedDevice") + ";"
        methods = "\n".join(function(source, signature) for signature in (
            "void SpinBLEAdvertisedDevice::set(",
            "void SpinBLEAdvertisedDevice::clearState(",
            "void SpinBLEAdvertisedDevice::reset(",
            "void SpinBLEClient::scanProcess(",
        ))
        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>
using String = std::string;
using BLEUUID = int;
using NimBLEUUID = int;
using QueueHandle_t = void*;
constexpr int BLE_HS_CONN_HANDLE_NONE = 65535;
constexpr int HEARTSERVICE_UUID = 1, CSCSERVICE_UUID = 2, CYCLINGPOWERSERVICE_UUID = 3;
constexpr int FITNESSMACHINESERVICE_UUID = 4, FLYWHEEL_UART_SERVICE_UUID = 5, ECHELON_DEVICE_UUID = 6;
constexpr int CHRONO_SERVICE_UUID = 7, PELOTON_DATA_UUID = 8, HID_SERVICE_UUID = 9;
constexpr const char* NONE = "none";
constexpr const char* ANY = "any";
#define SS2K_LOG(...) ((void)0)
#define SS2K_LOGE(...) ((void)0)
struct Measurement {};
struct NotifyData {};
QueueHandle_t xQueueCreate(int, size_t) { return reinterpret_cast<void*>(1); }
void xQueueReset(QueueHandle_t) {}
bool bleDeviceIdentifierEquals(const char* a, const char* b) { return strcmp(a, b) == 0; }
struct NimBLEAddress {
  std::string value;
  std::string toString() const { return value; }
};
struct NimBLEAdvertisedDevice {
  NimBLEAddress address;
  std::string name;
  std::vector<uint8_t> payload;
  NimBLEAddress getAddress() const { return address; }
};
struct NimBLERemoteService { BLEUUID getUUID() const { return CSCSERVICE_UUID; } };
struct NimBLEClient {
  std::vector<NimBLERemoteService*> getServices(bool) { return {}; }
};
struct NimBLEScan {
  bool scanning = false, succeed = true;
  int starts = 0;
  bool isScanning() const { return scanning; }
  bool start(int, bool, bool) { ++starts; return succeed; }
} scanner;
struct NimBLEDevice {
  static NimBLEClient* getClientByPeerAddress(NimBLEAddress) { return nullptr; }
  static NimBLEScan* getScan() { return &scanner; }
};
struct UserConfig {
  const char* getConnectedHeartMonitor() { return NONE; }
  void setFoundDevices(const char*) {}
} config;
UserConfig* userConfig = &config;
struct BLE_ss2kCustomCharacteristic {
  static void beginScanResults() {}
  static void endScanResults() {}
};
/* SLOT */
struct SpinBLEClient {
  bool connectedHRM = false, connectedPM = false, connectedCD = false;
  bool connectedSpeed = false, connectedRemote = false, doScan = true;
  SpinBLEAdvertisedDevice myBLEDevices[2];
  String adevName2UniqueName(const NimBLEAdvertisedDevice* device) { return device->name; }
  void scanProcess(int);
} spinBLEClient;
/* METHODS */
int main() {
  auto& slot = spinBLEClient.myBLEDevices[1];
  {
    NimBLEAdvertisedDevice result{{"A0:00:00:00:00:B1"}, "CAD-BLE0418789 B1", {0x16, 0x18}};
    slot.set(&result);
    // A later scan can reuse/mutate the scanner's result before clearing it.
    result.name = "unrelated device";
    result.address.value = "00:00:00:00:00:00";
    result.payload.clear();
    assert(slot.getAdvertisement()->name == "CAD-BLE0418789 B1");
    assert(slot.getAdvertisement()->payload.size() == 2);
  }
  // Scanner-owned result is now destroyed. The slot must still be usable.
  assert(slot.getAdvertisement()->getAddress().toString() == "A0:00:00:00:00:B1");
  assert(slot.uniqueName == "CAD-BLE0418789 B1");
  auto attempt = slot.getAdvertisement();
  slot.set(nullptr);
  assert(slot.getAdvertisement() == attempt);
  slot.reset(false);
  assert(slot.getAdvertisement() == attempt);
  // An in-flight attempt retains the data across a full reset.
  slot.reset(true);
  assert(!slot.getAdvertisement());
  assert(attempt->payload[0] == 0x16);
  assert(attempt->name == "CAD-BLE0418789 B1");
  // Refreshing from the owned snapshot itself must also be safe.
  slot.set(attempt.get());
  slot.set(slot.getAdvertisement().get());
  assert(slot.uniqueName == "CAD-BLE0418789 B1");

  // Readers hold an immutable snapshot while callbacks replace/clear the slot.
  std::thread callback([&] {
    for (int i = 0; i < 10000; ++i) {
      slot.reset(true);
      slot.set(attempt.get());
    }
  });
  for (int i = 0; i < 10000; ++i) {
    auto current = slot.getAdvertisement();
    if (current) {
      assert(current->name == "CAD-BLE0418789 B1");
      assert(current->payload[0] == 0x16);
    }
  }
  callback.join();

  // A match arriving after the connection pass wins over a requested scan.
  slot.doConnect = true;
  spinBLEClient.scanProcess(5000);
  assert(scanner.starts == 0);
  assert(spinBLEClient.doScan);
  slot.doConnect = false;
  spinBLEClient.scanProcess(5000);
  assert(scanner.starts == 1);
  assert(!spinBLEClient.doScan);
  scanner.succeed = false;
  spinBLEClient.scanProcess(5000);
  assert(spinBLEClient.doScan);
}
'''.replace("/* SLOT */", slot_class).replace("/* METHODS */", methods)

        with tempfile.TemporaryDirectory(prefix="ss2k-ble-lifetime-") as directory:
            directory = Path(directory)
            cpp = directory / "regression.cpp"
            executable = directory / "regression.exe"
            cpp.write_text(harness, encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", "-pthread", "-Wall", "-Wextra", str(cpp), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
