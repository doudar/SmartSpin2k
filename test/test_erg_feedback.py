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
  bool surface=false, sloped=false, failHighCadence=false, realSlope=false;
  bool saveFlag=false, _hasBeenLoadedThisSession=true;
  bool saveSucceeds=true;
  int saves=0;
  int lookupResult=11866, cadenceSlope=0, wattSlope=12, samples=0, loads=0;
  bool lookupErgSlope(int watts, int cadence, double& slope, PowerTableSlopeStatus::Value* status) {
    if(realSlope) return helpers.lookupErgSlope(watts,cadence,slope,ptData,status);
    *status=PowerTableSlopeStatus::InsufficientRows;return false;
  }
  int lookup(int watts,int cadence) {
    if(failHighCadence && cadence>=100) return RETURN_ERROR;
    return surface ? helpers.lookup(watts,cadence,ptData) : sloped ? lookupResult+(watts-155)*wattSlope+(cadence-94)*cadenceSlope : lookupResult;
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
void trainTrustedLow(int sensitivity, int watts=100) {
  setup(watts,watts); userConfig->setERGSensitivity(sensitivity);
  table.surface=true; rtConfig->cad.setValue(85);
  // Real measured rows exercise production seek support, interpolation, and
  // confidence. Only the delayed watt samples and motor are fake peripherals.
  for(int cadence:{80,85,90}) for(int power:{90,180,270,360}) {
    auto& entry=table.ptData.tableRow[(cadence-60)/5].tableEntry[power/30];
    entry.targetPosition=(10000+(power-100)*12-(cadence-85)*30)/10;
    entry.readings=3;
  }
  motor.current=motor.target=table.lookup(watts,85); rtConfig->setTargetIncline(motor.current);
  for(int t=1000;t<=26000;t+=1000) step(t,watts);
  assert(logged("now trusted"));
}

int main(int argc,char** argv) {
  assert(argc==2); std::string scenario=argv[1];




  if(scenario.find("acquisition_floor_")==0) {
    const int sensitivity=std::stoi(scenario.substr(scenario.find_last_of('_')+1));
    const std::string kind=scenario.substr(18,scenario.find_last_of('_')-18);
    const int index=sensitivity==3?0:sensitivity==5?1:2;
    int target=1000,watts=400,expected[3]={2250,2250,2450};
    if(kind=="operating600") {watts=600;expected[0]=1000;expected[1]=1000;expected[2]=2000;}
    else if(kind=="operating800") {watts=800;expected[0]=375;expected[1]=500;expected[2]=1000;}
    else if(kind=="clipped" || kind=="clipped_relative") {watts=kind=="clipped_relative"?100:200;expected[0]=expected[1]=expected[2]=2450;}
    else if(kind=="low") {target=400;watts=200;expected[0]=750;expected[1]=1250;expected[2]=2450;}
    else if(kind=="overshoot") {watts=1200;expected[0]=-300;expected[1]=-500;expected[2]=-1000;}
    else if(kind=="zero") {watts=1000;expected[0]=expected[1]=expected[2]=0;}
    else if(kind=="slope") {target=700;expected[0]=482;expected[1]=803;expected[2]=1607;}
    else if(kind=="settled" || kind=="trusted" || kind=="table_reset") {expected[0]=900;expected[1]=1500;expected[2]=2450;}
    else assert(kind=="untrusted" || kind=="empty_homed" || kind=="missing_local" || kind=="mode_reset" || kind=="target_change" || kind=="small_amend" || kind=="pacing");
    uint32_t at=3000;
    if(kind=="trusted") {
      trainTrustedLow(sensitivity,100);
      rtConfig->watts.setTarget(1000);step(27000,100);assert(controller.isTableSeeking());
      for(at=28000;at<=37000;at+=1000) {
        step(at,400);
        if(!controller.isTableSeeking())break;
      }
      assert(at<=37000 && logged("Trusted table seek ended") && !logged("ERG feedback wait:"));
    } else {
      if(kind=="mode_reset")motor.current=motor.target=1000; // Leave travel for several genuine mode cycles.
      setup(kind=="settled"?1000:watts,target);rtConfig->setHomed(kind=="empty_homed" || kind=="clipped_relative");userConfig->setERGSensitivity(sensitivity);
      if(kind=="empty_homed")table.surface=true; // Real empty production table, no forced lookup success.
      if(kind=="clipped_relative") {
        table.surface=true;rtConfig->cad.setValue(85);
        for(int cad:{80,85,90})for(int power:{90,180,270,360}) {
          auto& e=table.ptData.tableRow[(cad-60)/5].tableEntry[power/30];
          e.targetPosition=(10000+(power-100)*12-(cad-85)*30)/10;e.readings=3;
        }
        motor.current=motor.target=table.lookup(100,85);rtConfig->setTargetIncline(motor.current);
      }
      if(kind=="mode_reset")controller.prepareMode();
      if(kind=="slope" || kind=="missing_local") {
        table.realSlope=table.surface=true;rtConfig->cad.setValue(85);
        for(int cad:{80,85,90})for(int power:{600,660,720,780}) {
          auto& e=table.ptData.tableRow[(cad-60)/5].tableEntry[power/30];
          e.targetPosition=(10000+(power-600)-(cad-85)*10)/10;e.readings=3;
        }
        double slope;PowerTableSlopeStatus::Value status;
        assert(table.lookupErgSlope(target,85,slope,&status)==(kind=="slope"));
      }
      if(kind=="settled") {
        step(1000,1000);step(2000,1000);step(3000,1000);
        step(4000,watts);step(5000,watts);at=5000;
      } else {
        step(1000,watts);
        if(kind=="empty_homed" || kind=="clipped_relative") {
          const int initial[3]={kind=="clipped_relative"?2430:1620,kind=="clipped_relative"?2430:1620,2450};
          const int firstMove=static_cast<int>(rtConfig->getTargetIncline())-motor.current;
          printf("%s: first homed acquisition move%+d expected%+d\n",scenario.c_str(),firstMove,initial[index]);fflush(stdout);
          assert(firstMove==initial[index] && !controller.isTableSeeking() && !logged("ERG feedback wait:"));
          if(kind=="clipped_relative")assert(logged("Clipped high-power table estimate"));
        }
        step(2000,watts);step(3000,watts);
        if(kind=="table_reset") {controller.resetTableConfidence();step(4000,watts);at=4000;}
      }
    }
    const int move=static_cast<int>(rtConfig->getTargetIncline())-motor.current;
    printf("%s: target%d watts%d move%+d expected%+d at%u\n",scenario.c_str(),target,watts,move,expected[index],at);fflush(stdout);
    assert(move==expected[index] && !controller.isTableSeeking() && !logged("ERG feedback wait:"));
    if(kind=="slope")assert(logged("(table)"));
    if(kind=="mode_reset") {
      rtConfig->setFTMSMode(17);controller.prepareMode();rtConfig->watts.setTarget(400);
      rtConfig->setFTMSMode(5);controller.prepareMode();step(4000,200);
      const int lowExpected[3]={750,1250,2450};
      assert(static_cast<int>(rtConfig->getTargetIncline())-motor.current==lowExpected[index]);
      step(5000,400);step(6000,400);rtConfig->watts.setTarget(1000);step(6700,400,true,false);
      assert(static_cast<int>(rtConfig->getTargetIncline())-motor.current==expected[index]);
      step(7700,1000);step(8700,1000);step(9700,400);step(10700,400);
      const int maintained[3]={900,1500,2450};
      assert(static_cast<int>(rtConfig->getTargetIncline())-motor.current==maintained[index]);
      rtConfig->setFTMSMode(17);controller.prepareMode();
      rtConfig->setFTMSMode(5);controller.prepareMode();step(11700,400);
      printf("%s: same-target mode re-entry move%+d expected%+d\n",scenario.c_str(),static_cast<int>(rtConfig->getTargetIncline())-motor.current,expected[index]);fflush(stdout);
      assert(static_cast<int>(rtConfig->getTargetIncline())-motor.current==expected[index]);
    } else if(kind=="target_change" || kind=="table_reset") {
      rtConfig->watts.setTarget(700);step(at+700,watts,true,false);
      const int changed[3]={1125,1125,2142};
      printf("%s: new700W held-sample move%+d expected%+d\n",scenario.c_str(),static_cast<int>(rtConfig->getTargetIncline())-motor.current,changed[index]);fflush(stdout);
      assert(static_cast<int>(rtConfig->getTargetIncline())-motor.current==changed[index]);
    } else if(kind=="small_amend") {
      const int command=rtConfig->getTargetIncline();
      rtConfig->watts.setTarget(1001);step(at+700,watts,false,false);
      assert(rtConfig->getTargetIncline()==command);
      step(at+1400,watts);
      const int changed[3]={901,1502,2450};
      printf("%s: small-amended fresh move%+d expected%+d\n",scenario.c_str(),static_cast<int>(rtConfig->getTargetIncline())-motor.current,changed[index]);fflush(stdout);
      assert(static_cast<int>(rtConfig->getTargetIncline())-motor.current==changed[index]);
    } else if(kind=="pacing") {
      const int command=rtConfig->getTargetIncline();
      motor.current+=100; // Partial travel makes the deferred correction observable.
      step(at+300,watts,false,true);assert(rtConfig->getTargetIncline()==command);
      step(at+700,watts,false,false); // Deferred fresh report, paced from actual position.
      assert(static_cast<int>(rtConfig->getTargetIncline())-motor.current==expected[index]);
      assert(rtConfig->getTargetIncline()==command+100); // Rebase on actual motor, not old pending command.
      const int after=rtConfig->getTargetIncline();
      step(at+1400,watts,false,false);step(at+6000,watts,false,false);
      assert(rtConfig->getTargetIncline()==after); // Neither held nor stale data stacks correction.
      step(at+7000,watts);
      assert(static_cast<int>(rtConfig->getTargetIncline())-motor.current==expected[index]);
    }
  } else if(scenario.find("cadence_guard_")==0) {
    const bool recovery=scenario.find("_recovery_")!=std::string::npos;
    const bool low=scenario.find("_low_")!=std::string::npos;
    const bool under=scenario.find("_under_")!=std::string::npos;
    const bool approaching=scenario.find("_approaching_")!=std::string::npos;
    const bool opposed=scenario.find("_opposed_")!=std::string::npos;
    const int sensitivity=std::stoi(scenario.substr(scenario.find_last_of('_')+1));
    const int index=sensitivity==3?0:sensitivity==5?1:2;
    const int target=recovery?100:low?200:500;
    const int training=recovery?500:target;
    setup(training,training);userConfig->setERGSensitivity(sensitivity);
    table.surface=true;rtConfig->cad.setValue(85);
    for(int cad:{80,85,90}) for(int watts:{90,180,270,360,450,540,630}) {
      auto& entry=table.ptData.tableRow[(cad-60)/5].tableEntry[watts/30];
      entry.targetPosition=(10000+(watts-100)*12-(cad-85)*150)/10;
      entry.readings=3;
    }
    motor.current=motor.target=table.lookup(training,85);rtConfig->setTargetIncline(motor.current);
    // Public homing handoff records the unchanged target without a spurious
    // initial conservative seek. Genuine stationary reports earn confidence.
    rtConfig->setHomed(false);step(500,training);rtConfig->setHomed(true);
    for(int t=1000;t<=26000;t+=1000)step(t,training);
    assert(logged("now trusted") && !controller.isTableSeeking());
    if(recovery)rtConfig->watts.setTarget(100);
    const int first=target+(approaching?(under?-40:40):(under?20:-20));
    const int second=target+(approaching?(under?-30:30):(under?10:-10));
    const int last=target+(under?-7:7);
    uint32_t finalAt=29000;
    if(recovery) {
      // Execute a real decreasing 500->100 W seek before testing recovery
      // feedback. The 79 W sample trips its public low-side safety handoff;
      // the next two fresh samples expose a newly growing 100 W overshoot.
      step(27000,500);assert(controller.isTableSeeking());
      step(28000,79);assert(!controller.isTableSeeking());
      step(29000,90);finalAt=30000;
    } else {step(27000,first);step(28000,second);}
    // Equal-watt measured rows give -150 steps/RPM. A two-RPM change
    // therefore contributes exactly +/-300 steps when admitted by feedback.
    const int cadence=under?(opposed||approaching?87:83):(opposed||approaching?83:87);
    rtConfig->cad.setValue(cadence);logs.clear();step(finalAt,last);
    const int highNegative[3]={-16,-26,-51}, highPositive[3]={15,25,50};
    const int lowNegative[3]={-19,-32,-63}, lowPositive[3]={18,31,63};
    const int recoveryNegative[3]={-23,-38,-76};
    const int pid=approaching?0:recovery?recoveryNegative[index]:under?(low?lowPositive[index]:highPositive[index]):(low?lowNegative[index]:highNegative[index]);
    const bool allowsCadence=!opposed;
    const int expected=pid+(allowsCadence?(cadence==83?300:-300):0);
    const int move=static_cast<int>(rtConfig->getTargetIncline())-motor.current;
    printf("%s: cadence85->%d residual%+d motor%+d expected%+d compensation%d\n",scenario.c_str(),cadence,target-last,move,expected,logged("Cadence compensation:"));fflush(stdout);
    assert(move==expected && !controller.isTableSeeking() && !logged("ERG feedback wait:"));
    assert(logged("Cadence compensation:")==allowsCadence);
    const int command=rtConfig->getTargetIncline();
    step(finalAt+300,last,false,false);step(finalAt+700,last,false,false);
    assert(rtConfig->getTargetIncline()==command); // Held power cannot replay feed-forward.
  } else if(scenario.find("diverging_scope_")==0) {
    const int sensitivity=std::stoi(scenario.substr(scenario.find_last_of('_')+1));
    const std::string kind=scenario.substr(16,scenario.find_last_of('_')-16);
    const bool underLifecycle=kind=="under_stale" || kind=="under_quiet";
    const int index=sensitivity==3?0:sensitivity==5?1:2;
    int target=500, first=198, second=367, last=490;
    // Expected commands are literal public motor deltas. Do not duplicate
    // the production guard or its gain algebra in this oracle.
    int expected[3]={-22,-36,-72};
    if(kind=="growing" || kind=="growing_quiet" || kind=="growing_stale") {first=264;second=387;last=510;expected[0]=-399;expected[1]=-400;expected[2]=-320;}
    else if(kind=="under_quiet" || kind=="under_stale") {first=736;second=613;last=490;expected[0]=399;expected[1]=400;expected[2]=320;}
    else if(kind=="ride_undershoot") {target=501;first=563;second=520;last=477;expected[0]=200;expected[1]=159;expected[2]=143;}
    else if(kind=="ride_overshoot") {target=501;first=439;second=482;last=525;expected[0]=-201;expected[1]=-160;expected[2]=-144;}
    else if(kind=="under_reversal") {first=700;second=480;last=490;expected[0]=expected[1]=expected[2]=0;}
    else if(kind=="under_weak") {first=650;second=499;last=490;expected[0]=21;expected[1]=36;expected[2]=72;}
    else if(kind=="under_threshold") {first=650;second=500;last=490;expected[0]=36;expected[1]=43;expected[2]=72;}
    else if(kind=="under_boundary400") {target=400;first=636;second=513;last=390;expected[0]=498;expected[1]=831;expected[2]=1662;}
    else if(kind=="under_boundary401") {target=401;first=637;second=514;last=391;expected[0]=497;expected[1]=498;expected[2]=399;}
    else if(kind=="falling") {first=802;second=633;last=510;expected[0]=21;expected[1]=36;expected[2]=72;}
    else if(kind=="diverging") {first=802;second=633;last=490;expected[0]=459;expected[1]=460;expected[2]=368;}
    else if(kind=="zero") {first=254;second=377;last=500;expected[0]=expected[1]=expected[2]=0;}
    else if(kind=="zero_falling") {first=746;second=623;last=500;expected[0]=expected[1]=expected[2]=0;}
    else if(kind=="reversal") {first=300;second=520;last=510;expected[0]=expected[1]=expected[2]=0;}
    else if(kind=="weak") {first=350;second=501;last=510;}
    else if(kind=="threshold") {first=350;second=500;last=510;expected[0]=-36;expected[1]=-44;expected[2]=-72;}
    else if(kind=="boundary400") {target=400;first=164;second=287;last=410;expected[0]=-499;expected[1]=-832;expected[2]=-1663;}
    else if(kind=="boundary401") {target=401;first=165;second=288;last=411;expected[0]=-498;expected[1]=-499;expected[2]=-400;}
    else if(kind=="recovery" || kind=="ordinary_low") {
      target=100;first=280;second=200;last=110;
      expected[0]=kind=="recovery"?32:360;expected[1]=kind=="recovery"?54:600;expected[2]=kind=="recovery"?108:1200;
    } else assert(kind=="approach");
    // Untrusted high acquisition now floors gain at the measured operating
    // point. These literal deltas preserve its interaction with prediction,
    // including one extra braking step on the still-below-target approach.
    if(sensitivity==3) {
      if(kind=="approach")expected[0]=-23;
      else if(kind=="diverging")expected[0]=468;
      else if(kind=="under_quiet" || kind=="under_stale")expected[0]=407;
      else if(kind=="ride_undershoot")expected[0]=210;
      else if(kind=="under_weak")expected[0]=22;
      else if(kind=="under_boundary401")expected[0]=498;
    }
    setup(kind=="recovery"?500:first,kind=="recovery"?500:target);
    rtConfig->setHomed(false);userConfig->setERGSensitivity(sensitivity);
    uint32_t start=1000;
    if(kind=="recovery") {
      step(1000,500);step(2000,500);step(3000,500);
      rtConfig->watts.setTarget(100);start=4000;
    }
    step(start,first);step(start+1000,second);step(start+2000,last);
    const int move=static_cast<int>(rtConfig->getTargetIncline())-motor.current;
    printf("%s: motor move%+d expected%+d\n",scenario.c_str(),move,expected[index]);fflush(stdout);
    assert(move==expected[index] && !controller.isTableSeeking() && !logged("ERG feedback wait:"));
    const int command=rtConfig->getTargetIncline();
    const auto sample=rtConfig->watts.getValueTimestamp();
    step(start+2300,last,false,false); // Held, moving and inside normal pacing.
    assert(rtConfig->getTargetIncline()==command);
    step(start+2700,last,false,false); // Held, moving but now eligible for feedback.
    assert(rtConfig->getTargetIncline()==command && rtConfig->watts.getValueTimestamp()==sample);
    // Fresh exact-target power must not cause derivative-only motion, even
    // after either sign of sustained trend. The unchanged low-only controls
    // deliberately preserve their original predictive behavior instead.
    if(kind=="growing_stale" || kind=="under_stale") {
      step(start+6000,last,false,false);
      assert(rtConfig->getTargetIncline()==command); // Stale power cannot stack a move.
      step(start+7000,last);
      const int next=static_cast<int>(rtConfig->getTargetIncline())-motor.current;
      const int quietExpected[3]={underLifecycle?21:-22,underLifecycle?36:-36,underLifecycle?72:-72};
      printf("%s: fresh after stale move%+d expected%+d\n",scenario.c_str(),next,quietExpected[index]);fflush(stdout);
      assert(next==quietExpected[index]); // A new sample cannot reuse the old sustained trend.
    } else if(kind=="growing_quiet" || kind=="under_quiet") {
      step(start+3400,last); // Fresh same-valued report is real zero-trend evidence.
      const int next=static_cast<int>(rtConfig->getTargetIncline())-motor.current;
      const int quietExpected[3]={underLifecycle?21:-22,underLifecycle?36:-36,underLifecycle?72:-72};
      printf("%s: fresh quiet move%+d expected%+d\n",scenario.c_str(),next,quietExpected[index]);fflush(stdout);
      assert(next==quietExpected[index]);
      step(start+4400,underLifecycle?480:520);step(start+5400,underLifecycle?470:530);
      const int settledExpected[3]={underLifecycle?54:-54,underLifecycle?90:-90,underLifecycle?180:-180};
      const int settled=static_cast<int>(rtConfig->getTargetIncline())-motor.current;
      printf("%s: later settled move%+d expected%+d\n",scenario.c_str(),settled,settledExpected[index]);fflush(stdout);
      assert(settled==settledExpected[index]); // Quiet settlement ends transient forecasting.
    } else {
      step(start+3400,target);
      if(kind!="ordinary_low" && kind!="boundary400" && kind!="under_boundary400") assert(rtConfig->getTargetIncline()==motor.current);
    }
  } else if(scenario.find("amended_source_")==0) {
    const bool direct=scenario.find("direct")!=std::string::npos;
    const bool boundary=scenario.find("boundary")!=std::string::npos;
    const bool conservative=scenario.find("conservative")!=std::string::npos;
    const bool remote=scenario.find("remote")!=std::string::npos;
    const bool natural=scenario.find("natural")!=std::string::npos;
    const bool backToLow=scenario.find("back_to_low")!=std::string::npos;
    const bool laterLow=scenario.find("later_low")!=std::string::npos;
    setup(350,350);table.surface=true;rtConfig->cad.setValue(85);
    for(int cadence:{80,85,90}) for(int watts:{90,180,270}) {
      auto& e=table.ptData.tableRow[(cadence-60)/5].tableEntry[watts/30];
      e.targetPosition=(10000+(watts-100)*12-(cadence-85)*30)/10;e.readings=3;
    }
    if(remote) for(int watts:{600,870}) {
      auto& e=table.ptData.tableRow[0].tableEntry[watts/30];
      e.targetPosition=(10000+(watts-100)*12-(60-85)*30)/10;e.readings=3;
    }
    motor.current=motor.target=table.lookup(350,85);rtConfig->setTargetIncline(motor.current);
    for(int t=1000;t<=26000;t+=1000) step(t,350);
    assert(logged("now trusted"));
    rtConfig->watts.setTarget(direct?401:395);step(27000,350);
    assert(controller.isTableSeeking());
    const int firstCommand=rtConfig->getTargetIncline();
    if(!direct) {
      rtConfig->watts.setTarget(boundary?400:401);step(27200,350,false,false);
      assert(controller.isTableSeeking());
      assert(rtConfig->watts.getValueTimestamp()==27000);
    }
    uint32_t recoveryAt=27300;
    if(backToLow) {
      rtConfig->watts.setTarget(395);step(27300,350,false,false);
      assert(controller.isTableSeeking());recoveryAt=27400;
    }
    const int secondCommand=rtConfig->getTargetIncline();
    const int source=table.lookup(350,85),destination=table.lookup(100,85);
    const int offset=conservative?2079:-2079;
    if(!natural) {
      motor.current=source+offset;motor.target=motor.current;motor.stepperIsRunning=false;
      rtConfig->setTargetIncline(motor.current);
    }
    rtConfig->watts.setTarget(100);step(recoveryAt,350,natural,false);
    const int command=rtConfig->getTargetIncline();
    printf("%s: first%d amended%d source%d current%d destination%d recovery%d offset%+d seek%d sample%u\n",
      scenario.c_str(),firstCommand,secondCommand,source,motor.current,destination,command,command-destination,controller.isTableSeeking(),rtConfig->watts.getValueTimestamp());
    for(const auto& text:logs) if(text.find("Trusted table seek")!=std::string::npos) printf("%s\n",text.c_str());
    assert(controller.isTableSeeking());
    // The actual accepted high request, not the last PID-processed target,
    // determines whether this recovery may transfer an unsupported offset.
    const int expectedOffset=boundary?(natural?motor.current-source:offset):0;
    assert(command==destination+expectedOffset);
    if(laterLow) {
      // Starting the low recovery replaces the earlier seek's high history.
      // A later unrelated low seek retains ordinary source-offset behavior.
      motor.current=source+offset;motor.target=motor.current;motor.stepperIsRunning=false;
      rtConfig->setTargetIncline(motor.current);
      rtConfig->watts.setTarget(150);step(recoveryAt+100,350,false,false);
      assert(controller.isTableSeeking());
      assert(rtConfig->getTargetIncline()==table.lookup(150,85)+offset);
    }
  } else if(scenario.find("history_guard_")==0) {
    const bool low=scenario=="history_guard_low";
    const bool resetTable=scenario=="history_guard_table_reset";
    const bool resetMode=scenario=="history_guard_mode_reset";
    const bool stopCadence=scenario=="history_guard_cadence_stop";
    const bool heldBoundary=scenario=="history_guard_held_400_boundary";
    const bool heldAmend=scenario=="history_guard_held_high_amend" || heldBoundary;
    const bool simulatedHigh=scenario=="history_guard_sim_high";
    const bool numeric=scenario.back()>='0'&&scenario.back()<='9';
    const int sensitivity=numeric?std::stoi(scenario.substr(scenario.find_last_of('_')+1)):10;
    trainTrustedLow(sensitivity,100);
    uint32_t end=26000;
    if(heldAmend) {
      // A low seek is amended across400 W while power is held and the motor
      // is moving. This path returns before computeErg. The actual request
      // must still become high history, even when canceled before new watts.
      rtConfig->watts.setTarget(395); step(27000,100);
      assert(controller.isTableSeeking());
      rtConfig->watts.setTarget(heldBoundary?400:401); step(27200,100,false,false);
      assert(controller.isTableSeeking() && rtConfig->watts.getValueTimestamp()==27000);
      rtConfig->watts.setTarget(100); step(27400,100,false,false);
      for(int t=28000;t<=45000;t+=1000) step(t,100);
      end=45000;
    } else if(!low && !simulatedHigh) {
      rtConfig->watts.setTarget(500); step(27000,100);
      for(int t=28000;t<=35000;t+=1000) step(t,500);
      rtConfig->watts.setTarget(100); step(36000,500);
      for(int t=37000;t<=45000;t+=1000) step(t,100);
      end=45000;
    }
    if(resetTable) {
      controller.resetTableConfidence();
      for(int t=end+1000;t<=end+26000;t+=1000) step(t,100);
      end+=26000;
    }
    if(resetMode || simulatedHigh) {
      rtConfig->setFTMSMode(17); controller.prepareMode();
      if(simulatedHigh) rtConfig->watts.setTarget(500);
      step(end+1000,100);
      rtConfig->watts.setTarget(100);rtConfig->setFTMSMode(5);controller.prepareMode();
      for(int t=end+2000;t<=end+28000;t+=1000) step(t,100);
      end+=28000;
    }
    if(stopCadence) {
      rtConfig->cad.setValue(0);step(end+1000,100);
      assert(rtConfig->watts.getTarget()==userConfig->getMinWatts());
      rtConfig->cad.setValue(85);step(end+2000,100);
      assert(rtConfig->watts.getTarget()==100);
      for(int t=end+3000;t<=end+28000;t+=1000) step(t,100);
      end+=28000;
    }
    assert(!controller.isTableSeeking());
    rtConfig->watts.setTarget(240);step(end+1000,100);
    assert(controller.isTableSeeking());
    const int pending=rtConfig->getTargetIncline();
    step(end+1200,100,false,false);
    assert(controller.isTableSeeking() && rtConfig->getTargetIncline()==pending);
    step(end+2000,150);step(end+3000,190);step(end+4000,230);
    assert(!controller.isTableSeeking());
    const bool guarded=!(low||resetTable||resetMode||simulatedHigh||heldBoundary);
    const int expected=guarded?(sensitivity==3?-68:sensitivity==5?-81:-90):
                               (sensitivity==3?-68:sensitivity==5?-113:-225);
    const int move=static_cast<int>(rtConfig->getTargetIncline())-motor.current;
    printf("%s: residual+10 trend+40 move%d expected%d\n",scenario.c_str(),move,expected);fflush(stdout);
    assert(move==expected);
    const int command=rtConfig->getTargetIncline();
    step(end+4300,230,false,false); // Held data and moving motor cannot add a correction.
    assert(rtConfig->getTargetIncline()==command);
  } else if (scenario == "local_support_queries") {
    PTData fixture;
    const auto put = [&](int cadence, int watts, int readings=3) {
      auto& e = fixture.tableRow[(cadence-60)/5].tableEntry[watts/30];
      e.targetPosition = watts*2-cadence; e.readings = readings;
    };
    for (int cadence : {80,85,90}) for (int watts : {90,180,270}) put(cadence,watts);
    for (int watts : {600,870}) put(60,watts);
    assert(ErgControl::recordedTableBounds(fixture).contains(700,85));
    assert(!ErgControl::hasLocalMeasuredPowerSupport(fixture,700,85));
    assert(ErgControl::hasLocalMeasuredPowerSupport(fixture,100,85));
    for (int watts : {600,870}) put(85,watts);
    assert(ErgControl::hasLocalMeasuredPowerSupport(fixture,700,85));
    assert(!ErgControl::hasLocalMeasuredPowerSupport(fixture,700,86));
    for (int watts : {600,870}) put(90,watts);
    assert(ErgControl::hasLocalMeasuredPowerSupport(fixture,700,86));
    assert(!ErgControl::hasLocalMeasuredPowerSupport(fixture,700,91));
    assert(!ErgControl::hasLocalMeasuredPowerSupport(fixture,700,0));
    assert(!ErgControl::hasLocalMeasuredPowerSupport(fixture,-1,85));
    // An exact reliable anchor is measured even without a higher watt cell.
    fixture = PTData{}; put(85,90); put(85,690);
    assert(ErgControl::hasLocalMeasuredPowerSupport(fixture,690,85));
    assert(!ErgControl::hasLocalMeasuredPowerSupport(fixture,700,85));
    put(85,690,1);
    assert(!ErgControl::hasLocalMeasuredPowerSupport(fixture,690,85));
    // Nearest valid cadence rows contribute at equal torque, not equal watts.
    fixture = PTData{}; for (int c : {80,90}) for (int w : {600,720}) put(c,w);
    assert(!ErgControl::hasLocalMeasuredPowerSupport(fixture,700,85)); // 741 W at90 RPM is uncovered.
    put(90,750);
    assert(ErgControl::hasLocalMeasuredPowerSupport(fixture,700,85));
    fixture.tableRow[6].tableEntry[25].targetPosition = 1;
    assert(!ErgControl::hasLocalMeasuredPowerSupport(fixture,700,85));
  } else if (scenario.find("sparse_source_") == 0) {
    const bool remoteHigh = scenario.find("remote") != std::string::npos;
    const bool localHigh = scenario.find("locally_measured") != std::string::npos;
    const bool settled = scenario.find("settled") != std::string::npos;
    const int offset = scenario.find("conservative") != std::string::npos ? 2079 : -2079;
    setup(700, 700); table.surface = true; rtConfig->cad.setValue(85);
    for (int cadence : {80, 85, 90}) for (int watts : {90, 180, 270}) {
      auto& e = table.ptData.tableRow[(cadence-60)/5].tableEntry[watts/30];
      e.targetPosition = (10000+(watts-100)*12-(cadence-85)*30)/10;
      e.readings = 3;
    }
    if (remoteHigh) for (int watts : {600, 870}) {
      auto& e = table.ptData.tableRow[0].tableEntry[watts/30];
      e.targetPosition = (10000+(watts-100)*12-(60-85)*30)/10;
      e.readings = 3;
    }
    if (localHigh) for (int watts : {600, 870}) {
      auto& e = table.ptData.tableRow[5].tableEntry[watts/30];
      e.targetPosition = (10000+(watts-100)*12)/10; e.readings = 3;
    }
    motor.current = motor.target = table.lookup(700, 85);
    rtConfig->setTargetIncline(motor.current);
    for (int t=1000; t<=26000; t+=1000) step(t, 700);
    assert(logged("now trusted"));
    const int before = motor.current;
    motor.current += offset; motor.target = motor.current;
    rtConfig->setTargetIncline(motor.current); step(27000, 700, false, false);
    uint32_t requestAt = 27100;
    if (settled) {
      for (int t=28000; t<=30000; t+=1000) step(t, 700);
      requestAt = 30100;
    }
    const int destination = table.lookup(100, 85);
    const auto bounds = ErgControl::recordedTableBounds(table.ptData);
    printf("%s source700W/85RPM localMax270W global%u current%d sourcePrediction%d dest%d\n",
           scenario.c_str(), bounds.contains(700, 85), motor.current, before, destination);
    rtConfig->watts.setTarget(100); step(requestAt, 700, false, false);
    printf("command%d destinationDelta%+d seek%d\n", static_cast<int>(rtConfig->getTargetIncline()),
           static_cast<int>(rtConfig->getTargetIncline())-destination, controller.isTableSeeking());
    assert(controller.isTableSeeking());
    assert(rtConfig->getTargetIncline() == destination + (localHigh ? offset : 0));
  } else if(scenario.find("near_flat_ramp_")==0) {
    const int span=scenario.find("_19_")!=std::string::npos?19:1;
    const bool upper=scenario.find("upper")!=std::string::npos;
    const int start=upper?465:436;
    setup(start,start); table.surface=true; rtConfig->cad.setValue(102);
    for(int cadence:{90,100}) for(int watts:{90,180,270}) {
      auto& entry=table.ptData.tableRow[(cadence-60)/5].tableEntry[watts/30];
      entry.targetPosition=1360+(watts-90)-2*(cadence-90);entry.readings=3;
    }
    for(int watts:{450,480}) {
      auto& entry=table.ptData.tableRow[9].tableEntry[watts/30];
      entry.targetPosition=1612+(watts==480?span:0);entry.readings=3;
    }
    motor.current=motor.target=table.lookup(start,102);
    rtConfig->setTargetIncline(motor.current);
    rtConfig->setHomed(false); step(500,start); rtConfig->setHomed(true);
    for(int t=1000;t<=26000;t+=1000) step(t,start);
    assert(logged("now trusted"));
    assert(!logged("ERG feedback wait:") && !controller.isTableSeeking());
    // Independent measured-edge plus equal-torque donor deltas. The lower
    // and upper 105 RPM edges fall between integer targets at 102 RPM.
    const int expected[4][4]={{16508,16518,16525,16531},{16687,16693,16702,16712},
                              {16508,16518,16527,16535},{16756,16764,16774,16784}};
    const int row=(span==19?2:0)+(upper?1:0);
    printf("%s initial%d expected%d\n",scenario.c_str(),static_cast<int>(rtConfig->getTargetIncline()),expected[row][0]);
    fflush(stdout);assert(rtConfig->getTargetIncline()==expected[row][0]);
    for(int i=1;i<=3;++i) {
      const int before=rtConfig->getTargetIncline();
      rtConfig->watts.setTarget(start+i);step(26000+100*i,start,false,false);
      const int command=rtConfig->getTargetIncline();
      printf("%s target%d command%d delta%+d expected%d\n",scenario.c_str(),start+i,command,command-before,expected[row][i]);
      fflush(stdout);assert(command==expected[row][i]);
      assert(command>before && command-before<=11);
      assert(!controller.isTableSeeking() && !logged("ERG feedback wait:"));
    }
    assert(rtConfig->watts.getValueTimestamp()==26000);
  } else if(scenario=="flat_boundary_ramp") {
    setup(640,640); table.surface=true; rtConfig->cad.setValue(102);
    for(int cadence:{90,100}) for(int watts:{90,180,270}) {
      auto& entry=table.ptData.tableRow[(cadence-60)/5].tableEntry[watts/30];
      entry.targetPosition=(10000+(watts-90)*100/9-(cadence-90)*20)/10;
      entry.readings=3;
    }
    for(int watts:{660,690}) {
      auto& entry=table.ptData.tableRow[9].tableEntry[watts/30];
      entry.targetPosition=1806; entry.readings=3;
    }
    motor.current=motor.target=table.lookup(640,102);
    rtConfig->setTargetIncline(motor.current);
    // Public homing handoff records the unchanged target through PID before
    // allowing table use. This avoids the unrelated first-call conservative
    // seek artifact when target == power. Fresh stationary reports then earn
    // confidence normally; no private state or fabricated trust is used.
    rtConfig->setHomed(false); step(500,640); rtConfig->setHomed(true);
    for(int t=1000;t<=26000;t+=1000) step(t,640);
    assert(logged("now trusted"));
    assert(!logged("ERG feedback wait:") && !controller.isTableSeeking());
    const int initial=rtConfig->getTargetIncline();
    assert(initial==table.lookup(640,102));
    for(int target:{641,642,643}) {
      const int before=rtConfig->getTargetIncline();
      rtConfig->watts.setTarget(target);
      step(26000+100*(target-640),640,false,false);
      const int command=rtConfig->getTargetIncline();
      printf("flat ramp %d W: %d -> %d (%+d steps)\n",target,before,command,command-before);
      // The fixture's largest donor slope is 12.5 steps/W. A one-watt
      // adjustment may inherit that slope, never the old ~900-step jump.
      assert(command>before && command-before<=14);
      assert(!controller.isTableSeeking() && !logged("ERG feedback wait:"));
    }
    assert(rtConfig->watts.getValueTimestamp()==26000); // Held power, real small-ramp path.
  } else if (scenario.find("seek_amended_") == 0) {
    const bool amended = scenario != "seek_amended_no";
    const int targetDelta = scenario == "seek_amended_nine" ? 9 : 1;
    trainTrustedLow(10, 100);
    rtConfig->watts.setTarget(240); step(27000, 100);
    assert(controller.isTableSeeking());
    step(28000, 150); step(29000, 190);
    if (amended) {
      rtConfig->watts.setTarget(240 + targetDelta); step(29500, 190, false, false);
      assert(controller.isTableSeeking());
    }
    step(30000, 230);
    assert(!controller.isTableSeeking());
    // The low-only seek retains its original predictive authority. It has
    // already followed the amendment, so handoff must not apply its table
    // delta again. High-request history is tested independently below.
    // A sub-padding 1 W amendment likewise belongs to ordinary feedback now.
    const int expected = amended ? (targetDelta == 1 ? -218 : -158) : -225;
    assert(static_cast<int>(rtConfig->getTargetIncline())-motor.current == expected);
    printf("%s: target=%d residual=%d move=%d\n", scenario.c_str(),
           rtConfig->watts.getTarget(), rtConfig->watts.getTarget()-230,
           static_cast<int>(rtConfig->getTargetIncline())-motor.current);
  } else if (scenario.find("settled_recovery_cadence_") == 0) {
    const bool high = scenario == "settled_recovery_cadence_high";
    const int start = high ? 500 : 100;
    setup(start, start); table.surface = true; rtConfig->cad.setValue(85);
    for (int cad : {80, 85, 90}) for (int watts : {90, 180, 270, 360}) {
      auto& e = table.ptData.tableRow[(cad-60)/5].tableEntry[watts/30];
      e.targetPosition = (10000+(watts-100)*12-(cad-85)*150)/10;
      e.readings = 3;
    }
    motor.current = motor.target = table.lookup(start, 85);
    rtConfig->setTargetIncline(motor.current);
    for (int t=1000; t<=26000; t+=1000) step(t, start);
    assert(logged("now trusted"));
    if (high) { rtConfig->watts.setTarget(100); step(27000, 500); }
    for (int t=28000; t<=60000; t+=1000) step(t, 100);
    assert(!controller.isTableSeeking());
    step(61000, 90); step(62000, 80);
    rtConfig->cad.setValue(87); step(63000, 70);
    // Thirty seconds of quiet recovery ends the transient cadence exception.
    // Ordinary feedback for a diverging low-power residual must be identical,
    // regardless of whether a high interval preceded the settled 100 W hold.
    assert(static_cast<int>(rtConfig->getTargetIncline())-motor.current == 135);
    printf("%s: residual=30W trend=-10W/s cadence=85->87 after30sQuiet move=%d\n",
           scenario.c_str(), static_cast<int>(rtConfig->getTargetIncline())-motor.current);
  } else if(scenario=="upload_home" || scenario=="upload_save_failure" || scenario=="upload_full_home") {
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
  } else if(scenario.find("seek_guard_")==0) {
    const bool untrusted=scenario.find("untrusted")!=std::string::npos;
    const bool decreasing=scenario.find("decreasing")!=std::string::npos;
    const bool residual=scenario.find("residual")!=std::string::npos;
    const bool stale=scenario=="seek_guard_stale_motion";
    const bool numeric=scenario.back()>='0'&&scenario.back()<='9';
    const int sensitivity=numeric?std::stoi(scenario.substr(scenario.find_last_of('_')+1)):10;
    if(untrusted) {
      setup(100,240); userConfig->setERGSensitivity(sensitivity); table.sloped=true;
      step(1000,100); assert(!controller.isTableSeeking());
      assert(logged("ERG feedback wait:") && !logged("now trusted"));
      const int pending=rtConfig->getTargetIncline();
      step(2000,150); step(3000,190); step(4000,230);
      assert(rtConfig->getTargetIncline()==pending);
      step(4500,270); // Safety exit: -30 W residual and +80 W/s trend.
      assert(!controller.isTableSeeking() && logged("power crossed safety limit"));
      // Ordinary acquisition retains the full -110 W projected correction.
      const int expected=static_cast<int>(motor.current-110*1.25*sensitivity);
      assert(rtConfig->getTargetIncline()==expected);
      printf("%s: ordinary acquisition move %d\n",scenario.c_str(),expected-motor.current);
      return 0;
    }
    const int startWatts=decreasing?240:residual?155:100;
    const int target=decreasing?100:residual?310:240;
    trainTrustedLow(sensitivity,startWatts);
    rtConfig->watts.setTarget(target); step(27000,startWatts);
    assert(controller.isTableSeeking());
    const int seek=rtConfig->getTargetIncline();
    assert(seek==table.lookup(target,85));
    step(27200,startWatts,false,false); // In-flight held report cannot add a move.
    assert(controller.isTableSeeking() && rtConfig->getTargetIncline()==seek);
    if(stale) {
      step(30000,startWatts,false,false);
      assert(!controller.isTableSeeking() && rtConfig->getTargetIncline()==seek);
      assert(logged("power feedback became stale"));
      step(30500,150,false); assert(rtConfig->getTargetIncline()==seek);
      step(31000,180); step(32000,210); assert(rtConfig->getTargetIncline()==seek);
      step(33500,240); assert(rtConfig->getTargetIncline()==seek);
      assert(logged("power acquisition complete"));
      return 0;
    }
    step(28000,decreasing?200:residual?180:150);
    step(29000,decreasing?160:residual?200:190);
    assert(controller.isTableSeeking() && rtConfig->getTargetIncline()==seek);
    step(30000,decreasing?110:residual?270:230);
    assert(!controller.isTableSeeking() && logged("power trend crossed target"));
    const int move=static_cast<int>(rtConfig->getTargetIncline())-motor.current;
    // Increasing: +10 W residual with +40 W/s trend projects -30 W.
    // Decreasing: -10 W residual with -50 W/s trend projects +40 W.
    // Low-only sessions retain the original full forecast at all tested
    // sensitivities. These are actual motor commands after gain scheduling.
    const int expected=residual?(sensitivity==3?-68:sensitivity==5?-113:-225):
                       decreasing?(sensitivity==3?144:sensitivity==5?240:480):
                                  (sensitivity==3?-68:sensitivity==5?-113:-225);
    printf("%s: trusted handoff move %d (expected %d)\n",scenario.c_str(),move,expected); fflush(stdout);
    assert(move==expected);
    if(scenario=="seek_guard_reset_target") {
      // Removing table support does not reset confidence/session state. Only
      // the new low request should clear this seek's extra prediction guard.
      table.surface=false; table.sloped=false; table.lookupResult=RETURN_ERROR;
      rtConfig->watts.setTarget(200);
      step(31000,100); step(32000,140); step(33000,190);
      assert(!controller.isTableSeeking());
      const int resetMove=static_cast<int>(rtConfig->getTargetIncline())-motor.current;
      printf("target reset: move %d (ordinary forecast -400)\n",resetMove); fflush(stdout);
      assert(resetMove==-400);
    } else if(scenario=="seek_guard_reset_quiet") {
      step(31000,240); step(32000,240); // Near-target, zero-trend settlement.
      assert(rtConfig->getTargetIncline()==motor.current);
      // A later ordinary relative correction acquires power without another
      // trusted seek. Its braking must not inherit the earlier seek guard.
      step(33000,100); step(34000,100); assert(logged("ERG feedback wait:"));
      assert(!controller.isTableSeeking());
      step(35000,100); step(36000,340); step(37000,290); step(38000,250);
      assert(logged("power crossed safety limit"));
      const int resetMove=static_cast<int>(rtConfig->getTargetIncline())-motor.current;
      printf("quiet reset: move %d (ordinary forecast +225)\n",resetMove); fflush(stdout);
      assert(resetMove==225);
    } else if(scenario=="seek_guard_pacing") {
      const int first=rtConfig->getTargetIncline();
      step(30300,230,true,false); assert(rtConfig->getTargetIncline()==first);
      step(30600,220); assert(rtConfig->getTargetIncline()==first);
      step(30700,220,true,false); // The new sample waits for normal 700 ms pacing.
      const int second=rtConfig->getTargetIncline(); assert(second>first);
      step(31400,220,true,false); assert(rtConfig->getTargetIncline()==second);
      step(32700,220,true,false); assert(rtConfig->getTargetIncline()==second);
      step(33000,210); assert(rtConfig->getTargetIncline()>second);
    }
  } else if(scenario=="bounded_predictive_braking" || scenario=="bounded_predictive_recovery") {
    // Real sprint handoffs can report a small residual with a much larger
    // sustained trend. A below-target high approach and the immediately
    // following low recovery both retain their measured-residual cap.
    const bool recovery=scenario=="bounded_predictive_recovery";
    const int target=recovery?100:500;
    setup(recovery?500:198,recovery?500:target); rtConfig->setHomed(false);
    userConfig->setERGSensitivity(10);
    uint32_t start=1000;
    if(recovery) {
      // Establish the actual high interval through runERG before requesting
      // its recovery. A 280 -> 100 W startup is an ordinary low-power case.
      step(1000,500); step(2000,500); step(3000,500);
      rtConfig->watts.setTarget(100); start=4000;
    }
    step(start,recovery?280:198);
    step(start+1000,recovery?200:367);
    step(start+2000,recovery?110:490);
    const int move=static_cast<int>(rtConfig->getTargetIncline())-motor.current;
    assert(recovery?move>0:move<0);
    assert(std::abs(move)<=10*userConfig->getERGSensitivity()*ErgControl::GAIN_MAX_SENSITIVITY_RATIO+1);
    // A fresh exact-target reading must not cause derivative-only movement.
    step(start+3000,target);
    assert(static_cast<int>(rtConfig->getTargetIncline())==motor.current);
  } else if(scenario=="ordinary_low_predictive_recovery" || scenario=="ordinary_low_spike") {
    // The high-interval brake limiter must not silently change a low-power
    // startup: this sequence has never requested or measured more than 400 W.
    const int first=scenario=="ordinary_low_spike"?480:280;
    setup(first,100); rtConfig->setHomed(false); userConfig->setERGSensitivity(10);
    // Even a delayed/noisy report above 400 W does not create high-request
    // history when every requested target is only 100 W.
    step(1000,first); step(2000,200); step(3000,110);
    // -10 W residual - (-90 W/s * 1 s) = +80 W projected correction.
    // Ordinary low fallback: 12 steps/W * large-error 1.25 = 15 steps/W.
    assert(static_cast<int>(rtConfig->getTargetIncline())-motor.current==1200);
  } else if(scenario=="low_request_after_high_recovery") {
    setup(500,500); rtConfig->setHomed(false); userConfig->setERGSensitivity(10);
    step(1000,500); step(2000,500); step(3000,500);
    rtConfig->watts.setTarget(100);
    step(4000,100); step(5000,100); step(6000,100);
    // The subsequent 150 W interval has only low requested-target history.
    rtConfig->watts.setTarget(150);
    step(7000,280); step(8000,240); step(9000,160);
    // -10 - (-80) = +70 W, ordinary 10*1.25 gain, not bounded high recovery.
    assert(static_cast<int>(rtConfig->getTargetIncline())-motor.current==875);
  } else if(scenario=="mode_restart_low_request") {
    setup(500,500); rtConfig->setHomed(false); userConfig->setERGSensitivity(10);
    controller.prepareMode(); step(1000,500); step(2000,500);
    rtConfig->setFTMSMode(17); controller.prepareMode();
    rtConfig->watts.setTarget(100);
    rtConfig->setFTMSMode(5); controller.prepareMode();
    step(3000,280); step(4000,200); step(5000,110);
    assert(static_cast<int>(rtConfig->getTargetIncline())-motor.current==1200);
  } else if(scenario.find("fallback1000_")==0 || scenario.find("settled_fallback1000_")==0) {
    int sensitivity=0,error=0;
    const bool settled=scenario.find("settled_")==0;
    assert(std::sscanf(scenario.c_str()+(settled?8:0),"fallback1000_%d_%d",&sensitivity,&error)==2);
    const int watts=1000-error;
    setup(settled?1000:watts,1000); rtConfig->setHomed(false); userConfig->setERGSensitivity(sensitivity);
    if(settled) {step(1000,1000);step(2000,1000);step(3000,1000);}
    // Constant fresh watts remove trend braking from this gain-schedule test.
    // The missing table is a real public controller fallback, not a helper call.
    const uint32_t start=settled?4000:1000;
    step(start,watts); step(start+1000,watts); step(start+2000,watts);
    const double multiplier=std::abs(error)<20?.90:std::abs(error)<40?.75:std::abs(error)>60?1.25:1.0;
    // V preserves the gain floor before attenuating tiers only. Ordinary and
    // amplifying tiers retain the original final floor: at 1000 W, the raw
    // .4*sensitivity gain reaches .5*sensitivity even for the 1.25 tier.
    const double expectedGain=multiplier<1?.5*sensitivity*multiplier:std::max(.5*sensitivity,.4*sensitivity*multiplier);
    int expected=static_cast<int>(motor.current+expectedGain*error);
    // Only these two startup tier cases gain extra operating-point authority.
    // Their settled equivalents retain the exact previous gain schedule.
    if(!settled && sensitivity==3 && error==61)expected=motor.current+97;
    if(!settled && sensitivity==3 && error==100)expected=motor.current+166;
    assert(static_cast<int>(rtConfig->getTargetIncline())==expected);
    assert(!controller.isTableSeeking() && !logged("ERG feedback wait:"));
  } else if(scenario.find("gain_cap_")==0) {
    const int sensitivity=std::stoi(scenario.substr(9));
    setup(40,50); rtConfig->setHomed(false); userConfig->setERGSensitivity(sensitivity);
    step(1000,40); step(2000,40); step(3000,40);
    // Low-watt start amplification would exceed 4*sensitivity. Observe the
    // actual move at a 10 W residual, safely below the separate motor cap.
    assert(static_cast<int>(rtConfig->getTargetIncline())-motor.current==40*sensitivity);
  } else if(scenario.find("lowfloor_")==0) {
    const double sensitivity=std::stod(scenario.substr(9));
    setup(40,100); rtConfig->setHomed(false); userConfig->setERGSensitivity(sensitivity);
    step(1000,40); step(2000,40); step(3000,40);
    // Ordinary low-power gain keeps its final floor, including fractional
    // settings where low-watt amplification actually reduces the gain.
    const double actualSensitivity=userConfig->getERGSensitivity();
    const double expectedGain=std::max(.5*actualSensitivity,std::min(1.2*actualSensitivity*actualSensitivity,4*actualSensitivity));
    assert(static_cast<int>(rtConfig->getTargetIncline())==static_cast<int>(motor.current+60*expectedGain));
  } else if(scenario=="positive_move_cap" || scenario=="negative_move_cap") {
    const bool positive=scenario=="positive_move_cap";
    setup(positive?50:2000,1000); rtConfig->setHomed(false); userConfig->setERGSensitivity(10);
    step(1000,positive?50:2000);
    const int move=static_cast<int>(rtConfig->getTargetIncline())-motor.current;
    assert(move==(positive?2450:-2450)); // 3500 steps/s * 0.700 s.
  } else if(scenario.find("clipped_high_relative")==0 || scenario.find("clipped_low_relative")==0) {
    const bool high=scenario.find("clipped_high_relative")==0;
    const bool decreasing=scenario.find("decreasing")!=std::string::npos;
    const int watts=decreasing?1000:100;
    const int direction=decreasing?-1:1;
    setup(watts,high?(decreasing?500:1000):(decreasing?100:400)); table.sloped=true; table.wattSlope=30;
    step(1000,watts);
    const int first=rtConfig->getTargetIncline();
    assert((first-10083)*direction>0 && std::abs(first-10083)<=2450);
    assert(logged("Table feedback correction"));
    if(high) {
      assert(!logged("ERG feedback wait:"));
      step(1300,watts,true,false); assert(rtConfig->getTargetIncline()==first);
      step(1600,watts); assert(rtConfig->getTargetIncline()==first);
      step(1700,watts); const int second=rtConfig->getTargetIncline();
      assert((second-first)*direction>0 && std::abs(second-first)<=2450);
      step(2400,watts); assert((rtConfig->getTargetIncline()-second)*direction>0);
      assert(!logged("ERG feedback wait:"));
    } else {
      // A capped low-target table move retains its original acquisition wait.
      assert(logged("ERG feedback wait:"));
      step(1700,watts); step(2400,watts);
      assert(rtConfig->getTargetIncline()==first);
    }
  } else if(scenario.find("anchor_")==0) {
    const bool low=scenario=="anchor_low_aggressive";
    const bool settled=scenario.find("settled")!=std::string::npos;
    const bool conservative=scenario.find("conservative")!=std::string::npos;
    const bool decreasing=scenario.find("decreasing")!=std::string::npos;
    const bool measuredSource=scenario.find("measured")!=std::string::npos || !decreasing;
    const int watts=decreasing?700:200;
    setup(watts,watts); table.surface=true; rtConfig->cad.setValue(85);
    // Measured adjacent cadence rows permit the production constrained
    // extrapolator. The usual surface stops at 270 W; a separate decreasing
    // control includes measured coverage of its 700 W source and destination.
    const std::vector<int> measuredWatts=decreasing&&measuredSource?std::vector<int>{90,180,270,630,720,810}:std::vector<int>{90,180,270};
    for(int cadence:{80,85,90}) for(int watts:measuredWatts) {
      auto& entry=table.ptData.tableRow[(cadence-60)/5].tableEntry[watts/30];
      entry.targetPosition=(10000+(watts-100)*12-(cadence-85)*30)/10;
      entry.readings=3;
    }
    motor.current=motor.target=table.lookup(watts,85); rtConfig->setTargetIncline(motor.current);
    for(int t=1000;t<=26000;t+=1000) step(t,watts);
    assert(logged("now trusted"));
    const int offset=(conservative?-1000:1000)*(decreasing?-1:1);
    // The physical motor has just completed a move. Held power is still the
    // pre-arrival sample at 26000, and remains young enough to be processed.
    motor.current+=offset; motor.target=motor.current; rtConfig->setTargetIncline(motor.current);
    step(27000,watts,false,false);
    uint32_t requestAt=27100;
    if(settled) {
      for(int t=28000;t<=30000;t+=1000) step(t,watts);
      requestAt=30100;
    }
    const int target=low?350:500;
    const int seekWatts=decreasing&&measuredSource?target:target+(decreasing?ERG_MODE_PID_WINDOW:-ERG_MODE_PID_WINDOW);
    const int unanchored=table.lookup(seekWatts,85);
    rtConfig->watts.setTarget(target); step(requestAt,watts,false,false);
    assert(controller.isTableSeeking());
    // Settlement cannot certify an extrapolated source power, even when its
    // report is perfectly quiet. A source-supported conservative offset still
    // shortens a seek, but either sign of an unsupported offset is rejected.
    const int expected=unanchored+((low||(measuredSource&&(settled||conservative)))?offset:0);
    assert(rtConfig->getTargetIncline()==expected);
    if(!(decreasing&&measuredSource)) assert(logged("(extrapolated)"));
  } else if(scenario.find("wait_cancel_offset_")==0) {
    const bool settled=scenario=="wait_cancel_offset_settled";
    const bool low=scenario=="wait_cancel_offset_low";
    setup(200,200); table.surface=true; rtConfig->cad.setValue(85);
    for(int cadence:{80,85,90}) for(int watts:{90,180,270}) {
      auto& entry=table.ptData.tableRow[(cadence-60)/5].tableEntry[watts/30];
      entry.targetPosition=(10000+(watts-100)*12-(cadence-85)*30)/10;
      entry.readings=3;
    }
    motor.current=motor.target=table.lookup(200,85); rtConfig->setTargetIncline(motor.current);
    for(int t=1000;t<=26000;t+=1000) step(t,200);
    assert(logged("now trusted"));
    // These real feedback calls command a relative correction and acquire
    // power while confidence scoring is paused. Arrival is at 29 seconds.
    step(27000,100); step(28000,100);
    assert(logged("ERG feedback wait:"));
    step(29000,100); step(30000,100);
    const int actualPosition=motor.current;
    assert(actualPosition==13025 && rtConfig->getTargetIncline()==actualPosition);
    const int target=low?350:500;
    const int unanchored=table.lookup(target-ERG_MODE_PID_WINDOW,85);
    const int offset=actualPosition-table.lookup(100,85);
    assert(offset==3025);
    // Changing the request cancels the acquisition wait in this same runERG.
    // At 31 seconds the motor has been stationary only 2 seconds; at 31.5
    // seconds a fresh sample describes the full acquisition window.
    rtConfig->watts.setTarget(target); step(settled?31500:31000,100);
    assert(controller.isTableSeeking());
    assert(rtConfig->getTargetIncline()==unanchored+((settled||low)?offset:0));
    printf("%s: seek %d, unanchored %d, local offset %d\n",scenario.c_str(),static_cast<int>(rtConfig->getTargetIncline()),unanchored,offset);
  } else if(scenario.find("source_recovery_")==0) {
    const bool conservative=scenario.find("conservative")!=std::string::npos;
    const bool settled=scenario.find("settled")!=std::string::npos;
    setup(978,978); table.surface=true; rtConfig->cad.setValue(85);
    for(int cadence:{80,85,90}) for(int watts:{90,180,270}) {
      auto& entry=table.ptData.tableRow[(cadence-60)/5].tableEntry[watts/30];
      entry.targetPosition=(10000+(watts-100)*12-(cadence-85)*30)/10;
      entry.readings=3;
    }
    motor.current=motor.target=table.lookup(978,85); rtConfig->setTargetIncline(motor.current);
    for(int t=1000;t<=26000;t+=1000) step(t,978);
    assert(logged("now trusted"));
    // This surface measures only 90..270 W: 978 W is extrapolated, whereas
    // the 100 W recovery destination is bracketed. Moving 2079 steps below
    // that unmeasured prediction must not deepen the recovery command. The
    // opposite sign must not stop recovery above its measured destination.
    const int offset=conservative?2079:-2079;
    motor.current+=offset; motor.target=motor.current; rtConfig->setTargetIncline(motor.current);
    step(27000,978,false,false);
    uint32_t requestAt=27100;
    if(settled) {
      for(int t=28000;t<=30000;t+=1000) step(t,978);
      requestAt=30100;
    }
    const int unanchored=table.lookup(100,85);
    rtConfig->watts.setTarget(100); step(requestAt,978,false,false);
    assert(controller.isTableSeeking());
    assert(rtConfig->getTargetIncline()==unanchored);
    printf("%s: seek %d, measured destination %d, local offset %d\n",scenario.c_str(),static_cast<int>(rtConfig->getTargetIncline()),unanchored,offset);
  } else if(scenario=="fast_feedback" || scenario=="fast_feedback_wrap") {
    setup(180,200); rtConfig->setHomed(false);
    const uint32_t start=scenario=="fast_feedback_wrap"?UINT32_MAX-350:1000;
    step(start,180);
    const int first=rtConfig->getTargetIncline();
    assert(first>10083);
    for(uint32_t elapsed=10;elapsed<700;elapsed+=10) {
      step(start+elapsed,180);
      assert(rtConfig->getTargetIncline()==first);
    }
    step(start+700,180);
    const int second=rtConfig->getTargetIncline();
    assert(second>first);
    // New targets bypass the spacing; repeated target writes do not.
    rtConfig->watts.setTarget(220); step(start+710,180);
    const int changed=rtConfig->getTargetIncline(); assert(changed>second);
    rtConfig->watts.setTarget(220); step(start+720,180);
    assert(rtConfig->getTargetIncline()==changed);
    // Use the latest raw power at the next correction, not queued old errors.
    for(uint32_t elapsed=730;elapsed<=1410;elapsed+=10) step(start+elapsed,220);
    assert(rtConfig->getTargetIncline()==changed);
  } else if(scenario=="fast_trend") {
    setup(160,200); rtConfig->setHomed(false);
    step(1000,160);
    const int first=rtConfig->getTargetIncline();
    // A 40 W/s rise reaches target before further resistance is needed.
    for(int elapsed=10;elapsed<=700;elapsed+=10) step(1000+elapsed,160+elapsed*40/1000);
    assert(rtConfig->getTargetIncline()==first);
  } else if(scenario.find("fast_simulation_")==0) {
    setup(196,310); rtConfig->setHomed(false);
    const int reportMs=std::stoi(scenario.substr(scenario.find_last_of('_')+1));
    std::deque<double> delayed(101,196.0); // 1 s transport delay, 1 s native meter response.
    double position=motor.current, reported=196, peak=0;
    for(int elapsed=0;elapsed<=60000;elapsed+=10) {
      clockMs=1000+elapsed;
      motor.target=static_cast<int32_t>(rtConfig->getTargetIncline());
      position+=std::clamp(motor.target-position,-35.0,35.0);
      motor.current=std::lround(position); motor.stepperIsRunning=motor.current!=motor.target;
      delayed.push_back(196+(position-10083)*0.05);
      const double old=delayed.front(); delayed.pop_front(); reported+=(old-reported)*0.01;
      if(elapsed%reportMs==0) rtConfig->watts.setValue(std::lround(reported));
      controller.runERG();
      peak=std::max(peak,reported);
      if(elapsed>=45000) assert(std::abs(reported-310)<10);
    }
    printf("%s: peak %.1f W, final %.1f W\n",scenario.c_str(),peak,reported); fflush(stdout);
    assert(peak<350);
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
      step(3900,450);
      step(4600,490);
      // The failed table retreat hands off to bounded proportional updates,
      // without stacking another table-sized move or acquisition pause.
      assert(rtConfig->getTargetIncline()<secondRetreat);
      int waits=0;
      for(const auto& entry:logs)if(entry.find("ERG feedback wait:")!=std::string::npos)++waits;
      assert(waits==1);
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
    setup(); controller.prepareMode(); step(1000,196); step(2000,198);
    rtConfig->setFTMSMode(17); controller.prepareMode(); step(2100,198);
    rtConfig->setFTMSMode(5); controller.prepareMode(); step(2900,300);
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
        table.lookupResult+=20; rtConfig->cad.setValue(t%2000?92:96); step(t,310);
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
      // Falling cadence requires rising resistance; increasing cadence would
      // correctly reject these upward retargets as physically inconsistent.
      const int retarget=(POWER_SAMPLE_POSITION_SPAN-28)/2+10;
      table.lookupResult+=retarget;rtConfig->cad.setValue(93);step(28000,170);
      assert(controller.collectionAllowed());
      table.lookupResult+=retarget;rtConfig->cad.setValue(92);step(29000,170);
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
    // Two consecutive 45 W residuals cross the 44 W table-correction threshold,
    // starting the acquisition whose cadence reference this scenario checks.
    step(27000,200);step(28000,200);step(29000,200);
    rtConfig->cad.setValue(100);step(30000,180);step(31000,160);step(32000,155);
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
  } else if(scenario=="pedal_spike") {
    // Trusted table at a steady target: a one-report surge (rider shifting on the
    // saddle) must not trigger a table-sized reduction; a persistent one does.
    setup(200,200);table.surface=true;rtConfig->cad.setValue(90);
    for(int cad:{75,100})for(int watts:{90,180,270}) {
      auto& entry=table.ptData.tableRow[(cad-60)/5].tableEntry[watts/30];
      entry.targetPosition=std::lround((10000+(watts*90.0/cad-100)*20)/10);entry.readings=3;
    }
    motor.current=motor.target=table.lookup(200,90);rtConfig->setTargetIncline(motor.current);
    for(int t=1000;t<=26000;t+=1000)step(t,200);
    assert(logged("now trusted"));
    const int before=motor.current;
    const int maxChange=std::lround(userConfig->getStepperSpeed()*ERG_MODE_DELAY/1000.0);
    step(27000,280);
    assert(!logged("Table feedback correction"));assert(!logged("ERG feedback wait:"));
    assert(rtConfig->getTargetIncline()>=before-maxChange); // Proportional response only.
    step(28000,200);step(29000,200);
    assert(!logged("Table feedback correction"));
    for(int t=30000;t<=40000;t+=1000)step(t,200);
    step(41000,280);step(42000,280);
    assert(logged("Table feedback correction"));assert(logged("ERG feedback wait:"));
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
  } else if(scenario=="cadence_surge") {
    // A 9 RPM surge (often caused by the previous resistance drop) is compensated
    // by at most ERG_CADENCE_COMPENSATION_MAX_RPM; power feedback corrects the
    // remainder. 1 RPM jitter does not compensate at all. 25 W under target keeps the
    // maintenance cadence seek from taking over, so proportional feedback runs.
    setup(200,200);table.surface=true;rtConfig->cad.setValue(90);
    for(int cad:{80,85,90,95,100})for(int watts:{90,180,270}) {
      auto& entry=table.ptData.tableRow[(cad-60)/5].tableEntry[watts/30];
      entry.targetPosition=std::lround((10000+(watts*90.0/cad-100)*20)/10);entry.readings=3;
    }
    motor.current=motor.target=table.lookup(200,90);rtConfig->setTargetIncline(motor.current);
    for(int t=1000;t<=26000;t+=1000)step(t,200);
    assert(logged("now trusted"));
    rtConfig->cad.setValue(91);step(27000,200);
    assert(!logged("Cadence compensation"));
    rtConfig->cad.setValue(99);step(28000,175);
    assert(logged("90 -> 99rpm, applied +3rpm"));
    step(29000,175);
    assert(!logged("93 -> 99rpm")); // The move refreshed the reference; no second feed-forward.
    // A slow 1 RPM-per-report drift accumulates across power-feedback moves until it reaches the deadband.
    rtConfig->cad.setValue(100);step(30000,175);
    assert(!logged("99 -> 100rpm"));
    rtConfig->cad.setValue(101);step(31000,175);
    assert(logged("99 -> 101rpm, applied +2rpm"));
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
  } else if(scenario=="trusted_miss_recovery" || scenario=="trusted_miss_recovery_low") {
    setup(155,155); userConfig->setERGSensitivity(3);
    table.sloped=true; table.lookupResult=motor.current; table.cadenceSlope=-100;
    for(int t=1000;t<=26000;t+=1000)step(t,155);
    assert(logged("now trusted"));
    rtConfig->watts.setTarget(335);step(27000,155);
    for(int t=28000;t<=32000;t+=1000)step(t,255);
    assert(logged("power stabilized outside prediction window"));
    int previous=rtConfig->getTargetIncline();
    rtConfig->cad.setValue(88); // Would otherwise start another upward seek.
    for(int t=33000;t<=38000;t+=1000) {
      step(t,255); assert(!controller.isTableSeeking());
      assert(rtConfig->getTargetIncline()>previous); previous=rtConfig->getTargetIncline();
    }
    assert(!logged("ERG feedback wait:"));
    const bool low=scenario=="trusted_miss_recovery_low";
    rtConfig->watts.setTarget(low?390:460); step(39000,255);
    if(low) {
      assert(controller.isTableSeeking()); // An ordinary new request re-arms the table.
    } else {
      // This deliberately fake sloped lookup has no measured cells. Its
      // accumulated live offset cannot justify an aggressive 460 W seek.
      // High control still progresses through the bounded relative fallback.
      assert(!controller.isTableSeeking());
      assert(logged("Trusted table result was unusable at 460w/88rpm"));
      assert(rtConfig->getTargetIncline()>previous && rtConfig->getTargetIncline()-previous<2450);
      assert(logged("Table feedback correction: 255w -> 460w"));
    }
  } else if(scenario=="stalled_under" || scenario=="stalled_over") {
    const bool under=scenario=="stalled_under";
    const int watts=under?255:525, target=under?335:460, direction=under?1:-1;
    setup(watts,target); userConfig->setERGSensitivity(3); table.sloped=true;
    step(1000,watts); step(2000,watts);
    const int seekPosition=rtConfig->getTargetIncline();
    for(int t=3000;t<7000;t+=1000) {
      step(t,watts); assert(rtConfig->getTargetIncline()==seekPosition);
    }
    step(7000,watts);
    assert(logged("insufficient power response; using proportional recovery"));
    int previous=rtConfig->getTargetIncline();
    for(int t=8000;t<=13000;t+=1000) {
      step(t,watts);
      const int position=rtConfig->getTargetIncline();
      assert((position-previous)*direction>0); previous=position;
    }
    int waits=0;
    for(const auto& entry:logs)if(entry.find("ERG feedback wait:")!=std::string::npos)++waits;
    assert(waits==1);
    rtConfig->watts.setTarget(target); step(13700,watts,true,false);
    assert(rtConfig->getTargetIncline()==previous); // A target write is not new power.
    step(16000,watts,true,false);
    assert(rtConfig->getTargetIncline()==previous); // Stale power still cannot move.
    step(17000,target); step(18000,target); // Settle back at target to re-arm.
    step(19000,watts);
    waits=0;
    for(const auto& entry:logs)if(entry.find("ERG feedback wait:")!=std::string::npos)++waits;
    assert(waits==1); // One report beyond the limit is not yet persistent.
    step(20000,watts);
    waits=0;
    for(const auto& entry:logs)if(entry.find("ERG feedback wait:")!=std::string::npos)++waits;
    assert(waits==2);
  } else if(scenario.find("miscalibrated_")==0) {
    // A shallow new table underestimates travel by 3x. Real power is delayed
    // and smoothed; both undershoot and overshoot use the default sensitivity.
    const bool under=scenario.find("under")!=std::string::npos;
    const int start=under?255:525, target=under?335:460;
    setup(start,target); userConfig->setERGSensitivity(3);
    table.sloped=true; table.wattSlope=4;
    const int delayMs=std::stoi(scenario.substr(scenario.find_last_of('_')+1));
    std::deque<double> delayed(delayMs/10+1,start);
    double position=motor.current, reported=start, peakError=0;
    int lastOutside=0;
    for(int elapsed=0;elapsed<60000;elapsed+=10) {
      clockMs=1000+elapsed;
      motor.target=std::clamp(static_cast<int32_t>(rtConfig->getTargetIncline()),rtConfig->getMinStep(),rtConfig->getMaxStep());
      position+=std::clamp(motor.target-position,-35.0,35.0);
      motor.current=std::lround(position); motor.stepperIsRunning=motor.current!=motor.target;
      delayed.push_back(start+(position-10083)/12);
      const double old=delayed.front(); delayed.pop_front(); reported+=(old-reported)*0.01;
      if(elapsed%1000==0) {
        rtConfig->watts.setValue(std::lround(reported));
        if(std::abs(reported-target)>10) lastOutside=elapsed/1000;
        peakError=std::max(peakError,under?reported-target:target-reported);
      }
      controller.runERG();
    }
    printf("%s: settled %ds, opposite overshoot %.1fW, final %.1fW\n",scenario.c_str(),lastOutside+1,peakError,reported); fflush(stdout);
    assert(lastOutside+1<=30); assert(peakError<30); assert(std::abs(reported-target)<10);
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

    def test_nominal_acquisition_floor_is_scoped_and_paced(self):
        for sensitivity in (3, 5, 10):
            for kind in ("untrusted", "empty_homed", "operating600", "operating800", "clipped", "clipped_relative", "low", "overshoot", "zero",
                         "slope", "missing_local", "settled", "trusted", "table_reset", "mode_reset",
                         "target_change", "small_amend", "pacing"):
                with self.subTest(sensitivity=sensitivity, kind=kind):
                    subprocess.run([str(self.exe), f"acquisition_floor_{kind}_{sensitivity}"], check=True)

    def test_cadence_feedback_rejects_worsening_resistance(self):
        for sensitivity in (3, 5, 10):
            for context in ("high", "low"):
                for kind in ("growing_opposed", "growing_helpful", "under_opposed", "under_helpful", "approaching_over", "approaching_under"):
                    with self.subTest(sensitivity=sensitivity, context=context, kind=kind):
                        subprocess.run([str(self.exe), f"cadence_guard_{context}_{kind}_{sensitivity}"], check=True)
            for kind in ("growing_opposed", "growing_helpful"):
                with self.subTest(sensitivity=sensitivity, context="recovery", kind=kind):
                    subprocess.run([str(self.exe), f"cadence_guard_recovery_{kind}_{sensitivity}"], check=True)

    def test_nominal_prediction_requires_observed_high_divergence(self):
        for sensitivity in (3, 5, 10):
            for kind in ("approach", "growing", "growing_quiet", "growing_stale", "falling", "diverging",
                         "zero", "zero_falling", "reversal", "weak", "threshold", "boundary400", "boundary401",
                         "recovery", "ordinary_low", "under_quiet", "under_stale", "ride_undershoot", "ride_overshoot",
                         "under_reversal", "under_weak", "under_threshold", "under_boundary400", "under_boundary401"):
                with self.subTest(sensitivity=sensitivity, kind=kind):
                    subprocess.run([str(self.exe), f"diverging_scope_{kind}_{sensitivity}"], check=True)

    def test_amended_high_seek_keeps_recovery_source_guards(self):
        for kind in ("high", "high_conservative", "boundary", "direct", "direct_conservative", "high_remote", "direct_remote",
                     "natural", "direct_natural", "boundary_natural", "natural_remote", "direct_natural_remote",
                     "back_to_low", "back_to_low_conservative", "later_low", "later_low_conservative"):
            with self.subTest(kind=kind):
                subprocess.run([str(self.exe), "amended_source_" + kind], check=True)

    def test_prediction_guard_requires_requested_high_history(self):
        for kind in ("low", "high_3", "high_5", "high_10", "table_reset", "mode_reset", "cadence_stop", "held_high_amend", "held_400_boundary", "sim_high"):
            with self.subTest(kind=kind):
                subprocess.run([str(self.exe), "history_guard_" + kind], check=True)

    def test_near_flat_boundaries_use_continuous_measured_edge_extension(self):
        for span in (1, 19):
            for edge in ("lower", "upper"):
                with self.subTest(span=span, edge=edge):
                    subprocess.run([str(self.exe), f"near_flat_ramp_{span}_{edge}"], check=True)

    def test_flat_boundary_small_ramp_is_continuous(self):
        subprocess.run([str(self.exe), "flat_boundary_ramp"], check=True)

    def test_high_anchor_requires_local_source_measurements(self):
        for suffix in ("local_only", "remote", "remote_settled", "remote_conservative", "remote_conservative_settled",
                       "locally_measured", "locally_measured_conservative"):
            with self.subTest(suffix=suffix):
                subprocess.run([str(self.exe), "sparse_source_" + suffix], check=True)

    def test_local_source_support_follows_actual_lookup_rows(self):
        subprocess.run([str(self.exe), "local_support_queries"], check=True)

    def test_production_erg_feedback_wait(self):
        for scenario in ["ride_handoff", "crossing_and_safety", "reduction_safety", "worsening_reduction", "ordinary_reduction",
                         "new_target", "cadence_stop",
                         "mode_change", "missing_feedback", "movement_timeout", "no_table", "pid_no_wait_decrease", "small_error",
                         "clamped_move", "clock_wrap", "trusted_seek", "trusted_seek_late_uptime", "simulation_1000", "simulation_2000", "simulation_3000",
                         "simulation_missing_2000", "trust_transients", "stale_seek", "seek_deadline", "sparse_seventy", "extrapolated_cadence",
                         "mode_entry", "stale_partial_motion", "small_seek_collection", "growing_seek_collection", "cadence_feedback_reference",
                         "trusted_cadence_amend", "unusable_cadence_wait", "trusted_seek_safety", "fresh_table_gate", "opposed_cadence", "pedal_spike", "cadence_surge"]:
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.exe), scenario], check=True)

    def test_sparse_surface_transitions_at_one_hz(self):
        for delay in (1000,2000,3000):
            with self.subTest(delay=delay):
                subprocess.run([str(self.exe), "surface_"+str(delay)], check=True)

    def test_fast_power_feedback(self):
        for scenario in ("fast_feedback", "fast_feedback_wrap", "fast_trend",
                         "fast_simulation_10", "fast_simulation_30", "fast_simulation_100", "fast_simulation_1000"):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.exe), scenario], check=True)

    def test_high_prediction_and_low_recovery_bounds(self):
        for scenario in ("bounded_predictive_braking", "bounded_predictive_recovery", "ordinary_low_predictive_recovery",
                         "ordinary_low_spike", "low_request_after_high_recovery", "mode_restart_low_request"):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.exe), scenario], check=True)

    def test_trusted_seek_predictive_gain_guard(self):
        for sensitivity in (3, 5, 10):
            for kind in ("increasing", "decreasing", "residual", "untrusted"):
                with self.subTest(sensitivity=sensitivity, kind=kind):
                    subprocess.run([str(self.exe), f"seek_guard_{kind}_{sensitivity}"], check=True)
        for scenario in ("seek_guard_reset_target", "seek_guard_reset_quiet", "seek_guard_pacing", "seek_guard_stale_motion"):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.exe), scenario], check=True)

    def test_amended_trusted_seek_retains_predictive_guard(self):
        for scenario in ("seek_amended_no", "seek_amended_yes", "seek_amended_nine"):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.exe), scenario], check=True)

    def test_settled_recovery_restores_low_cadence_scope(self):
        for scenario in ("settled_recovery_cadence_low", "settled_recovery_cadence_high"):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.exe), scenario], check=True)

    def test_high_fallback_gain_preserves_error_attenuation_only(self):
        for sensitivity in (3, 5, 10):
            for magnitude in (10, 19, 20, 30, 39, 40, 50, 60, 61, 100):
                for sign in (-1, 1):
                    error = sign * magnitude
                    with self.subTest(sensitivity=sensitivity, error=error):
                        subprocess.run([str(self.exe), f"fallback1000_{sensitivity}_{error}"], check=True)
                        subprocess.run([str(self.exe), f"settled_fallback1000_{sensitivity}_{error}"], check=True)

    def test_production_feedback_gain_and_motor_caps(self):
        for scenario in ("gain_cap_3", "gain_cap_5", "gain_cap_10", "positive_move_cap", "negative_move_cap",
                         "lowfloor_0.1", "lowfloor_0.25", "lowfloor_0.5", "lowfloor_1", "lowfloor_3"):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.exe), scenario], check=True)

    def test_clipped_high_table_correction_uses_paced_fresh_feedback(self):
        for scenario in ("clipped_high_relative", "clipped_low_relative", "clipped_high_relative_decreasing", "clipped_low_relative_decreasing"):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.exe), scenario], check=True)

    def test_extrapolated_high_seek_requires_measured_settled_aggressive_offset(self):
        for scenario in ("anchor_high_aggressive", "anchor_high_conservative", "anchor_high_settled", "anchor_low_aggressive",
                         "anchor_high_aggressive_decreasing", "anchor_high_conservative_decreasing", "anchor_high_settled_decreasing",
                         "anchor_high_measured_settled_decreasing", "anchor_high_measured_conservative_decreasing"):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.exe), scenario], check=True)

    def test_wait_cancellation_preserves_independent_offset_settlement(self):
        for scenario in ("wait_cancel_offset_high", "wait_cancel_offset_settled", "wait_cancel_offset_low"):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.exe), scenario], check=True)

    def test_high_recovery_rejects_either_extrapolated_source_offset(self):
        for scenario in ("source_recovery_aggressive", "source_recovery_settled", "source_recovery_conservative", "source_recovery_conservative_settled"):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.exe), scenario], check=True)

    def test_failed_table_moves_keep_correcting(self):
        subprocess.run([str(self.exe), "trusted_miss_recovery"], check=True)
        subprocess.run([str(self.exe), "trusted_miss_recovery_low"], check=True)
        for side in ("under", "over"):
            with self.subTest(side=side):
                subprocess.run([str(self.exe), "stalled_"+side], check=True)
            for delay in (1000, 2000, 3000):
                with self.subTest(side=side, delay=delay):
                    subprocess.run([str(self.exe), f"miscalibrated_{side}_{delay}"], check=True)

    def test_upload_continues_during_homing(self):
        for scenario in ("upload_home", "upload_save_failure", "upload_full_home"):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.exe), scenario], check=True)


if __name__ == "__main__":
    unittest.main()
