"""Exercise both factory-reset entry points using production config persistence and deterministic peripherals."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from protocol_headers import protocol_include
from test_custom_characteristic_secrets import case_block
from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestPairedFactoryReset(unittest.TestCase):
    def test_reset_from_settings_and_boot_buttons_survives_a_storage_failure(self):
        config = (ROOT / 'src/SmartSpin_parameters.cpp').read_text()
        main = (ROOT / 'src/Main.cpp').read_text()
        channel = (ROOT / 'src/PairedSettings.cpp').read_text()
        commands = (ROOT / 'src/BLE_Custom_Characteristic.cpp').read_text()
        libraries = list((ROOT / '.pio/libdeps').glob('*/ArduinoJson/src/ArduinoJson.h'))
        if not libraries:
            self.skipTest('Install ArduinoJson to exercise the persisted settings format')
        generated = '\n'.join(function(config, symbol) for symbol in (
            'void userParameters::setDefaults(', 'void userParameters::saveToLittleFS(',
            'void userParameters::loadFromLittleFS(', 'bool userParameters::setGearTeethJSON('))
        secure = '\n'.join(function(channel, symbol) for symbol in (
            'bool resetPending(', 'bool beginReset(', 'bool finishReset('))
        production = '\n'.join(function(main, symbol) for symbol in (
            'void SS2K::applyFactoryReset(', 'void SS2K::resetIfShiftersHeld('))
        harness = r"""
#include "persisted_settings_fakes.h"
#include "CustomCharacteristicProtocol.h"
#include <mutex>
#include <vector>
#include <set>
CONFIG
bool wifiCredentials = true;
bool resetWifiSettings() { wifiCredentials = false; return true; }
struct SS2K { bool resetDefaultsFlag=false, rebootFlag=false; void applyFactoryReset(); void resetIfShiftersHeld(); } controller;
SS2K* ss2k = &controller;
userParameters settings;
userParameters* userConfig = &settings;
struct Power { bool learned = true; void reset() { learned=false; } } power;
auto powerTable = &power;
struct { int ledPin=3, shiftUpPin=1, shiftDownPin=2; } currentBoard;
constexpr int LOW=0, HIGH=1;
bool up=false, down=false;
int digitalRead(int pin) { return !(pin==1 ? up : down); }
void digitalWrite(int, int) {}
void delay(int) {}
namespace PairedSettings {
  std::recursive_mutex mutex;
  std::vector<int> peers = {1,2};
  void clear(int& peer) { peer=0; }
  bool marker=false, failCommit=false;
  std::set<int> keys{11,22};
  struct Nvs { int handle=1; bool open() { return true; } };
  constexpr int ESP_OK=0;
  int nvs_get_u8(int,const char*,uint8_t* v) { *v=marker; return marker ? 0 : -1; }
  int nvs_set_u8(int,const char*,uint8_t v) { marker=v; return 0; }
  int nvs_commit(int) { return failCommit ? -1 : 0; }
  int nvs_erase_all(int) { marker=false; keys.clear(); return 0; }
  SECURE
}
PRODUCTION
void customReset() {
  uint8_t rxValue[]={cc_write,BLE_resetToDefaults}, returnValue[2]={0xff,BLE_resetToDefaults};
  switch(rxValue[1]) {
  #define LOG_BUF_APPEND(...)
  RESET_CASE
  }
  assert(returnValue[0] == cc_success);
}
int main() {
  for (bool shifters: {false,true}) {
    wifiCredentials=true;
    settings.setDefaults(); settings.setDeviceName("bench"); settings.setPassword("saved"); settings.setShiftStep(765);
    settings.saveToLittleFS(); power.learned=true;
    PairedSettings::keys={11,22}; PairedSettings::peers={1,2};
    controller = {};
    if (shifters) {
      up=true; down=false; controller.resetIfShiftersHeld(); assert(!controller.resetDefaultsFlag);
      down=true; controller.resetIfShiftersHeld();
    } else customReset();
    assert(controller.resetDefaultsFlag);
    LittleFS.failFormat=true;
    controller.applyFactoryReset();
    assert(controller.resetDefaultsFlag && !controller.rebootFlag);
    assert(PairedSettings::resetPending());
    assert(PairedSettings::peers==std::vector<int>({0,0})); // existing sessions invalid before erasure
    controller={}; controller.resetDefaultsFlag=PairedSettings::resetPending(); // interrupted boot recovers the reset
    assert(controller.resetDefaultsFlag);
    LittleFS.failFormat=false; controller.applyFactoryReset();
    assert(controller.rebootFlag && !controller.resetDefaultsFlag && !PairedSettings::resetPending());
    assert(PairedSettings::keys.empty() && !power.learned && !wifiCredentials);
    userParameters restored; restored.loadFromLittleFS();
    assert(strcmp(restored.getDeviceName(), DEVICE_NAME)==0 && strcmp(restored.getPassword(),DEFAULT_PASSWORD)==0);
    assert(restored.getShiftStep()==DEFAULT_SHIFT_STEP);
  }
}
""".replace('\nCONFIG\n', '\n' + generated + '\n')
        harness = harness.replace('  SECURE\n','  '+secure+'\n').replace('PRODUCTION\n',production+'\n').replace('  RESET_CASE',case_block(commands,'case BLE_resetToDefaults:'))
        compiler = shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as tmp:
            cpp, exe = Path(tmp)/'reset.cpp', Path(tmp)/'reset.exe'
            cpp.write_text(harness)
            inc = [ROOT/'test',ROOT/'include',ROOT/'lib/SS2K/include',protocol_include(ROOT),ROOT/'lib/ArduinoCompat/include',libraries[0].parent]
            subprocess.run([compiler,'-std=c++17','-DPLATFORMIO_ENV_NATIVE','-DFIRMWARE_VERSION="test"',*['-I'+str(p) for p in inc],str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
