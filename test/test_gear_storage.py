#
# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only
#

"""Round-trip production configuration through ArduinoJson and an in-memory file."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestGearStorage(unittest.TestCase):
    def test_tooth_configuration_round_trip(self):
        libraries = list((ROOT / ".pio/libdeps").glob("*/ArduinoJson/src/ArduinoJson.h"))
        if not libraries:
            self.skipTest("Install firmware dependencies to exercise ArduinoJson persistence")
        source = (ROOT / "src/SmartSpin_parameters.cpp").read_text(encoding="utf-8")
        production = "\n".join(function(source, signature) for signature in (
            "void userParameters::setDefaults(", "String userParameters::returnJSON(",
            "void userParameters::saveToLittleFS(", "bool userParameters::saveToLittleFSChecked(", "void userParameters::loadFromLittleFS(",
            "bool userParameters::setGearTeethJSON(",
        ))
        harness = r'''
#include <cassert>
#include <cstring>
#include <ArduinoJson.h>
#include "SmartSpin_parameters.h"
#define SS2K_LOG(...) ((void)0)
#define FILE_WRITE 1
#include <map>
#include <mutex>
#include <algorithm>
std::map<std::string,std::string> files;
std::string &fileContents=files[configFILENAME];
bool failWrite=false, failRename=false;
int writes=0;
struct File {
  std::string *data=nullptr;
  size_t position=0;
  operator bool() const { return data; }
  size_t size() const { return data ? data->size() : 0; }
  int read() { return data && position < data->size() ? static_cast<unsigned char>((*data)[position++]) : -1; }
  size_t readBytes(char* out,size_t length) {
    size_t n=0; int c; while(n<length && (c=read())>=0) out[n++]=c; return n;
  }
  size_t write(const uint8_t* bytes,size_t length) {
    ++writes; if(failWrite) length/=2; data->append(reinterpret_cast<const char*>(bytes),length); return length;
  }
  void flush() {}
  void close() {}
};
struct MemoryFS {
  void remove(const String &path) { files.erase(path.c_str()); }
  File open(const String &path,const char *mode="r") {
    auto found=files.find(path.c_str());
    if(mode[0]=='w') { files[path.c_str()].clear(); return {&files[path.c_str()]}; }
    return {found==files.end() ? nullptr : &found->second};
  }
  bool rename(const String &from,const String &to) {
    if(failRename) return false;
    files[to.c_str()]=files[from.c_str()]; files.erase(from.c_str()); return true;
  }
} LittleFS;
PRODUCTION
int main() {
  userParameters config, restored;
  config.setDefaults();
  assert(config.setGearTeethJSON("[5319,3432,5332,3419]"));
  const auto original=config.getGearProfile();
  config.saveToLittleFS();
  int written=writes;
  assert(config.saveToLittleFSChecked() && writes==written);
  const std::string old=fileContents;
  config.setPassword("private password");
  failWrite=true; assert(!config.saveToLittleFSChecked() && fileContents==old); failWrite=false;
  failRename=true; assert(!config.saveToLittleFSChecked() && fileContents==old); failRename=false;
  assert(config.saveToLittleFSChecked());
  JsonDocument publicApi; assert(!deserializeJson(publicApi,config.returnJSON(false)));
  assert(publicApi["password"].isNull() && publicApi["passwordConfigured"]==true);
  JsonDocument saved; assert(!deserializeJson(saved,fileContents));
  assert(saved["gearTeeth"].size()==4 && saved["gearRatios"].isNull());
  restored.loadFromLittleFS();
  assert(restored.getGearProfile()==original);
  JsonDocument api; assert(!deserializeJson(api,restored.returnJSON()));
  assert(api["gearTeeth"][0]==3432 && api["gearRatios"].isNull());
  assert(restored.getGearProfile().ratios[0]==1063);
  for (const char* bad : {"[5332]","[5332,5332]","[5332,5300]","[5332,-1]","[5332,1.5]","{}","[5332,65536]"}) {
    assert(!restored.setGearTeethJSON(bad));
    assert(restored.getGearProfile()==original);
  }
  assert(!restored.setGearTeethJSON("[1000,1500,2000]"));
  assert(restored.getGearProfile()==original);
  assert(config.setGearPreset(VirtualGearing::MIXED_TERRAIN_1X24));
  config.saveToLittleFS(); restored.loadFromLittleFS();
  assert(restored.getGearProfile().preset==VirtualGearing::MIXED_TERRAIN_1X24);
  assert(restored.getGearProfile().count==24 && restored.getGearProfile().ratios[7]==1680);
  assert(!deserializeJson(api,restored.returnJSON()));
  assert(api["gearPreset"]==1 && api["gearTeeth"].size()==0 && api["gearRatios"].isNull());
  assert(!restored.setGearPreset(2) && restored.getGearProfile().preset==1);
  assert(restored.setGearTeethJSON("[3434,3417]"));
  assert(restored.getGearProfile().preset==0 && restored.getGearProfile().count==2);
  assert(restored.setGearPreset(0) && restored.getGearProfile().unlimited());
  assert(config.setGearTeethJSON("[]")); config.saveToLittleFS();
  restored.loadFromLittleFS(); assert(restored.getGearProfile().unlimited());
  assert(!deserializeJson(api,restored.returnJSON()));
  assert(api["gearTeeth"].is<JsonArray>() && api["gearTeeth"].size()==0);
}
'''.replace("PRODUCTION", production)
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required")
        with tempfile.TemporaryDirectory(prefix="ss2k-storage-") as directory:
            cpp = Path(directory) / "test.cpp"
            exe = Path(directory) / "test.exe"
            cpp.write_text(harness, encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", "-DPLATFORMIO_ENV_NATIVE", '-DFIRMWARE_VERSION="test"',
                            "-I" + str(ROOT / "include"), "-I" + str(ROOT / "lib/SS2K/include"),
                            "-I" + str(ROOT / "lib/ArduinoCompat/include"), "-I" + str(libraries[0].parent),
                            str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
