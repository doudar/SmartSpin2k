"""Exercise production ERG orchestration with a fake motor and delayed power.

Run: python -B -m unittest discover -s test -p test_erg_feedback.py
"""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

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
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <deque>
#include <string>
#include <vector>
#include "ERG_Mode.h"
uint32_t clockMs = 100;
std::vector<std::string> logs;
void record(const char*, const char* format, ...) {
  char buf[512]; va_list args; va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args); va_end(args);
  logs.emplace_back(buf);
}
#define SS2K_LOG record
#define SS2K_LOGW(...) ((void)0)
struct FitnessMachineControlPointProcedure { enum { SetTargetPower=5, SetIndoorBikeSimulationParameters=17 }; };
RuntimeParameters runtime;
RuntimeParameters* rtConfig = &runtime;
userParameters config;
userParameters* userConfig = &config;
void userParameters::saveToLittleFS() {}
struct SS2K {
  int32_t current=10083, target=10083;
  bool stepperIsRunning=false, resetPowerTableFlag=false;
  int32_t getCurrentPosition() { return current; }
  int32_t getTargetPosition() { return target; }
  bool usePowerTableForPower() { return false; }
} motor;
SS2K* ss2k = &motor;
struct { bool connectedPM=true; } spinBLEClient;
struct { int spinDownFlag=0; } spinBLEServer;
struct { void remove(const char*) {} } LittleFS;
void PowerBuffer::reset() { for(auto& entry:powerEntry) entry.readings=0; }
int PowerBuffer::getReadings() { int n=0; for(const auto& entry:powerEntry) n+=entry.readings!=0; return n; }
struct PowerTable {
  PTData ptData;
  bool saveFlag=false, _hasBeenLoadedThisSession=true;
  int lookupResult=11866, samples=0, loads=0;
  bool lookupErgSlope(int, int, double&, PowerTableSlopeStatus::Value* status) {
    *status=PowerTableSlopeStatus::InsufficientRows; return false;
  }
  int lookup(int,int) { return lookupResult; }
  int lookupWatts(int,int) { return 155; }
  void processPowerValue(PowerBuffer&,int,Measurement) { ++samples; }
  void setStepperMinMax() {}
  void _save() {}
  void _manageSaveState(bool=false) { ++loads; _hasBeenLoadedThisSession=true; }
  void reset() {}
} table;
PowerTable* powerTable=&table;
/* PRODUCTION_ERG */

ErgMode controller;
bool logged(const std::string& needle) {
  for(const auto& text:logs) if(text.find(needle)!=std::string::npos) return true;
  return false;
}
void setup(int watts=196, int target=310) {
  userConfig->setERGSensitivity(5);
  userConfig->setStepperSpeed(3500);
  userConfig->setShiftStep(1000);
  userConfig->setMinWatts(50);
  rtConfig->setFTMSMode(5); rtConfig->setHomed(true);
  rtConfig->setMinStep(0); rtConfig->setMaxStep(24482);
  rtConfig->setTargetIncline(motor.current);
  rtConfig->watts.setValue(watts); rtConfig->watts.setTarget(target);
  rtConfig->cad.setValue(94);
}
void step(uint32_t when, int watts, bool arrive=true, bool publish=true) {
  clockMs=when;
  motor.target=std::max(rtConfig->getMinStep(),std::min(rtConfig->getMaxStep(),static_cast<int32_t>(rtConfig->getTargetIncline())));
  if(arrive) motor.current=motor.target;
  motor.stepperIsRunning=motor.current!=motor.target;
  if(publish) rtConfig->watts.setValue(watts);
  controller.runERG();
}
void beginSeek() { setup(); step(1000,196); assert(rtConfig->getTargetIncline()==11866); }

int main(int argc,char** argv) {
  assert(argc==2); std::string scenario=argv[1];
  if(scenario=="ride_handoff") {
    // Captured 155 -> 310 W transition: motor arrived while power still
    // reported 198 W. The old 700 ms handoff commanded another 700 steps.
    beginSeek(); table._hasBeenLoadedThisSession=false; step(2000,198); step(2700,198);
    assert(rtConfig->getTargetIncline()==11866);
    step(3400,235); assert(rtConfig->getTargetIncline()==11866);
    step(4400,289); assert(rtConfig->getTargetIncline()==11866);
    assert(table.samples==1); // Pending feedback must not contaminate learning.
    step(4500,310); assert(rtConfig->getTargetIncline()==11866);
    assert(logged("power acquisition complete"));
    assert(table.loads==1); // Housekeeping continues while waiting.
  } else if(scenario=="crossing_and_safety") {
    beginSeek(); step(2000,198); step(2400,315);
    assert(rtConfig->getTargetIncline()==11866); // Ordinary crossing still settles.
    step(2500,345); assert(rtConfig->getTargetIncline()<11866);
    assert(logged("power crossed safety limit"));
  } else if(scenario=="reduction_safety") {
    setup(310,155); table.lookupResult=9000; step(1000,310); step(2000,155);
    step(2400,140); assert(rtConfig->getTargetIncline()==9000);
    step(2500,110); assert(rtConfig->getTargetIncline()>9000);
    assert(logged("power crossed safety limit"));
  } else if(scenario=="new_target") {
    beginSeek(); step(2000,198); rtConfig->watts.setTarget(155); table.lookupResult=9000;
    step(2100,198); assert(rtConfig->getTargetIncline()==9000);
  } else if(scenario=="cadence_stop") {
    beginSeek(); step(2000,198); rtConfig->cad.setValue(0);
    step(2100,198); assert(rtConfig->watts.getTarget()==50);
    rtConfig->cad.setValue(80); table.lookupResult=12000;
    step(2200,198); assert(rtConfig->watts.getTarget()==310);
    step(2800,198); assert(rtConfig->getTargetIncline()>11866);
  } else if(scenario=="mode_change") {
    beginSeek(); step(2000,198); rtConfig->setFTMSMode(17); step(2100,198);
    rtConfig->setFTMSMode(5); step(2900,300);
    assert(rtConfig->getTargetIncline()>11866);
  } else if(scenario=="missing_feedback") {
    beginSeek(); step(2000,198);
    for(int t=2700;t<=8300;t+=700) {
      clockMs=t; rtConfig->watts.setTarget(310); // Config writes aren't samples.
      step(t,198,true,false);
      assert(rtConfig->getTargetIncline()==11866);
    }
    assert(logged("power feedback timeout"));
    step(9000,300); assert(rtConfig->getTargetIncline()>11866);
  } else if(scenario=="movement_timeout") {
    beginSeek();
    for(int t=1700;t<=11000;t+=100) step(t,198,false,false);
    assert(logged("movement timeout"));
    assert(rtConfig->getTargetIncline()==11866);
  } else if(scenario=="no_table") {
    setup(); rtConfig->setHomed(false); step(1000,196);
    float commanded=rtConfig->getTargetIncline(); assert(commanded>10083);
    step(1700,196); step(2400,196); assert(rtConfig->getTargetIncline()==commanded);
    step(4200,260); assert(rtConfig->getTargetIncline()>commanded);
  } else if(scenario=="small_error") {
    setup(300,310); rtConfig->setHomed(false); step(1000,300);
    int previous=rtConfig->getTargetIncline(); assert(previous>10083);
    for(int t=1700;t<7000;t+=700) {
      step(t,300); assert(rtConfig->getTargetIncline()>previous);
      previous=rtConfig->getTargetIncline();
    }
    assert(!logged("feedback wait:"));
  } else if(scenario=="clamped_move") {
    setup(); rtConfig->setMaxStep(11000); step(1000,196); step(2000,198);
    step(4500,300); assert(logged("power acquisition complete"));
  } else if(scenario=="clock_wrap") {
    setup(); clockMs=UINT32_MAX-1500; controller.computeErg();
    step(UINT32_MAX-1000,198); step(0,198); step(1498,300);
    assert(!logged("power acquisition complete"));
    step(1499,300); assert(logged("power acquisition complete"));
  } else if(scenario=="trusted_seek" || scenario=="trusted_seek_late_uptime") {
    uint32_t offset=scenario=="trusted_seek"?0:0x80000000u;
    clockMs=offset+100; ergTimer=clockMs;
    setup(155,155); table.lookupResult=motor.current;
    for(int row:{3,7}) for(int col:{5,11}) {
      table.ptData.tableRow[row].tableEntry[col].targetPosition=1000+col*10;
      table.ptData.tableRow[row].tableEntry[col].readings=2;
    }
    for(int t=1000;t<=22000;t+=700) step(offset+t,155);
    assert(logged("now trusted"));
    table.lookupResult=11866; rtConfig->watts.setTarget(310); step(offset+23000,155);
    assert(controller.isTableSeeking());
    for(int t=23700;t<=27900;t+=700) step(offset+t,310);
    assert(!controller.isTableSeeking());
    assert(logged("power stabilized inside prediction window"));
    step(offset+28600,300); assert(rtConfig->getTargetIncline()>11866);
    assert(!logged("feedback wait:"));
  } else if(scenario.find("simulation_")==0) {
    setup(); rtConfig->setHomed(true);
    if(scenario.find("missing")!=std::string::npos) table.lookupResult=RETURN_ERROR;
    int delayMs=std::stoi(scenario.substr(scenario.find_last_of('_')+1));
    std::deque<double> delayed(delayMs/10+1,196.0);
    double position=10083, reported=196, peak=0;
    for(int elapsed=0;elapsed<=60000;elapsed+=10) {
      clockMs=1000+elapsed;
      int cad=94+std::min(9,elapsed/800);
      rtConfig->cad.setValue(cad);
      motor.target=static_cast<int32_t>(rtConfig->getTargetIncline());
      position+=std::max(-35.0,std::min(35.0,motor.target-position));
      motor.current=static_cast<int32_t>(position); motor.stepperIsRunning=motor.current!=motor.target;
      double actual=(196+(position-10083)*0.05)*cad/94.0;
      delayed.push_back(actual); double old=delayed.front(); delayed.pop_front();
      reported+=(old-reported)*0.01; // Transport delay plus 1 s sensor smoothing.
      if(elapsed%1000==0) { rtConfig->watts.setValue(static_cast<int>(std::round(reported))); if(elapsed>5000) peak=std::max(peak,reported); }
      controller.runERG();
    }
    printf("%s: peak %.1f W, final %.1f W\n",scenario.c_str(),peak,reported);
    assert(peak<350); assert(std::abs(reported-310)<10);
  } else { return 2; }
}
'''


class TestErgFeedback(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("g++")
        if not compiler:
            raise RuntimeError("g++ is required for the ERG integration regression")
        cls.temp = tempfile.TemporaryDirectory()
        folder = Path(cls.temp.name)
        (folder / "Arduino.h").write_text(ARDUINO, encoding="utf-8")
        source = (ROOT / "src/ERG_Mode.cpp").read_text(encoding="utf-8")
        for header in ['"SS2KLog.h"', '"Main.h"', '"Power_Table.h"', '<LittleFS.h>']:
            source = source.replace('#include ' + header, '')
        cpp = folder / "erg.cpp"
        cpp.write_text(HARNESS.replace("/* PRODUCTION_ERG */", source), encoding="utf-8")
        cls.exe = folder / "erg.exe"
        subprocess.run([compiler, "-std=c++17", "-DPLATFORMIO_ENV_NATIVE", "-I" + str(folder),
                        "-I" + str(ROOT / "include"), "-I" + str(ROOT / "lib/SS2K/include"),
                        "-I" + str(ROOT / "lib/ArduinoCompat/include"),
                        str(cpp), "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_production_erg_feedback_wait(self):
        for scenario in ["ride_handoff", "crossing_and_safety", "reduction_safety", "new_target", "cadence_stop",
                         "mode_change", "missing_feedback", "movement_timeout", "no_table", "small_error",
                         "clamped_move", "clock_wrap", "trusted_seek", "trusted_seek_late_uptime", "simulation_1000", "simulation_2000", "simulation_3000",
                         "simulation_missing_2000"]:
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.exe), scenario], check=True)


if __name__ == "__main__":
    unittest.main()
