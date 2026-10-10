# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only

"""Exercise production cache-only subscription setup without BLE hardware."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestConnectionSetup(unittest.TestCase):
    def test_cached_services_and_subscription_failures(self):
        source = (ROOT / 'src/BLE_Client.cpp').read_text()
        methods = '\n'.join(function(source, signature) for signature in (
            'static NimBLERemoteService* cachedService(',
            'bool subscribeToAllNotifications('))
        harness = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#define SS2K_LOG(...) ((void)0)
#define SS2K_LOGW(...) ((void)0)
using NimBLEUUID = int;
constexpr int HID_SERVICE_UUID=8, FITNESSMACHINESERVICE_UUID=7, FITNESSMACHINECONTROLPOINT_UUID=70;
uint32_t clockMs=0;
uint32_t millis() { return clockMs; }
struct NimBLERemoteService { int uuid; int getUUID() { return uuid; } };
struct NimBLEClient {
  bool connected=true;
  std::vector<NimBLERemoteService*> services;
  bool isConnected() { return connected; }
  unsigned getConnHandle() { return 1; }
  const auto& getServices(bool refresh) { assert(!refresh); return services; }
  // Deliberately no getService(): absent UUIDs must never trigger radio probes.
};
struct Service { int serviceUUID, characteristicUUID; };
namespace BLEServices { const std::vector<Service> SUPPORTED_SERVICES={{1,10},{2,20},{7,71},{8,80},{9,90}}; }
std::vector<int> subscribed;
bool success=true, disconnectDuring=false;
bool subscribeToCharacteristic(NimBLEClient* client, NimBLERemoteService*, const Service&, int uuid) {
  subscribed.push_back(uuid);
  if(disconnectDuring) client->connected=false;
  return success;
}
METHODS
int main() {
  NimBLERemoteService power{1}, ftms{7}, hid{8};
  NimBLEClient client;
  client.services={&power,&ftms};
  assert(subscribeToAllNotifications(&client));
  assert((subscribed==std::vector<int>{10,71,70}));
  subscribed.clear(); client.services={&hid};
  assert(subscribeToAllNotifications(&client)); assert(subscribed.empty());
  client.services.clear(); assert(!subscribeToAllNotifications(&client));
  client.services={&power}; success=false;
  assert(!subscribeToAllNotifications(&client));
  success=true; disconnectDuring=true;
  assert(!subscribeToAllNotifications(&client));
}
'''.replace('METHODS', methods)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'setup.cpp'
            exe = Path(directory) / 'setup'
            cpp.write_text(harness)
            subprocess.run([shutil.which('g++'), '-std=c++17', str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
