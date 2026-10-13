"""Behavior tests: characteristic packets traverse firmware callback/queue/collector."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from scripts.erg_sim.virtual_ble import prepare, ROOT


@unittest.skipUnless(shutil.which('g++'), 'g++ required')
class VirtualBleIngressTests(unittest.TestCase):
    def test_production_packet_ingress(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            (folder/'Arduino.h').write_text('''#pragma once
#include <cstdint>
#include <string>
using String=std::string;
extern uint32_t clockMs;
inline unsigned long millis(){return clockMs;}
''')
            prepared = prepare(folder)
            declarations, implementation, sources = (prepared[k] for k in ("declarations", "implementation", "sources"))
            source = r'''
#include <cmath>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <cstdio>
#include "SmartSpin_parameters.h"
#include "BLE_Definitions.h"
uint32_t clockMs=1000;
#define SS2K_LOG(...) ((void)0)
#define SS2K_LOGW(...) ((void)0)
RuntimeParameters runtime; RuntimeParameters*rtConfig=&runtime;
userParameters config; userParameters*userConfig=&config;
'''+declarations+r'''
struct SS2K {bool isUpdating=false; bool usePowerTableForPower(){return false;} int getCurrentPosition(){return 0;}} controller;
SS2K*ss2k=&controller;
'''+implementation+r'''
int main(){
 userConfig->setPowerCorrectionFactor(1); initializeVirtualBle();
 assert(spinBLEClient.connectedPM);
 assert(!injectVirtualBle("2a63",{0}));
 assert(!injectVirtualBle("2a5b",{2,1}));
 assert(!injectVirtualBle("2ad2",{0x44,0}));
 assert(!injectVirtualBle("2a63",std::vector<uint8_t>(26,0)));
 assert(!injectVirtualBle("ffff",{0,0,200,0}));
 assert(virtualBleNotify(0x2a63,{0,0,200,0}));
 assert(rtConfig->watts.valueTimestamp()==0); // callback only queues
 pollVirtualBle();
 assert(rtConfig->watts.value()==200);
 assert(injectVirtualBle("2a5b",{2,1,0,0,4})); pollVirtualBle();
 clockMs+=1000;
 assert(injectVirtualBle("2a5b",{2,2,0,0,8})); pollVirtualBle();
 assert(rtConfig->cad.value()==60);
 // Repeated equal values are new accepted reports with a new timestamp.
 assert(injectVirtualBle("2a63",{0,0,200,0}));pollVirtualBle();
 assert(rtConfig->watts.valueTimestamp()==clockMs);
 // Firmware queue accepts six reports and drops later ones until drained.
 for(int p=1;p<=7;p++)injectVirtualBle("2a63",{0,0,static_cast<uint8_t>(p),0});
 pollVirtualBle();assert(rtConfig->watts.value()==6);
 // Fresh repeated crank counters become zero cadence on virtual time.
 clockMs+=3000;injectVirtualBle("2a5b",{2,2,0,0,8});pollVirtualBle();
 assert(rtConfig->cad.value()==0);
 clockMs+=3000;injectVirtualBle("2a63",{0,0,0,0});pollVirtualBle();
 assert(rtConfig->watts.getValue()==0);
 assert(virtualBleStats.accepted>0 && virtualBleStats.processed>0 && virtualBleStats.queueDropped==1);
 // Disconnect must clear queued packets, so reconnect cannot consume old samples.
 injectVirtualBle("2a63",{0,0,240,0});
 disconnectVirtualBle();assert(!spinBLEClient.connectedPM);
 assert(!injectVirtualBle("2a63",{0,0,250,0}));pollVirtualBle();
 assert(rtConfig->watts.value()==0);
 initializeVirtualBle(); clockMs+=1000;
 injectVirtualBle("2ad2",{0x44,0,0,0,180,0,210,0});pollVirtualBle();
 assert(rtConfig->watts.value()==210);assert(rtConfig->cad.value()==90);
 clockMs+=5001;pollVirtualBle();assert(!NimBLEDevice::client.isConnected());
 virtualBleConnect("cps_csc");
 assert(!virtualBleNotify(0x2ad2,{0x44,0,0,0,180,0,210,0}));
 assert(virtualBleNotify(0x2a63,{0,0,201,0}));pollVirtualBle();
 assert(rtConfig->watts.getValue()==201);
}
'''
            # Public measurement accessors are checked against the selected source.
            source = source.replace('.valueTimestamp()', '.getValueTimestamp()').replace('.value()', '.getValue()')
            cpp = folder/'test.cpp';cpp.write_text(source)
            cmd=['g++','-std=c++17','-DPLATFORMIO_ENV_NATIVE','-I'+str(folder),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'lib/SS2K/include'),'-I'+str(ROOT/'lib/ArduinoCompat/include'),str(cpp),*sources,'-o',str(folder/'test')]
            result = subprocess.run(cmd,capture_output=True,text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(folder/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

if __name__=='__main__':unittest.main()
