#
# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only
#

"""BLE settings callbacks defer work and discard events from disconnected sessions."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestCustomCharacteristicEvents(unittest.TestCase):
    def test_callbacks_queue_order_capacity_and_connection_reuse(self):
        source = (ROOT / "src/BLE_Custom_Characteristic.cpp").read_text(encoding="utf-8")
        callbacks = "\n".join(function(source, signature) for signature in (
            "void ss2kCustomCharacteristicCallbacks::onWrite(",
            "void ss2kCustomCharacteristicCallbacks::onStatus(",
            "void BLE_ss2kCustomCharacteristic::onConnect(",
            "void BLE_ss2kCustomCharacteristic::onDisconnect(",
        ))
        harness = r'''
#include <cassert>
#include "CustomCharacteristicEvents.h"
CustomCharacteristicEvents<2> bleEvents;
struct NimBLEConnInfo {
  uint16_t handle;
  uint16_t getConnHandle() { return handle; }
  uint16_t getMTU() { return 515; }
};
struct NimBLECharacteristic {
  std::string value;
  std::string getValue() { return value; }
};
struct BLE_ss2kCustomCharacteristic {
  static void onConnect(uint16_t);
  static void onDisconnect(uint16_t);
};
struct ss2kCustomCharacteristicCallbacks {
  void onWrite(NimBLECharacteristic*, NimBLEConnInfo&);
  void onStatus(NimBLECharacteristic*, NimBLEConnInfo&, int);
};
CALLBACKS
int main() {
  ss2kCustomCharacteristicCallbacks callback;
  NimBLECharacteristic characteristic{std::string(512, 'x')};
  NimBLEConnInfo first{1}, second{2};
  decltype(bleEvents)::Event event;
  callback.onWrite(&characteristic, first);
  assert(!bleEvents.pop(event)); // No connection, no delayed command.
  BLE_ss2kCustomCharacteristic::onConnect(1);
  BLE_ss2kCustomCharacteristic::onConnect(2);
  callback.onWrite(&characteristic, first);
  characteristic.value = "second";
  callback.onWrite(&characteristic, second);
  callback.onStatus(&characteristic, first, 0);
  assert(bleEvents.pop(event) && event.peer == 1 && event.mtu == 515);
  assert(!event.isStatus && event.value == std::string(512, 'x'));
  const auto oldSession = event.session;
  assert(bleEvents.pop(event) && event.peer == 2 && event.value == "second");
  assert(!bleEvents.pop(event)); // Notification success must not advance a snapshot.

  callback.onWrite(&characteristic, first);
  callback.onStatus(&characteristic, first, 14);
  callback.onWrite(&characteristic, second);
  BLE_ss2kCustomCharacteristic::onDisconnect(1);
  BLE_ss2kCustomCharacteristic::onConnect(1);
  assert(!bleEvents.connected(1, oldSession));
  callback.onWrite(&characteristic, first);
  assert(bleEvents.pop(event) && event.peer == 2); // Other connection is preserved.
  assert(bleEvents.pop(event) && event.peer == 1 && event.session != oldSession);
  assert(!bleEvents.pop(event)); // Old write and status were both discarded.

  for (unsigned i = 0; i < 8; ++i) callback.onWrite(&characteristic, first);
  callback.onStatus(&characteristic, second, 14); // Reserved acknowledgment slot.
  assert(bleEvents.takeDropped() == 1 && bleEvents.takeDropped() == 0);
  for (unsigned i = 0; i < 7; ++i) assert(bleEvents.pop(event) && !event.isStatus);
  assert(bleEvents.pop(event) && event.isStatus && event.peer == 2 && event.status == 14);
  assert(!bleEvents.pop(event));
}
'''.replace("CALLBACKS", callbacks)
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required")
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "events.cpp"
            exe = Path(directory) / "events.exe"
            cpp.write_text(harness, encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", "-pthread", "-I", str(ROOT / "lib/SS2K/include"), str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
