# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only

"""Companion clients with notification-only CCCDs still receive queued replies."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestCompanionSubscription(unittest.TestCase):
    def test_response_subscription_compatibility(self):
        source = (ROOT / "src/BLE_Custom_Characteristic.cpp").read_text()
        production = function(source, "bool queueIndication(")
        harness = r'''
#include <cassert>
#include <algorithm>
#include "CustomCharacteristicEvents.h"
#include "BleTxScheduler.h"
constexpr uint16_t BLE_HS_CONN_HANDLE_NONE=UINT16_MAX;
using Tx=BleTxScheduler<4>;
Tx bleTx;
CustomCharacteristicEvents<4> bleEvents;
uint32_t millis(){return 0;}
struct Server { uint16_t getPeerMTU(uint16_t){return 23;} } server;
struct NimBLEDevice { static Server* getServer(){return &server;} };
PRODUCTION
int main(){
 for(uint16_t peer=1;peer<=4;++peer){bleEvents.connect(peer);bleEvents.subscribe(peer,23,peer-1);}
 const uint8_t response[]={0x80,0x01,0x02};
 assert(!queueIndication(response,sizeof(response),1,Tx::Kind::Response)); // CCCD disabled.
 assert(queueIndication(response,sizeof(response),2,Tx::Kind::Response)); // Notify-only app.
 assert(queueIndication(response,sizeof(response),3,Tx::Kind::Snapshot)); // Indicate-only app.
 assert(queueIndication(response,sizeof(response),4,Tx::Kind::Response)); // Both enabled.
 std::vector<uint16_t> delivered;
 auto connected=[](uint16_t peer,uint64_t session){return bleEvents.connected(peer,session);};
 auto send=[&](const Tx::Packet& packet){delivered.push_back(packet.peer);assert(packet.data==std::vector<uint8_t>({0x80,0x01,0x02}));return true;};
 assert(bleTx.pump(20,true,connected,send));
 assert(bleTx.pump(40,true,connected,send));
 assert(bleTx.pump(60,true,connected,send));
 assert(delivered==std::vector<uint16_t>({2,3,4}));
 assert(queueIndication(response,sizeof(response),2,Tx::Kind::Response));
 assert(!bleTx.pump(80,true,connected,send)); // Legacy compatibility must retain ACK pacing.
 Tx::Kind kind;
 auto peers=bleEvents.recipients();
 assert(bleTx.complete(2,peers[1].session,kind));
 assert(bleTx.pump(100,true,connected,send));
 bleEvents.disconnect(2);
 assert(!queueIndication(response,sizeof(response),2,Tx::Kind::Response));
}
'''.replace("PRODUCTION", production)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory)/"companion.cpp"
            exe = Path(directory)/"companion"
            cpp.write_text(harness)
            subprocess.run([shutil.which("g++"), "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT/"lib/SS2K/include"), str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
