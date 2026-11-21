#
# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only
#

"""Client settings JSON hides the stored WiFi password while storage keeps it."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestSettingsClientJson(unittest.TestCase):
    def test_client_json_hides_password_and_storage_keeps_it(self):
        libraries = list((ROOT / ".pio/libdeps").glob("*/ArduinoJson/src/ArduinoJson.h"))
        if not libraries:
            self.skipTest("Install firmware dependencies to exercise ArduinoJson persistence")
        source = (ROOT / "src/SmartSpin_parameters.cpp").read_text(encoding="utf-8")
        production = "\n".join(function(source, signature) for signature in (
            "void userParameters::setDefaults(", "String userParameters::returnJSON(",
            "void userParameters::saveToLittleFS(", "void userParameters::loadFromLittleFS(",
            "bool userParameters::setGearTeethJSON(",
        ))
        harness = r'''
#include "persisted_settings_fakes.h"
PRODUCTION
int main() {
  const std::string secret = "s3cret-Value!";
  userParameters config, restored;
  config.setDefaults();
  JsonDocument api;
  // Defaults: empty password key, not set, and no removed settings.
  assert(!deserializeJson(api, config.returnJSON()));
  assert(api["password"].is<const char*>() && strcmp(api["password"].as<const char*>(), "") == 0);
  assert(api["passwordSet"].is<bool>() && api["passwordSet"] == false);
  assert(api["firmwareUpdateURL"].isNull() && api["autoUpdate"].isNull());

  // A stored password never appears anywhere in the client JSON.
  config.setPassword(secret.c_str());
  std::string client = config.returnJSON().c_str();
  assert(client.find(secret) == std::string::npos);
  assert(!deserializeJson(api, client));
  assert(strcmp(api["password"].as<const char*>(), "") == 0);
  assert(api["passwordSet"] == true);

  // Storage keeps the real password and omits the removed settings.
  config.saveToLittleFS();
  JsonDocument saved; assert(!deserializeJson(saved, fileContents));
  assert(strcmp(saved["password"].as<const char*>(), secret.c_str()) == 0);
  assert(saved["firmwareUpdateURL"].isNull() && saved["autoUpdate"].isNull());
  restored.loadFromLittleFS();
  assert(strcmp(restored.getPassword(), secret.c_str()) == 0);

  // Legacy files that still carry the removed settings load normally.
  fileContents = R"({"firmwareUpdateURL":"http://x/","autoUpdate":true,"ssid":"home","password":"legacy-pass","shiftStep":123,"deviceName":"bike"})";
  userParameters legacy;
  legacy.loadFromLittleFS();
  assert(strcmp(legacy.getSsid(), "home") == 0);
  assert(strcmp(legacy.getPassword(), "legacy-pass") == 0);
  assert(legacy.getShiftStep() == 123);
  assert(strcmp(legacy.getDeviceName(), "bike") == 0);

  // A stored empty password loads as empty; it differs from the default, so it reports as set.
  fileContents = R"({"ssid":"home","password":""})";
  userParameters blank;
  blank.loadFromLittleFS();
  assert(strcmp(blank.getPassword(), "") == 0);
  assert(!deserializeJson(api, blank.returnJSON()));
  assert(api["passwordSet"] == true);
}
'''.replace("PRODUCTION", production)
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required")
        with tempfile.TemporaryDirectory(prefix="ss2k-client-json-") as directory:
            cpp = Path(directory) / "test.cpp"
            exe = Path(directory) / "test.exe"
            cpp.write_text(harness, encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", "-DPLATFORMIO_ENV_NATIVE", '-DFIRMWARE_VERSION="test"',
                            "-I" + str(ROOT / "test"), "-I" + str(ROOT / "include"), "-I" + str(ROOT / "lib/SS2K/include"),
                            "-I" + str(ROOT / "lib/ArduinoCompat/include"), "-I" + str(libraries[0].parent),
                            str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_settings_page_never_fills_password_field(self):
        for variant in ("data", "data_s3"):
            with self.subTest(variant=variant):
                html = (ROOT / variant / "settings.html").read_text(encoding="utf-8")
                start = html.index("      password: {")
                self.assertIn("defaultValue: ''", html[start:html.index("deviceName: {", start)])
                body = html[html.index("function requestConfigValues"):html.index("function updateSlider")]
                self.assertIn("if (key === 'password') continue;", body)
                self.assertNotIn("obj.password", body.replace("obj.passwordSet", ""))
                self.assertIn('autocomplete="new-password"', html)


if __name__ == "__main__":
    unittest.main()
