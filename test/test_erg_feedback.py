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
#include "ByteUtils.h"
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
  uint32_t positionEpoch=0;
  PTHelpers helpers;
  bool surface=false, sloped=false, failHighCadence=false;
  bool saveFlag=false, _hasBeenLoadedThisSession=true;
  bool saveSucceeds=true;
  int saves=0;
  int lookupResult=11866, cadenceSlope=0, samples=0, loads=0;
  bool lookupErgSlope(int, int, double&, PowerTableSlopeStatus::Value* status) {
    *status=PowerTableSlopeStatus::InsufficientRows;return false;
  }
  int lookup(int watts,int cadence) {
    if(failHighCadence && cadence>=100) return RETURN_ERROR;
    return surface ? helpers.lookup(watts,cadence,ptData) : sloped ? lookupResult+(watts-155)*12+(cadence-94)*cadenceSlope : lookupResult;
  }
  bool hasErgSeekSupport() { return !surface || helpers.hasErgSeekSupport(ptData); }
  int lookupWatts(int,int) { return 155; }
  void processPowerValue(PowerBuffer&,int,const Measurement&,bool allowed=true) { if (allowed) ++samples; }
  void setStepperMinMax() {}
  bool _save() { ++saves; assert(spinBLEServer.spinDownFlag!=1); return saveSucceeds; }
  void _manageSaveState(bool=false) { ++loads; _hasBeenLoadedThisSession=true; }
  void reset() {}
} table;
PowerTable* powerTable=&table;
void writeRow(int row, int position) {
  std::string rxValue(3+2*POWERTABLE_WATT_SIZE, '\0');
  const uint8_t cc_write=2, cc_success=128;
  rxValue[0]=cc_write; rxValue[2]=row;
  auto* pData=reinterpret_cast<uint8_t*>(&rxValue[0]);
  for(int i=0;i<POWERTABLE_WATT_SIZE;++i) put_le16s(pData+3+2*i,position);
  uint8_t returnValue[3]{};
  /* PRODUCTION_UPLOAD */
}
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
  if(scenario=="upload_home" || scenario=="upload_save_failure" || scenario=="upload_full_home") {
    setup(); rtConfig->setFTMSMode(17);
    if(scenario=="upload_full_home") spinBLEServer.spinDownFlag=2;
    writeRow(0,1234);
    assert(spinBLEServer.spinDownFlag==(scenario=="upload_full_home"?2:1));
    assert(table.saves==0 && table.ptData.tableRow[0].tableEntry[0].targetPosition==1234);
    // Startup homing retains the table while subsequent writes continue.
    rtConfig->setHomed(false);
    clockMs=9000; writeRow(1,-123);
    spinBLEServer.spinDownFlag=0;
    rtConfig->setHomed(true);
    step(10000,196);
    assert(table.saves==0 && table.ptData.tableRow[0].tableEntry[0].targetPosition==1234);
    writeRow(2,INT16_MIN); // No second homing request for the same transfer.
    writeRow(POWERTABLE_CAD_SIZE,999); // Invalid rows do not disturb the transfer.
    assert(spinBLEServer.spinDownFlag==0);
    step(20000,196); assert(table.saves==0);
    if(scenario=="upload_save_failure") table.saveSucceeds=false;
    step(21000,196);
    assert(table.saves==1);
    assert(table.ptData.tableRow[0].tableEntry[0].targetPosition==1234);
    assert(table.ptData.tableRow[1].tableEntry[0].targetPosition==-123);
    assert(table.ptData.tableRow[1].tableEntry[0].readings==MINIMUM_RELIABLE_POSITIONS+1);
    assert(table.ptData.tableRow[2].tableEntry[0].readings==0);
    assert(table.positionEpoch==3 && spinBLEServer.spinDownFlag==0);
    if(!table.saveSucceeds) {
      assert(table.saveFlag && spinBLEServer.spinDownFlag==0);
      table.saveSucceeds=true;
      step(22000,196); step(32000,196);
      assert(table.saves==1 && spinBLEServer.spinDownFlag==0);
      step(33000,196);
      assert(table.saves==2 && !table.saveFlag && spinBLEServer.spinDownFlag==0);
    } else {
      assert(!table.saveFlag);
      step(40000,196);
      assert(table.saves==1 && spinBLEServer.spinDownFlag==0);
    }
    writeRow(3,321); assert(spinBLEServer.spinDownFlag==1); // New upload homes again.
  } else if(scenario=="ride_handoff") {
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
  } else if(scenario=="worsening_reduction" || scenario=="ordinary_reduction") {
    // The 924 ride had a 374 -> 330 W retreat, followed by a higher power
    // report after the motor settled. That report should release the wait;
    // ordinary variation and pre-settlement reports should not.
    setup(374,330);table.sloped=true;table.lookupResult=motor.current;
    step(1000,374);const int firstRetreat=rtConfig->getTargetIncline();
    assert(logged("feedback wait:"));
    if(scenario=="worsening_reduction") {
      step(1800,430,false);assert(rtConfig->getTargetIncline()==firstRetreat);
      step(2500,430);assert(rtConfig->getTargetIncline()==firstRetreat);
      step(3200,438);assert(rtConfig->getTargetIncline()<firstRetreat);
      assert(logged("power rose after reduction"));
      const int secondRetreat=rtConfig->getTargetIncline();
      step(3900,450); // The second move has just settled.
      step(4600,490); // Delayed watts are still rising; do not stack a third move.
      assert(rtConfig->getTargetIncline()==secondRetreat);
      int earlyReleases=0;
      for(const auto& entry:logs)if(entry.find("power rose after reduction")!=std::string::npos)++earlyReleases;
      assert(earlyReleases==1);
    } else {
      step(1800,374);step(2500,389);step(3200,370);
      assert(rtConfig->getTargetIncline()==firstRetreat);
      assert(!logged("power rose after reduction"));
    }
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
    assert(rtConfig->getTargetIncline()>=11866);
    step(3900,300); assert(rtConfig->getTargetIncline()>11866); // Held residual still corrects after the approach slows.
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
    int previous=rtConfig->getTargetIncline();assert(previous>10083);
    for(int t:{1700,2400,3100}) {
      step(t,196);const int current=rtConfig->getTargetIncline();assert(current>previous);
      previous=current;
    }
    assert(!logged("feedback wait:"));
    step(4200,260); // Rising power can make the trend guard hold a cycle.
    step(5500,260);assert(rtConfig->getTargetIncline()>previous);
  } else if(scenario=="pid_no_wait_decrease") {
    setup(350,250);rtConfig->setHomed(false);step(1000,350);
    int previous=rtConfig->getTargetIncline();assert(previous<10083);
    for(int t:{1700,2400,3100}) {
      step(t,350);const int current=rtConfig->getTargetIncline();assert(current<previous);
      previous=current;
    }
    assert(!logged("feedback wait:"));
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
    setup(); clockMs=UINT32_MAX-1500; rtConfig->watts.setValue(196); controller.computeErg();
    step(UINT32_MAX-1000,198); step(0,198); step(1498,300);
    assert(!logged("power acquisition complete"));
    step(1499,300); assert(logged("power acquisition complete"));
  } else if(scenario=="trusted_seek" || scenario=="trusted_seek_late_uptime") {
    uint32_t offset=scenario=="trusted_seek"?0:0x80000000u;
    clockMs=offset+100; ergTimer=clockMs;
    setup(155,155); table.lookupResult=motor.current; table.sloped=true;
    for(int row:{3,7}) for(int col:{5,11}) {
      table.ptData.tableRow[row].tableEntry[col].targetPosition=1000+col*10;
      table.ptData.tableRow[row].tableEntry[col].readings=2;
    }
    for(int t=1000;t<=22000;t+=700) step(offset+t,155);
    assert(logged("now trusted"));
    table.lookupResult=11866-(310-155)*12; rtConfig->watts.setTarget(310); step(offset+23000,155);
    assert(controller.isTableSeeking());
    for(int t=23700;t<=27900;t+=700) step(offset+t,310);
    assert(!controller.isTableSeeking());
    assert(logged("power stabilized inside prediction window"));
    step(offset+28600,300); assert(rtConfig->getTargetIncline()>11866);
    assert(!logged("feedback wait:"));
  } else if(scenario=="sparse_seventy" || scenario=="extrapolated_cadence") {
    setup(200,200); table.surface=true;
    const int cadence=scenario=="sparse_seventy"?70:120;
    rtConfig->cad.setValue(cadence);
    for(int cad:{60,100}) for(int watts:{90,180,270}) {
      auto& entry=table.ptData.tableRow[(cad-MINIMUM_TABLE_CAD)/5].tableEntry[watts/30];
      entry.targetPosition=std::lround((10000+(watts*90.0/cad-100)*20)/TABLE_DIVISOR); entry.readings=3;
    }
    motor.current=motor.target=table.lookup(200,cadence); rtConfig->setTargetIncline(motor.current);
    for(int t=1000;t<=26000;t+=1000) step(t,200);
    assert(logged("now trusted"));
    rtConfig->watts.setTarget(350); step(27000,200);
    assert(controller.isTableSeeking());
    assert(rtConfig->getTargetIncline()==table.lookup(350,cadence));
    assert(logged("(extrapolated)"));
  } else if(scenario=="trust_transients" || scenario=="stale_seek" || scenario=="seek_deadline") {
    setup(155,155); table.sloped=true; table.lookupResult=motor.current;
    for(int t=1000;t<=26000;t+=1000) step(t,155);
    assert(logged("now trusted"));
    if(scenario=="trust_transients") {
      // Wrong instantaneous position/power alignment while moving must not
      // revoke trust. The same mismatch after settling must eventually do so.
      for(int t=27000;t<=51000;t+=1000) {
        motor.current+=30; motor.target=motor.current;
        rtConfig->setTargetIncline(motor.current); step(t,155);
      }
      assert(!logged("now untrusted"));
      for(int t=52000;t<=77000;t+=1000) step(t,155);
      assert(logged("now untrusted"));
    } else if(scenario=="stale_seek") {
      rtConfig->watts.setTarget(310); step(27000,155);
      assert(controller.isTableSeeking());
      step(29000,155,true,false);
      assert(!controller.isTableSeeking()); assert(logged("power feedback became stale"));
      const int position=rtConfig->getTargetIncline();
      rtConfig->cad.setValue(70); rtConfig->watts.setTarget(450);
      step(31000,155,true,false); assert(rtConfig->getTargetIncline()==position);
    } else {
      rtConfig->watts.setTarget(310); step(27000,155);
      for(int t=28000;t<=38000;t+=1000) {
        // Changing the lookup result emulates moving table/cadence targets.
        table.lookupResult+=20; rtConfig->cad.setValue(t%2000?94:95); step(t,310);
      }
      assert(logged("overall seek timeout"));
    }
  } else if(scenario=="mode_entry") {
    setup(30,75); motor.current=6500; rtConfig->setTargetIncline(7);
    controller.prepareMode(); assert(rtConfig->getTargetIncline()==6500);
    rtConfig->setTargetIncline(8000); controller.prepareMode(); assert(rtConfig->getTargetIncline()==8000);
    rtConfig->setFTMSMode(17); controller.prepareMode(); rtConfig->setTargetIncline(100);
    motor.current=12000; rtConfig->setFTMSMode(5); controller.prepareMode();
    assert(rtConfig->getTargetIncline()==12000);
  } else if(scenario=="stale_partial_motion") {
    setup(155,155); table.sloped=true; table.lookupResult=motor.current;
    for(int t=1000;t<=26000;t+=1000)step(t,155);
    rtConfig->watts.setTarget(310); step(27000,155);
    const int commanded=rtConfig->getTargetIncline(); motor.current=11000;
    step(30100,155,false,false); assert(!controller.isTableSeeking());
    step(30800,155,false,true); assert(rtConfig->getTargetIncline()==commanded);
    assert(!controller.collectionAllowed()); // Large move remains blocked near arrival.
    step(31500,155); step(32500,196); step(33500,280);
    assert(rtConfig->getTargetIncline()==commanded);
    step(34500,310); assert(rtConfig->getTargetIncline()==commanded);
    assert(logged("power acquisition complete"));
  } else if(scenario=="small_seek_collection" || scenario=="growing_seek_collection") {
    setup(155,155); table.sloped=true; table.lookupResult=motor.current;
    for(int t=1000;t<=26000;t+=1000)step(t,155);
    table.lookupResult=motor.current+28-(175-155)*12;
    rtConfig->watts.setTarget(175); step(27000,155);
    assert(controller.isTableSeeking()); assert(controller.collectionAllowed());
    if(scenario=="growing_seek_collection") {
      table.lookupResult+=40;rtConfig->cad.setValue(95);step(28000,170);
      assert(controller.collectionAllowed());
      table.lookupResult+=40;rtConfig->cad.setValue(96);step(29000,170);
      assert(!controller.collectionAllowed()); // Individually small retargets cannot reset the origin.
    } else {
      step(28000,168); assert(controller.collectionAllowed());
      step(30100,168,true,false); assert(controller.collectionAllowed());
      step(31200,168,true,false); assert(!controller.isTableSeeking());
      assert(controller.collectionAllowed()); // Small seek's acquisition handoff also remains eligible.
    }
  } else if(scenario=="cadence_feedback_reference") {
    setup(155,155);table.sloped=true;table.lookupResult=motor.current;
    for(int t=1000;t<=26000;t+=1000)step(t,155);
    step(27000,196);step(28000,196);
    rtConfig->cad.setValue(100);step(29000,180);step(30000,160);step(31000,155);
    assert(logged("power acquisition complete"));
    assert(!controller.isTableSeeking()); // Acquired feedback already includes the new cadence.
  } else if(scenario=="trusted_cadence_amend" || scenario=="unusable_cadence_wait" || scenario=="trusted_seek_safety") {
    setup(155,155);table.sloped=true;table.lookupResult=motor.current;table.cadenceSlope=-100;
    for(int t=1000;t<=26000;t+=1000)step(t,155);
    assert(logged("now trusted"));
    rtConfig->watts.setTarget(330);step(27000,155);
    assert(controller.isTableSeeking());
    const int original=rtConfig->getTargetIncline();
    if(scenario=="trusted_seek_safety") {
      rtConfig->cad.setValue(100);step(27800,355,false);
      assert(!controller.isTableSeeking());
      assert(logged("power exceeded high-side safety limit"));
      assert(rtConfig->getTargetIncline()<original);
      return 0;
    }
    motor.current=original-280;rtConfig->cad.setValue(100);
    if(scenario=="unusable_cadence_wait")table.failHighCadence=true;
    step(27800,162,false);
    if(scenario=="trusted_cadence_amend") {
      // A higher cadence predicts less resistance even while delayed watts
      // remain low. Amend the in-flight seek instead of adding a table move.
      assert(controller.isTableSeeking());
      assert(rtConfig->getTargetIncline()==original-600);
      assert(!logged("cadence correction opposed power error"));
      assert(!logged("Table feedback correction"));
      step(28500,250);assert(controller.isTableSeeking());
    } else {
      // An unusable cadence lookup may exit, but must acquire the pending
      // motor/power response before another table-sized correction.
      assert(!controller.isTableSeeking());
      assert(rtConfig->getTargetIncline()==original);
      assert(logged("cadence lookup is no longer usable"));
      step(28500,180,false);assert(rtConfig->getTargetIncline()==original);
      step(29500,250);step(31000,290);
      assert(rtConfig->getTargetIncline()==original);
      step(32500,330);assert(logged("power acquisition complete"));
      assert(!logged("Table feedback correction"));
    }
  } else if(scenario=="fresh_table_gate") {
    setup(165,165);table.surface=true;rtConfig->cad.setValue(80);
    auto add=[&](int cad,int watts,int position){
      auto& cell=table.ptData.tableRow[(cad-MINIMUM_TABLE_CAD)/5].tableEntry[watts/30];
      cell.targetPosition=position/TABLE_DIVISOR;cell.readings=3;
    };
    add(80,150,12000);add(80,180,13000);
    assert(!table.hasErgSeekSupport());
    motor.current=motor.target=table.lookup(165,80);rtConfig->setTargetIncline(motor.current);
    for(int t=1000;t<=26000;t+=1000)step(t,165);
    assert(!logged("now trusted"));
    rtConfig->watts.setTarget(175);step(27000,165);
    rtConfig->cad.setValue(82);step(28000,165);
    assert(!controller.isTableSeeking());
    assert(!logged("Table feedback correction"));
    add(80,210,14000);add(85,150,11500);add(85,180,12500);add(85,210,13500);
    assert(table.hasErgSeekSupport());
    rtConfig->cad.setValue(80);rtConfig->watts.setTarget(165);
    motor.current=motor.target=table.lookup(165,80);rtConfig->setTargetIncline(motor.current);
    for(int t=29000;t<=55000;t+=1000)step(t,165);
    assert(logged("now trusted"));
    rtConfig->watts.setTarget(175);step(56000,165);
    assert(controller.isTableSeeking());
  } else if(scenario=="opposed_cadence") {
    setup(200,200);table.surface=true;rtConfig->cad.setValue(90);
    for(int cad:{75,100})for(int watts:{90,180,270}) {
      auto& entry=table.ptData.tableRow[(cad-60)/5].tableEntry[watts/30];
      entry.targetPosition=std::lround((10000+(watts*90.0/cad-100)*20)/10);entry.readings=3;
    }
    motor.current=motor.target=table.lookup(200,90);rtConfig->setTargetIncline(motor.current);
    for(int t=1000;t<=26000;t+=1000)step(t,200);
    const int before=motor.current;rtConfig->cad.setValue(100);step(27000,160);
    assert(rtConfig->getTargetIncline()>before); // Never lower resistance when already 40 W below target.
  } else if(scenario.find("surface_")==0) {
    // Only two widely separated cadence rows, no samples above 270 W.
    // Both the production forward lookup and production controller run here.
    setup(200,200); table.surface=true; motor.current=motor.target=12000;
    rtConfig->setTargetIncline(12000); rtConfig->cad.setValue(90);
    for(int cad:{60,100}) for(int watts:{90,180,270}) {
      auto& entry=table.ptData.tableRow[(cad-MINIMUM_TABLE_CAD)/5].tableEntry[watts/30];
      entry.targetPosition=std::round((10000+(watts*90.0/cad-100)*20)/TABLE_DIVISOR); entry.readings=3;
    }
    int delayMs=std::stoi(scenario.substr(8));
    std::deque<double> delayed(delayMs/10+1,200.0);
    double position=12000, reported=200;
    int previousPhase=-1;
    double settled=-1, peak=0;
    int lastOutside=-1;
    int targets[]={200,350,200,245,450,200,200,200,200};
    for(int elapsed=0;elapsed<270000;elapsed+=10) {
      clockMs=1000+elapsed;
      int phase=elapsed/30000;
      if(phase!=previousPhase) {
        if(previousPhase>=1) {
          settled=lastOutside+1;
          printf("%s phase %d: settled %.1fs, peak %.1fW\n",scenario.c_str(),previousPhase,settled,peak); fflush(stdout);
          assert(settled>=0 && settled<=10.0);
          assert(peak<=std::max(targets[previousPhase],targets[previousPhase-1])+90);
        }
        previousPhase=phase; settled=-1; peak=0; lastOutside=-1;
        rtConfig->watts.setTarget(targets[phase]);
      }
      int cadence=phase==6?70:phase>=7?100:90;
      rtConfig->cad.setValue(cadence);
      motor.target=std::clamp(static_cast<int32_t>(rtConfig->getTargetIncline()),rtConfig->getMinStep(),rtConfig->getMaxStep());
      position+=std::clamp(motor.target-position,-35.0,35.0);
      motor.current=std::lround(position); motor.stepperIsRunning=motor.current!=motor.target;
      double actual=(100+(position-10000)/20)*cadence/90.0+(phase==8?80:0);
      delayed.push_back(actual); double old=delayed.front(); delayed.pop_front();
      reported+=(old-reported)*0.01;
      if(elapsed%1000==0) {
        rtConfig->watts.setValue(std::lround(reported)); peak=std::max(peak,reported);
        if(std::abs(reported-targets[phase])>10) lastOutside=(elapsed%30000)/1000;
      }
      controller.runERG();
    }
    settled=lastOutside+1;
    printf("%s disturbance: settled %.1fs, final %.1fW\n",scenario.c_str(),settled,reported); fflush(stdout);
    // An unknown plant offset arrives through the delayed meter, unlike a
    // commanded target/cadence change. Track its separate recovery limit;
    // this case does not yet meet the ten-second target-change requirement.
    assert(settled>=0 && settled<=15.0); assert(std::abs(reported-200)<10);
    assert(logged("now trusted")); assert(logged("(extrapolated)")); assert(logged("Table feedback correction"));
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
        custom = (ROOT / "src/BLE_Custom_Characteristic.cpp").read_text(encoding="utf-8")
        custom = custom.split("case BLE_powerTableData:  // 0x27", 1)[1].split("case BLE_simulatedTargetWatts:", 1)[0]
        custom = custom[custom.index("if (rxValue[0] == cc_write)"):custom.rindex("break;")]
        cpp.write_text(HARNESS.replace("/* PRODUCTION_ERG */", source).replace("/* PRODUCTION_UPLOAD */", custom), encoding="utf-8")
        cls.exe = folder / "erg.exe"
        endian = folder / "endian.o"
        subprocess.run([compiler, "-x", "c", "-I" + str(ROOT / "lib/ArduinoCompat/include"), "-c",
                        str(ROOT / "lib/ArduinoCompat/src/os/endian.c"), "-o", str(endian)], check=True)
        subprocess.run([compiler, "-std=c++17", "-DPLATFORMIO_ENV_NATIVE", "-I" + str(folder),
                        "-I" + str(ROOT / "include"), "-I" + str(ROOT / "lib/SS2K/include"),
                        "-I" + str(ROOT / "lib/ArduinoCompat/include"),
                        str(cpp), str(ROOT / "src/PowerTable_Helpers.cpp"),
                        str(endian), "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_production_erg_feedback_wait(self):
        for scenario in ["ride_handoff", "crossing_and_safety", "reduction_safety", "worsening_reduction", "ordinary_reduction",
                         "new_target", "cadence_stop",
                         "mode_change", "missing_feedback", "movement_timeout", "no_table", "pid_no_wait_decrease", "small_error",
                         "clamped_move", "clock_wrap", "trusted_seek", "trusted_seek_late_uptime", "simulation_1000", "simulation_2000", "simulation_3000",
                         "simulation_missing_2000", "trust_transients", "stale_seek", "seek_deadline", "sparse_seventy", "extrapolated_cadence",
                         "mode_entry", "stale_partial_motion", "small_seek_collection", "growing_seek_collection", "cadence_feedback_reference",
                         "trusted_cadence_amend", "unusable_cadence_wait", "trusted_seek_safety", "fresh_table_gate", "opposed_cadence"]:
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.exe), scenario], check=True)

    def test_sparse_surface_transitions_at_one_hz(self):
        for delay in (1000,2000,3000):
            with self.subTest(delay=delay):
                subprocess.run([str(self.exe), "surface_"+str(delay)], check=True)

    def test_upload_continues_during_homing(self):
        for scenario in ("upload_home", "upload_save_failure", "upload_full_home"):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.exe), scenario], check=True)


if __name__ == "__main__":
    unittest.main()
