"""Exercise the actual PTAB persistence methods with an in-memory filesystem."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestFtmsMetadata(unittest.TestCase):
    def test_legacy_metadata_only_and_failed_writes(self):
        source = (ROOT / "src/Power_Table.cpp").read_text(encoding="utf-8")
        production = "\n".join(function(source, signature) for signature in (
            "bool PowerTable::loadFtmsCalibration(", "bool PowerTable::_manageSaveState(",
            "bool PowerTable::_save(", "void PowerTable::clearRuntime(", "bool PowerTable::reset("))
        harness = r'''
#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "FtmsCalibration.h"
using String = std::string;
#define SS2K_LOG(...) ((void)0)
#define POWERTABLE_CAD_SIZE 2
#define POWERTABLE_WATT_SIZE 3
#define TABLE_VERSION 6
#define POWER_TABLE_SAVE_INTERVAL 600000
#define POWER_TABLE_FILENAME "/ptab"
constexpr int FILE_READ = 0, FILE_WRITE = 1;
unsigned long millis() { return 1; }
struct SerialStub { template<class... T> void printf(const char*, T...) {} } Serial;
using Bytes = std::vector<uint8_t>;
int writeBudget = -1;
bool failRename = false;
struct File {
  std::shared_ptr<Bytes> data;
  size_t cursor = 0;
  explicit operator bool() const { return bool(data); }
  size_t size() const { return data ? data->size() : 0; }
  size_t read(uint8_t* out, size_t count) {
    if (!data) return 0;
    count = std::min(count, data->size() - cursor);
    memcpy(out, data->data() + cursor, count);
    cursor += count;
    return count;
  }
  size_t write(const uint8_t* bytes, size_t count) {
    if (writeBudget >= 0) count = std::min(count, size_t(writeBudget));
    if (writeBudget >= 0) writeBudget -= int(count);
    data->insert(data->end(), bytes, bytes + count);
    return count;
  }
  bool seek(size_t where) { if (where > size()) return false; cursor = where; return true; }
  void flush() {}
  void close() { data.reset(); }
};
struct Filesystem {
  std::map<String, std::shared_ptr<Bytes>> files;
  File open(const String& name, int mode) {
    if (mode == FILE_WRITE) files[name] = std::make_shared<Bytes>();
    auto found = files.find(name);
    File file;
    if (found != files.end()) file.data = found->second;
    return file;
  }
  bool exists(const String& name) { return files.count(name); }
  bool remove(const String& name) { return files.erase(name); }
  bool rename(const String& from, const String& to) {
    if (failRename || !exists(from)) return false;
    files[to] = files[from]; files.erase(from); return true;
  }
  int totalBytes() { return 100000; }
  int usedBytes() { return 0; }
} LittleFS;
struct Runtime { bool homed = false; bool getHomed() { return homed; } void setHomed(bool h) { homed = h; } } runtime;
Runtime* rtConfig = &runtime;
struct Config {
  int32_t maximum = 30000;
  int32_t minimum = 0;
  bool direction = false;
  const char* name = "Grupetto 32";
  int32_t getHMax() { return maximum; }
  void setHMax(int32_t value) { maximum = value; }
  void setHMin(int32_t value) { minimum = value; }
  int32_t getHMin() { return minimum; }
  bool getStepperDir() { return direction; }
  const char* getConnectedPowerMeter() { return name; }
} config;
Config* userConfig = &config;
struct Controller { bool resetPowerTableFlag = false; } controller;
Controller* ss2k = &controller;
struct Entry { int16_t targetPosition = INT16_MIN; int8_t readings = 0; };
struct Row { Entry tableEntry[POWERTABLE_WATT_SIZE]; };
struct Data { Row tableRow[POWERTABLE_CAD_SIZE]; };
struct Helpers {
  int getTotalReadings(Data& data) {
    int sum = 0;
    for (auto& row : data.tableRow) for (auto& cell : row.tableEntry) sum += cell.readings;
    return sum;
  }
};
struct PowerTable {
  Data ptData;
  Helpers ptHelpers;
  FtmsCalibration::Map ftmsCalibration;
  bool ftmsPositionUncertain = false;
  bool saveFlag = false;
  bool _hasBeenLoadedThisSession = false;
  uint32_t positionEpoch = 0;
  unsigned long lastSaveTime = 0;
  bool loadFtmsCalibration();
  bool _manageSaveState(bool canSkipReliabilityChecks = false, bool allowSave = true);
  bool _save();
  bool reset();
  void clearRuntime(bool allowSavedTableLoad = false);
};
/* PRODUCTION */
FtmsCalibration::Map makeMap() {
  FtmsCalibration::Map map;
  map.source = FtmsCalibration::identity(config.name, config.direction);
  map.maximum = 30000;
  map.level2[0] = 67;
  map.level2[2] = 132;
  for (int i = 0; i < FtmsCalibration::COUNT; ++i) map.position[i] = map.level2[i] * 150;
  return map;
}
int main() {
  PowerTable table;
  table.ptData.tableRow[0].tableEntry[0].targetPosition = 123;
  table.ptData.tableRow[0].tableEntry[0].readings = 4;
  assert(!table._save());
  runtime.homed = true;
  assert(table._save()); // Legacy file, with no trailer.
  Bytes legacy = *LittleFS.files[POWER_TABLE_FILENAME];
  runtime.homed = false;
  PowerTable migrated;
  assert(!migrated.loadFtmsCalibration());
  assert(!migrated._manageSaveState());
  migrated.ftmsCalibration = makeMap();
  runtime.homed = true;
  assert(migrated._manageSaveState());
  assert(migrated.ptData.tableRow[0].tableEntry[0].targetPosition == 123);
  assert(migrated._save());
  Bytes saved = *LittleFS.files[POWER_TABLE_FILENAME];
  assert(saved.size() == legacy.size() + FtmsCalibration::WIRE_SIZE);
  assert(std::equal(legacy.begin(), legacy.end(), saved.begin()));
  runtime.homed = false;
  PowerTable boot;
  assert(boot.loadFtmsCalibration()); // Only metadata may load before homing.
  assert(boot.ftmsCalibration.level2[0] == 67 && boot.ftmsCalibration.level2[2] == 132);
  // FTM2's arbitrary middle sample cannot stand in for the downward crossing.
  Bytes oldSamples = saved;
  auto trailer = oldSamples.data() + legacy.size();
  put_le32(trailer, 0x324d5446);
  put_le32(trailer + 28, FtmsCalibration::checksum(trailer, 28));
  *LittleFS.files[POWER_TABLE_FILENAME] = oldSamples;
  assert(!boot.loadFtmsCalibration());
  assert(*LittleFS.files[POWER_TABLE_FILENAME] == oldSamples);
  runtime.homed = true;
  PowerTable sampleMigration;
  sampleMigration.ftmsCalibration = makeMap();
  assert(sampleMigration._manageSaveState(false, false));
  assert(sampleMigration.ptData.tableRow[0].tableEntry[0].targetPosition == 123);
  assert(*LittleFS.files[POWER_TABLE_FILENAME] == oldSamples);
  assert(sampleMigration._save());
  assert(std::equal(legacy.begin(), legacy.end(), LittleFS.files[POWER_TABLE_FILENAME]->begin()));
  runtime.homed = false;
  // Older FTM1 coordinates also require calibration, preserving watt entries.
  Bytes oldMetadata = legacy;
  oldMetadata.resize(legacy.size() + 40, 0);
  put_le32(oldMetadata.data() + legacy.size(), 0x314d5446);
  *LittleFS.files[POWER_TABLE_FILENAME] = oldMetadata;
  assert(!boot.loadFtmsCalibration());
  runtime.homed = true;
  PowerTable oldMapMigration;
  assert(oldMapMigration._manageSaveState());
  assert(oldMapMigration.ptData.tableRow[0].tableEntry[0].targetPosition == 123);
  runtime.homed = false;
  *LittleFS.files[POWER_TABLE_FILENAME] = saved;
  assert(boot.loadFtmsCalibration());
  assert(!boot._hasBeenLoadedThisSession);
  assert(boot.ptData.tableRow[0].tableEntry[0].readings == 0);
  config.direction = true;
  assert(!boot.loadFtmsCalibration() && !boot.ftmsCalibration.valid());
  config.direction = false;
  config.name = "Another bike";
  assert(!boot.loadFtmsCalibration());
  config.name = "Grupetto 32";
  config.maximum = 29999;
  assert(!boot.loadFtmsCalibration());
  config.maximum = 30000;
  for (int truncate : {1, 20, 41}) {
    *LittleFS.files[POWER_TABLE_FILENAME] = Bytes(saved.begin(), saved.end() - truncate);
    assert(!boot.loadFtmsCalibration());
    if (truncate < FtmsCalibration::WIRE_SIZE) {
      runtime.homed = true;
      PowerTable repair;
      repair.ftmsCalibration = makeMap();
      assert(repair._manageSaveState());
      assert(repair.ptData.tableRow[0].tableEntry[0].targetPosition == 123);
      runtime.homed = false;
    }
  }
  *LittleFS.files[POWER_TABLE_FILENAME] = saved;
  LittleFS.files[POWER_TABLE_FILENAME]->back() ^= 1;
  assert(!boot.loadFtmsCalibration());
  *LittleFS.files[POWER_TABLE_FILENAME] = saved;
  runtime.homed = true;
  for (int budget : {0, 8, 15, int(saved.size()) - 1}) {
    writeBudget = budget;
    assert(!migrated._save());
    assert(*LittleFS.files[POWER_TABLE_FILENAME] == saved);
  }
  writeBudget = -1;
  failRename = true;
  assert(!migrated._save());
  assert(*LittleFS.files[POWER_TABLE_FILENAME] == saved);
  failRename = false;
  // Homing's load-only phase must not repair/overwrite an invalid saved file
  // before its one final atomic save succeeds.
  Bytes invalid = saved;
  invalid[0] = 0;
  *LittleFS.files[POWER_TABLE_FILENAME] = invalid;
  PowerTable loadOnly;
  loadOnly.ftmsCalibration = makeMap();
  assert(!loadOnly._manageSaveState(false, false));
  assert(*LittleFS.files[POWER_TABLE_FILENAME] == invalid);
  *LittleFS.files[POWER_TABLE_FILENAME] = saved;
  // Fallback learning uses a clean in-memory table and never opens/replaces the
  // saved calibration, even when its temporary table has more readings.
  runtime.homed = false;
  migrated.ftmsPositionUncertain = migrated.saveFlag = true;
  const uint32_t oldEpoch = migrated.positionEpoch;
  migrated.clearRuntime();
  assert(migrated.positionEpoch == oldEpoch + 1 && !migrated.ftmsPositionUncertain && !migrated.saveFlag);
  assert(!migrated.ftmsCalibration.valid() && migrated._hasBeenLoadedThisSession);
  assert(migrated.ptHelpers.getTotalReadings(migrated.ptData) == 0);
  assert(config.minimum == 0 && config.maximum == 30000);
  migrated.ptData.tableRow[0].tableEntry[0].targetPosition = -50;
  migrated.ptData.tableRow[0].tableEntry[0].readings = 50;
  assert(!migrated._manageSaveState() && !migrated._save());
  assert(*LittleFS.files[POWER_TABLE_FILENAME] == saved);
  // A later successful startup home discards temporary coordinates first,
  // then reloads the original saved table instead of saving the temporary one.
  migrated.clearRuntime(true);
  assert(!migrated._hasBeenLoadedThisSession);
  runtime.homed = true;
  assert(migrated._manageSaveState());
  assert(migrated.ptData.tableRow[0].tableEntry[0].targetPosition == 123);
  assert(*LittleFS.files[POWER_TABLE_FILENAME] == saved);
  PowerTable empty;
  empty.ftmsCalibration = makeMap();
  assert(empty._save()); // New calibration persists before the first watt sample.
  PowerTable emptyBoot;
  runtime.homed = false;
  assert(emptyBoot.loadFtmsCalibration());
  runtime.homed = true;
  assert(emptyBoot._manageSaveState());
  assert(emptyBoot.ptHelpers.getTotalReadings(emptyBoot.ptData) == 0);
  assert(emptyBoot.reset());
  assert(!LittleFS.exists(POWER_TABLE_FILENAME) && !emptyBoot.ftmsCalibration.valid());
  assert(!emptyBoot.loadFtmsCalibration());
}
'''
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "metadata.cpp"
            exe = Path(directory) / "metadata.exe"
            cpp.write_text(harness.replace("/* PRODUCTION */", production), encoding="utf-8")
            endian = Path(directory) / "endian.o"
            subprocess.run([compiler, "-x", "c", "-c", "-I" + str(ROOT / "lib/ArduinoCompat/include"),
                            str(ROOT / "lib/ArduinoCompat/src/os/endian.c"), "-o", str(endian)], check=True)
            subprocess.run([
                compiler, "-std=c++11", "-I" + str(ROOT / "include"),
                "-I" + str(ROOT / "lib/SS2K/include"), "-I" + str(ROOT / "lib/ArduinoCompat/include"),
                str(cpp), str(endian), "-o", str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
