"""Exercise the production log appender with a snapshot-blocked transport."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class TestBleLogStream(unittest.TestCase):
    def test_live_logs_do_not_replay_a_backlog_after_suppressed_notifications(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / "Main.h").write_text(r'''
#pragma once
struct Runtime {
  bool enabled = true;
  bool getBleLogEnabled() const { return enabled; }
};
extern Runtime* rtConfig;
constexpr char BLE_BLELogging = 0x30;
''', encoding="utf-8")
            (work / "BLE_Custom_Characteristic.h").write_text(r'''
#pragma once
struct BLE_ss2kCustomCharacteristic {
  static void notify(char item, int tableRow = -1);
};
''', encoding="utf-8")
            source = work / "log_test.cpp"
            source.write_text(r'''
#include <cassert>
#include <string>
#include <vector>
#include "BleAppender.h"
#include "Main.h"
#include "BLE_Custom_Characteristic.h"
Runtime runtime;
Runtime* rtConfig = &runtime;
BleAppender appender;
bool snapshotActive = false;
std::vector<std::string> received;
void BLE_ss2kCustomCharacteristic::notify(char item, int) {
  assert(item == BLE_BLELogging);
  // Matches notify's early return while either transport has a settings snapshot.
  if (snapshotActive) return;
  received.push_back(appender.getLastMessage());
}
int main() {
  appender.Initialize();
  assert(appender.getLastMessage().empty());
  appender.Log("first\r\n");
  appender.Log("second");
  assert((received == std::vector<std::string>{"first", "second"}));
  appender.Log(nullptr);
  appender.Log("");
  appender.Log("\r\n");
  assert(received.size() == 2);

  // Multiple interruptions used to add permanently to the queue. Verify the
  // first resumed message is current each time, with no missing live messages.
  for (int round = 0; round < 10; ++round) {
    snapshotActive = true;
    const auto before = received.size();
    for (int i = 0; i < 1000; ++i) appender.Log("suppressed during snapshot");
    assert(received.size() == before);
    snapshotActive = false;
    for (int i = 0; i < 100; ++i) {
      const auto live = "live " + std::to_string(round) + ":" + std::to_string(i);
      appender.Log(live.c_str());
      assert(received.back() == live);
      assert(received.size() == before + i + 1);
    }
    assert(appender.getLastMessage().empty());
  }

  snapshotActive = true;
  appender.Log("stale before disable");
  runtime.enabled = false;
  snapshotActive = false;
  const auto before = received.size();
  appender.Log("disabled");
  assert(received.size() == before);
  assert(appender.getLastMessage().empty());
  runtime.enabled = true;
  appender.Log("new session");
  assert(received.back() == "new session");
  appender.Log(std::string(600, 'x').c_str());
  assert(received.back() == std::string(500, 'x'));
}
''', encoding="utf-8")
            executable = work / "log_test.exe"
            subprocess.run([
                compiler, "-std=c++17", "-Wall", "-Wextra",
                "-I" + str(work), "-I" + str(ROOT / "include"),
                str(source), str(ROOT / "src/BleAppender.cpp"),
                "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
