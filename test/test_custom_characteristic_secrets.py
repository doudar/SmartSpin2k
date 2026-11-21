#
# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only
#

"""The custom characteristic never returns the WiFi password and reports its capabilities."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/BLE_Custom_Characteristic.cpp"


def case_block(source, label):
    """Return a switch case from its label through its terminating break."""
    start = source.index(label)
    end = source.index("\n      break;", start) + len("\n      break;")
    return source[start:end]


HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "ByteUtils.h"
#include "CustomCharacteristicProtocol.h"

#define LOG_BUF_APPEND(...)

struct String {
  std::string s;
  String(const char* c) : s(c) {}
  String(const String&) = default;
  void remove(unsigned index, unsigned count) { s.erase(index, count); }
  unsigned length() const { return s.size(); }
  const char* c_str() const { return s.c_str(); }
};
struct FakeConfig {
  std::string password = "";
  void setPassword(String value) { password = value.s; }
  const char* getPassword() { return password.c_str(); }
};
FakeConfig fakeConfig;
FakeConfig* userConfig = &fakeConfig;
struct FakeCharacteristic {
  std::vector<uint8_t> value;
  void setValue(const uint8_t* data, size_t length) { value.assign(data, data + length); }
} fakeCharacteristic;
FakeCharacteristic* pCharacteristic = &fakeCharacteristic;

std::vector<uint8_t> run(const std::string& rxValue) {
  const uint8_t* pData = reinterpret_cast<const uint8_t*>(rxValue.data());
@@PRELUDE@@
  switch (rxValue[1]) {
    @@PASSWORD@@

    @@CAPABILITIES@@

    default:
      returnValue[0] = cc_error;
      break;
  }
@@RESPONSE@@
  return returnValue;
}

std::string req(uint8_t op, uint8_t id, const std::string& value = "") {
  std::string out;
  out.push_back(static_cast<char>(op));
  out.push_back(static_cast<char>(id));
  return out + value;
}

bool same(const std::vector<uint8_t>& got, std::initializer_list<uint8_t> expected) {
  return got == std::vector<uint8_t>(expected);
}

int main() {
  userConfig->setPassword("s3cret-Value!");
  auto reply = run(req(cc_read, BLE_password));
  assert(same(reply, {0x80, 0x13}));
  const std::string text(reply.begin(), reply.end());
  assert(text.find("s3cret") == std::string::npos);

  // Longer read requests still never return stored content.
  reply = run(req(cc_read, BLE_password, "padding"));
  assert(std::search(reply.begin(), reply.end(), userConfig->password.begin(), userConfig->password.end()) == reply.end());

  reply = run(req(cc_write, BLE_password, "new-pass"));
  assert(same(reply, {0x80, 0x13}));  // The write response does not echo the password.
  assert(userConfig->password == "new-pass");

  reply = run(req(cc_write, BLE_password));
  assert(same(reply, {0x80, 0x13}));
  assert(userConfig->password == "new-pass");

  reply = run(req(cc_read, BLE_capabilities));
  assert(same(reply, {0x80, 0x36, 0x01, 0x03, 0x00, 0x00, 0x00}));
  reply = run(req(cc_write, BLE_capabilities));
  assert(same(reply, {0xFF, 0x36}));
  reply = run(req(cc_write, BLE_capabilities, std::string("\x01\x00\x00\x00", 4)));
  assert(reply.size() >= 2 && reply[0] == cc_error && reply[1] == BLE_capabilities);

  assert(customCharacteristicValueFormat(0x01) == CustomUnknown);
  assert(customCharacteristicValueFormat(0x11) == CustomUnknown);
  assert(customCharacteristicValueFormat(0x33) == CustomUnknown);
  assert(customCharacteristicValueFormat(BLE_capabilities) == CustomCapabilities);
  assert((customCharacteristicCapabilities() & CUSTOM_CAPABILITY_PASSWORD_WRITE_ONLY) != 0);
  return 0;
}
'''


class TestCustomCharacteristicSecrets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_password_and_capability_cases_with_production_source(self):
        source = self.source
        start = source.index("  size_t returnLength = rxValue.length();")
        prelude = source[start:source.index("  size_t requiredLength", start)]
        tail_start = source.index('  if (returnString == "") {')
        response = source[tail_start:source.index("\n  return returnValue;", tail_start)]
        harness = (HARNESS.replace("@@PRELUDE@@", prelude)
                   .replace("@@PASSWORD@@", case_block(source, "case BLE_password:"))
                   .replace("@@CAPABILITIES@@", case_block(source, "case BLE_capabilities:"))
                   .replace("@@RESPONSE@@", response))
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required")
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "secrets.cpp"
            exe = Path(directory) / "secrets.exe"
            endian = Path(directory) / "endian.o"
            cpp.write_text(harness, encoding="utf-8")
            include = str(ROOT / "lib/ArduinoCompat/include")
            subprocess.run([compiler, "-x", "c", "-I" + include, "-c",
                            str(ROOT / "lib/ArduinoCompat/src/os/endian.c"), "-o", str(endian)], check=True)
            subprocess.run([compiler, "-std=c++17", "-I" + str(ROOT / "lib/SS2K/include"), "-I" + include,
                            "-I" + str(ROOT / "lib/SS2kProtocol/c/include"), str(cpp), str(endian), "-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_password_is_never_read_or_notified(self):
        source = self.source
        notify = function(source, "void BLE_ss2kCustomCharacteristic::parseNemit(")
        for forbidden in ("BLE_password", "getPassword", "FirmwareUpdateURL"):
            self.assertNotIn(forbidden, notify)
        process = function(source, "std::vector<uint8_t> BLE_ss2kCustomCharacteristic::process(")
        self.assertNotIn("getPassword(", process)
        for forbidden in ("getAutoUpdate", "setAutoUpdate", "FirmwareUpdateURL", "BLE_autoUpdate"):
            self.assertNotIn(forbidden, source)

    def test_debug_dump_masks_password_writes(self):
        process = function(self.source, "std::vector<uint8_t> BLE_ss2kCustomCharacteristic::process(")
        self.assertIn("BLE_password) length = 2;", process)
        self.assertLess(process.index("BLE_password) length = 2;"), process.index("ss2k_log_hex_to_buffer("))


if __name__ == "__main__":
    unittest.main()
