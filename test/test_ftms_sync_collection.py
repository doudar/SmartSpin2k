"""Exercise production power-table collection and fitting with fresh 1 Hz reports."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]

ARDUINO = r'''
#pragma once
#include <cstdint>
#include <string>
using String = std::string;
extern uint32_t clockMs;
inline unsigned long millis() { return clockMs; }
'''

HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include "Power_Table.h"
uint32_t clockMs = 1000;
#define SS2K_LOG(...) ((void)0)
RuntimeParameters runtime; RuntimeParameters* rtConfig = &runtime;
userParameters config; userParameters* userConfig = &config;
struct Motor {
  int current = 15000, target = 15000;
  int getCurrentPosition() { return current; }
  int getTargetPosition() { return target; }
} motor;
Motor* ss2k = &motor;
uint16_t notified = 0;
struct BLE_ss2kCustomCharacteristic { static void notify(int, int row) { notified |= 1u << row; } };
int commits = 0;
bool PowerTable::_manageSaveState(bool, bool) { ++commits; return true; }
void PowerTable::toLog() {}
/* PRODUCTION */
ptIndex index(int row, int col) { ptIndex i; i.cadIndex=row; i.wattIndex=col; return i; }
void seed(PowerTable& table, int row, int col, int position, int weight=20) {
  auto& cell=table.ptData.tableRow[row].tableEntry[col];
  cell=TableEntry{}; cell.targetPosition=position; cell.readings=weight;
}
void observe(PowerTable& table, int watts, int cad, float position) {
  PowerBuffer b; b.powerEntry[0].readings=1; b.powerEntry[0].watts=watts;
  b.powerEntry[0].cad=cad; b.powerEntry[0].targetPosition=position;
  table.newEntry(b);
}
void monotonic(const PTData& table) {
  for(int row=0;row<POWERTABLE_CAD_SIZE;++row) {
    int previous=INT16_MIN;
    for(const auto& cell:table.tableRow[row].tableEntry) {
      if(cell.readings<2)continue;
      assert(cell.targetPosition>=previous); previous=cell.targetPosition;
    }
  }
  for(int col=0;col<POWERTABLE_WATT_SIZE;++col) {
    int previous=INT16_MAX;
    for(int row=0;row<POWERTABLE_CAD_SIZE;++row) {
      const auto& cell=table.tableRow[row].tableEntry[col];
      if(cell.readings<2)continue;
      assert(cell.targetPosition<=previous); previous=cell.targetPosition;
    }
  }
}
int main(int argc, char** argv) {
  std::string scenario=argv[1]; PowerTable table; PowerBuffer buffer;
  auto feed=[&](bool publish=true, int watts=210, int cadence=80, bool allowed=true) {
    clockMs+=1000;
    if(publish) rtConfig->watts.setValue(watts);
    table.processPowerValue(buffer,cadence,rtConfig->watts,allowed);
  };
  if(scenario=="duplicates") {
    feed(); feed(); feed(); assert(buffer.getReadings()==1);
    for(int i=0;i<20;++i) {clockMs+=10;rtConfig->watts.setTarget(300);table.processPowerValue(buffer,80,rtConfig->watts);}
    assert(buffer.getReadings()==1 && commits==0);
    for(int i=0;i<4;++i)feed();
    assert(commits==1 && buffer.getReadings()==0);
  } else if(scenario=="fresh_equal" || scenario=="wrap") {
    if(scenario=="wrap")clockMs=UINT32_MAX-3000;
    for(int i=0;i<7;++i)feed();
    assert(commits==1); assert(table.ptData.tableRow[4].tableEntry[7].targetPosition==1500);
    for(int i=0;i<5;++i)feed(); assert(commits==2);
  } else if(scenario=="motion" || scenario=="pending_motion" || scenario=="cadence" || scenario=="power_spike" || scenario=="gap" || scenario=="stop" ||
            scenario=="blocked" || scenario=="ftms" || scenario=="epoch" || scenario=="simulated") {
    for(int i=0;i<5;++i)feed(); assert(buffer.getReadings()==3);
    if(scenario=="motion")motor.current=motor.target=15500;
    if(scenario=="pending_motion")motor.target=15500;
    if(scenario=="gap")clockMs+=3000;
    if(scenario=="ftms")table.ftmsPositionUncertain=true;
    if(scenario=="epoch")++table.positionEpoch;
    if(scenario=="simulated")rtConfig->watts.setSimulate(true);
    feed(scenario!="gap",scenario=="power_spike"?300:210,scenario=="stop"?0:scenario=="cadence"?85:80,scenario!="blocked");
    assert(buffer.getReadings()==0 && commits==0);
    table.ftmsPositionUncertain=false;rtConfig->watts.setSimulate(false);motor.current=motor.target=16000;
    for(int i=0;i<7;++i)feed();
    assert(commits==1 && table.ptData.tableRow[4].tableEntry[7].targetPosition==1600);
  } else if(scenario=="poll_700ms") {
    clockMs=0;
    for(int t=0;t<=14000;t+=100) {
      clockMs=t;
      if(t%1000==0)rtConfig->watts.setValue(210);
      if(t%700==0)table.processPowerValue(buffer,80,rtConfig->watts);
    }
    assert(commits==2 && buffer.getReadings()==3);
  } else if(scenario=="small_moves") {
    for(int i=0;i<7;++i) {motor.current+=10; motor.target=motor.current+20;feed();}
    assert(commits==1); assert(table.ptData.tableRow[4].tableEntry[7].targetPosition==1505);
  } else if(scenario=="burst") {
    for(int i=0;i<3;++i)feed();assert(buffer.getReadings()==1);
    for(int i=0;i<5;++i){clockMs+=100;rtConfig->watts.setValue(210);table.processPowerValue(buffer,80,rtConfig->watts);}
    assert(buffer.getReadings()==1 && commits==0);
  } else if(scenario=="up" || scenario=="down" || scenario=="fraction") {
    seed(table,4,8,1500);
    float value=scenario=="up"?1510:scenario=="down"?1490:1500.6f;
    for(int i=0;i<100;++i)table.ptHelpers.enterData(table.ptData,index(4,8),value);
    assert(table.ptData.tableRow[4].tableEntry[8].targetPosition==std::lround(value));
  } else if(scenario=="interior" || scenario=="edge" || scenario=="power_neighbor") {
    int row=scenario=="edge"?1:4; seed(table,row,8,1500);
    int nr=scenario=="power_neighbor"?row:row-1, nc=scenario=="power_neighbor"?9:8; seed(table,nr,nc,1500);
    auto mask=table.ptHelpers.enterData(table.ptData,index(row,8),1600);
    assert(table.ptData.tableRow[row].tableEntry[8].targetPosition>1500);
    assert(table.ptData.tableRow[nr].tableEntry[nc].targetPosition>1500);
    assert(mask & (1u<<nr));
    for(int i=0;i<50;++i)table.ptHelpers.enterData(table.ptData,index(row,8),1600);
    assert(table.ptData.tableRow[row].tableEntry[8].targetPosition>=1599);
    assert(table.ptData.tableRow[nr].tableEntry[nc].readings==20);monotonic(table.ptData);
  } else if(scenario=="outlier") {
    seed(table,4,8,1500);table.ptHelpers.enterData(table.ptData,index(4,8),1600);
    assert(table.ptData.tableRow[4].tableEntry[8].targetPosition==1520);
    for(int i=0;i<20;++i)table.ptHelpers.enterData(table.ptData,index(4,8),1500);
    assert(table.ptData.tableRow[4].tableEntry[8].targetPosition==1500);
  } else if(scenario=="grid" || scenario=="grid_cadence") {
    seed(table,4,6,1400);seed(table,4,8,2000);
    int cad=scenario=="grid"?80:82;
    float pos=200.0f*80/cad*10-400;
    observe(table,200,cad,pos);
    assert(table.ptData.tableRow[4].tableEntry[7].targetPosition==1700);
  } else if(scenario=="bootstrap" || scenario=="bootstrap_exact" || scenario=="anchor_epoch") {
    observe(table,200,80,1600);assert(table.ptHelpers.getTotalReadings(table.ptData)==0);
    if(scenario=="anchor_epoch") {++table.positionEpoch;observe(table,260,80,2200);assert(table.ptHelpers.getTotalReadings(table.ptData)==0);return 0;}
    observe(table,scenario=="bootstrap"?260:270,80,scenario=="bootstrap"?2200:2300);
    assert(table.ptData.tableRow[4].tableEntry[7].targetPosition==1700);
    assert(table.ptData.tableRow[4].tableEntry[9].targetPosition==2300);
  } else if(scenario=="import") {
    seed(table,4,8,1500);table.ptHelpers.enterData(table.ptData,index(4,8),1502);
    seed(table,4,8,1500);table.ptHelpers.enterData(table.ptData,index(4,8),1500);
    assert(table.ptData.tableRow[4].tableEntry[8].learningPosition==1500);
    table.ptData.tableRow[4].tableEntry[8].targetPosition=1400;
    table.ptHelpers.enterData(table.ptData,index(4,8),1400);
    assert(table.ptData.tableRow[4].tableEntry[8].targetPosition==1400);
  } else if(scenario=="random_surface") {
    std::mt19937 random(42);
    for(int i=0;i<1500;++i) {
      int row=random()%10,col=random()%30;
      table.ptHelpers.enterData(table.ptData,index(row,col),static_cast<int>(random()%3000)-1000);
      monotonic(table.ptData);
    }
  } else if(scenario=="notify_neighbors") {
    seed(table,4,7,1500);seed(table,3,7,1500);
    observe(table,210,80,1600);assert((notified & (1u<<3)) && (notified & (1u<<4)));
  } else { assert(false); }
}
'''


class TestPowerTableLearning(unittest.TestCase):
    def test_collection_and_adaptation(self):
        source = (ROOT / "src/Power_Table.cpp").read_text(encoding="utf-8")
        production = "\n".join(function(source, signature) for signature in (
            "void PowerBuffer::set(", "void PowerBuffer::clearSamples(", "void PowerBuffer::reset(",
            "int PowerBuffer::getReadings(", "void PowerTable::processPowerValue(", "void PowerTable::newEntry("))
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required for integration regressions")
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / "Arduino.h").write_text(ARDUINO, encoding="utf-8")
            cpp = folder / "learning.cpp"
            exe = folder / "learning.exe"
            cpp.write_text(HARNESS.replace("/* PRODUCTION */", production), encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", "-DPLATFORMIO_ENV_NATIVE", "-I"+str(folder),
                            "-I"+str(ROOT / "include"), "-I"+str(ROOT / "lib/SS2K/include"),
                            "-I"+str(ROOT / "lib/ArduinoCompat/include"), str(cpp),
                            str(ROOT / "src/PowerTable_Helpers.cpp"), "-o", str(exe)], check=True)
            for scenario in ("duplicates", "fresh_equal", "wrap", "motion", "pending_motion", "cadence", "power_spike",
                             "gap", "stop", "blocked", "ftms", "epoch", "simulated", "poll_700ms", "small_moves", "burst", "up", "down", "fraction",
                             "interior", "edge", "power_neighbor", "outlier", "grid", "grid_cadence", "bootstrap", "bootstrap_exact",
                             "anchor_epoch", "import", "random_surface", "notify_neighbors"):
                with self.subTest(scenario=scenario):
                    subprocess.run([str(exe), scenario], check=True)
