/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "Version_Converter.h"
#include "Builtin_Pages.h"
#include "HTTP_Server_Basic.h"
#include "cert.h"
#include "SS2KLog.h"
#include "DirConManager.h"
#include "FirmwareImageValidation.h"
#include <WebServer.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <BLE_Custom_Characteristic.h>
#include <Preferences.h>
#include <esp_err.h>
#include <esp_ota_ops.h>

File fsUploadFile;

IPAddress myIP;

// DNS server
const byte DNS_PORT = 53;
DNSServer dnsServer;
HTTP_Server httpServer;
WebServer server(80);

// Helper functions for build version management
String readStoredBuildVersion() {
  File file = LittleFS.open(BUILD_VERSION_FILENAME, "r");
  if (!file) {
    return "";  // File doesn't exist
  }
  String version = file.readString();
  file.close();
  version.trim();  // Remove any trailing whitespace/newlines
  return version;
}

void writeStoredBuildVersion(const String& version) {
  File file = LittleFS.open(BUILD_VERSION_FILENAME, "w");
  if (file) {
    file.print(version);
    file.close();
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Build version saved: %s", version.c_str());
  } else {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Failed to save build version");
  }
}

// NVS guard helpers (one-time recovery per firmware version)
namespace {
const char* OTA_REC_NS     = "ota_recover";  // namespace
const char* OTA_REC_KEY    = "ver";          // key storing last recovered firmware version
const char* OTA_FS_VER_KEY = "fs_ver";     // key storing the installed web-filesystem version
bool otaUploadRejected      = false;
bool otaFilesystemUpload    = false;
bool otaFirmwareUpdateBegun = false;
esp_ota_handle_t otaFirmwareHandle = 0;
const esp_partition_t* otaFirmwarePartition = nullptr;
uint8_t otaFirmwareHeader[sizeof(esp_image_header_t)];
size_t otaFirmwareHeaderLength = 0;
String otaUploadError;

void abortFirmwareUpload() {
  if (otaFirmwareUpdateBegun) {
    esp_ota_abort(otaFirmwareHandle);
  }
  otaFirmwareHandle      = 0;
  otaFirmwarePartition   = nullptr;
  otaFirmwareUpdateBegun = false;
}

void resetFirmwareUploadValidation() {
  abortFirmwareUpload();
  otaFirmwareUpdateBegun  = false;
  otaFirmwareHeaderLength = 0;
  otaUploadError           = "";
}

void rejectFirmwareUpload(const char* message, esp_err_t error = ESP_OK) {
  otaUploadRejected = true;
  otaUploadError    = message;
  abortFirmwareUpload();
  ss2k->isUpdating = false;
  if (error == ESP_OK) {
    SS2K_LOGE(HTTP_SERVER_LOG_TAG, "%s", message);
  } else {
    SS2K_LOGE(HTTP_SERVER_LOG_TAG, "%s: %s (%d)", message, esp_err_to_name(error), error);
  }
}

bool beginFirmwareUpload() {
  otaFirmwarePartition = esp_ota_get_next_update_partition(nullptr);
  if (otaFirmwarePartition == nullptr) {
    rejectFirmwareUpload("No OTA update partition is available.");
    return false;
  }

  // WebServer does not expose the file size until UPLOAD_FILE_END. Incremental
  // erase avoids both a full-partition erase here and Arduino Update's 4 KiB
  // heap buffer, which can exhaust fragmented RAM on the classic ESP32.
  const esp_err_t result = esp_ota_begin(otaFirmwarePartition, OTA_WITH_SEQUENTIAL_WRITES, &otaFirmwareHandle);
  if (result != ESP_OK) {
    rejectFirmwareUpload("Unable to start firmware update.", result);
    return false;
  }

  otaFirmwareUpdateBegun = true;
  return true;
}

bool writeFirmwareUpload(const uint8_t* data, size_t length) {
  const esp_err_t result = esp_ota_write(otaFirmwareHandle, data, length);
  if (result == ESP_OK) return true;

  rejectFirmwareUpload("Firmware upload write failed.", result);
  return false;
}

bool finishFirmwareUpload() {
  const esp_ota_handle_t completedHandle = otaFirmwareHandle;
  otaFirmwareHandle                     = 0;
  otaFirmwareUpdateBegun                = false;

  esp_err_t result = esp_ota_end(completedHandle);
  if (result != ESP_OK) {
    otaFirmwarePartition = nullptr;
    rejectFirmwareUpload("Firmware image validation failed.", result);
    return false;
  }

  result = esp_ota_set_boot_partition(otaFirmwarePartition);
  otaFirmwarePartition = nullptr;
  if (result != ESP_OK) {
    rejectFirmwareUpload("Unable to select the uploaded firmware for boot.", result);
    return false;
  }

  return true;
}

bool beginHttpRequest(HTTPClient& request, NetworkClient& client, const String& url) {
  if (request.begin(client, url)) return true;
  SS2K_LOGE(HTTP_SERVER_LOG_TAG, "Unable to initialize HTTP request for %s", url.c_str());
  return false;
}

int beginHttpGet(HTTPClient& request, NetworkClient& client, const String& url) {
  if (!beginHttpRequest(request, client, url)) return HTTPC_ERROR_CONNECTION_REFUSED;
  return request.GET();
}

bool filesystemIndexExists() {
  return LittleFS.exists("/index.html.gz") || LittleFS.exists("/index.html");
}

String contentTypeForPath(String path) {
  if (path.endsWith(".gz")) {
    path.remove(path.length() - 3);
  }
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css")) return "text/css";
  if (path.endsWith(".js")) return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".ico")) return "image/x-icon";
  return "application/octet-stream";
}

String getNVSFilesystemVersion() {
  Preferences p;
  if (!p.begin(OTA_REC_NS, true)) return "";
  String version = p.getString(OTA_FS_VER_KEY, "");
  p.end();
  return version;
}

bool setNVSFilesystemVersion(const String& version) {
  Preferences p;
  if (!p.begin(OTA_REC_NS, false)) return false;
  size_t length = p.putString(OTA_FS_VER_KEY, version);
  p.end();
  return length > 0;
}

bool manifestContains(const JsonArray& files, const String& filename) {
  for (JsonVariantConst entry : files) {
    String manifestName = "/" + entry.as<String>();
    if (manifestName == filename) return true;
  }
  return false;
}

bool pruneFilesystem(const JsonArray& files) {
  bool success = true;
  while (true) {
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Failed to inspect filesystem before update");
      return false;
    }

    String staleName;
    File entry = root.openNextFile();
    while (entry) {
      String filename = entry.name();
      if (!filename.startsWith("/")) filename = "/" + filename;
      bool preserved = filename == configFILENAME || filename == POWER_TABLE_FILENAME || filename == BUILD_VERSION_FILENAME;
      if (!preserved && !manifestContains(files, filename)) {
        staleName = filename;
        entry.close();
        break;
      }
      entry = root.openNextFile();
    }
    entry.close();
    root.close();

    if (staleName.isEmpty()) break;
    if (LittleFS.remove(staleName)) {
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Removed stale filesystem file: %s", staleName.c_str());
    } else {
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Failed to remove stale filesystem file: %s", staleName.c_str());
      success = false;
      break;
    }
  }
  return success;
}

String getNVSRecoveryVersion() {
  Preferences p;
  if (!p.begin(OTA_REC_NS, true)) {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "NVS (ro) open failed");
    return "";
  }
  String v = p.getString(OTA_REC_KEY, "");
  p.end();
  return v;
}

bool setNVSRecoveryVersion(const String& v) {
  Preferences p;
  if (!p.begin(OTA_REC_NS, false)) {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "NVS (rw) open failed");
    return false;
  }
  size_t n = p.putString(OTA_REC_KEY, v);
  p.end();
  return n > 0;
}
}  // namespace

void _staSetup() {
  WiFi.setHostname(userConfig->getDeviceName());
  WiFi.mode(WIFI_STA);
  WiFi.begin(userConfig->getSsid(), userConfig->getPassword());
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
}

void _APSetup() {
  // WiFi.eraseAP(); //Needed if we switch back to espressif32 @6.5.0
  WiFi.mode(WIFI_AP);
  // WiFi.setHostname("reset");  // Fixes a bug when switching Arduino Core Versions
  // WiFi.softAPsetHostname("reset");
  // WiFi.setHostname(userConfig->getDeviceName());
  WiFi.softAPsetHostname(userConfig->getDeviceName());
  WiFi.enableAP(true);
  delay(500);  // Micro controller requires some time to reset the mode
}

// ********************************WIFI Setup*************************
void startWifi() {
  int i = 0;
  
  // Check build version for OTA WiFi issue handling
  String storedVersion = readStoredBuildVersion();
  String currentVersion = FIRMWARE_VERSION;
  bool versionMismatch = (storedVersion != currentVersion || storedVersion.length() == 0);
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "Build version check. FS:'%s' CUR:'%s'", storedVersion.c_str(), currentVersion.c_str());
  if (versionMismatch) {
    String nvsVer          = getNVSRecoveryVersion();
    bool recoveryCompleted = (nvsVer == currentVersion);
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Build version mismatch. FS:'%s' NVS:'%s' CUR:'%s'", storedVersion.c_str(), nvsVer.c_str(), currentVersion.c_str());

    if (!recoveryCompleted) {
      // Perform one-time recovery sequence
      WiFi.setHostname("reset");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Hostname temporarily set to 'reset' (OTA recovery)");

      writeStoredBuildVersion(currentVersion);
      bool fsOk  = (readStoredBuildVersion() == currentVersion);
      bool nvsOk = setNVSRecoveryVersion(currentVersion);

      // Restore original hostname
      WiFi.setHostname(userConfig->getDeviceName());
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Hostname restored: %s", userConfig->getDeviceName());

      if (fsOk && nvsOk) {
        ss2k->rebootFlag = true;
        SS2K_LOG(HTTP_SERVER_LOG_TAG, "Recovery persisted (fsOk=%d nvsOk=%d). Reboot flagged.", fsOk, nvsOk);
      } else {
        SS2K_LOG(HTTP_SERVER_LOG_TAG, "Recovery persistence failed (fsOk=%d nvsOk=%d). Skipping reboot to avoid loop.", fsOk, nvsOk);
      }
    } else {
      // Already recovered for this firmware version. Ensure file is updated if missing/different.
      if (storedVersion != currentVersion) {
        writeStoredBuildVersion(currentVersion);
        SS2K_LOG(HTTP_SERVER_LOG_TAG, "Re-synced build version file without reboot (already recovered)");
      }
    }
  }

  // Trying Station mode first:
  if (strcmp(userConfig->getSsid(), DEVICE_NAME) != 0) {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Connecting to: %s", userConfig->getSsid());
    _staSetup();
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Waiting for connection to be established...");
      i++;
      if (i > WIFI_CONNECT_TIMEOUT) {
        SS2K_LOG(HTTP_SERVER_LOG_TAG, "Couldn't Connect. Switching to AP mode");
        WiFi.disconnect(true, true);
        WiFi.setAutoReconnect(false);
        WiFi.mode(WIFI_MODE_NULL);
        delay(1000);
        break;
      }
    }
  }

  // Did we connect in STA mode?
  if (WiFi.status() == WL_CONNECTED) {
    myIP                          = WiFi.localIP();
    httpServer.internetConnection = true;
  }

  // Couldn't connect to existing network, Create SoftAP
  if (WiFi.status() != WL_CONNECTED) {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Starting AP Mode");
    _APSetup();
    if (strcmp(userConfig->getSsid(), DEVICE_NAME) == 0) {
      // If default SSID is still in use, let the user select a new password.
      // Else fall back to the default password.
      WiFi.softAP(userConfig->getDeviceName(), userConfig->getPassword());
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Using Stored Password");
    } else {
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Using Default Password");
      WiFi.softAP(userConfig->getDeviceName(), DEFAULT_PASSWORD);
    }
    delay(50);
    myIP = WiFi.softAPIP();
    /* Setup the DNS server redirecting all the domains to the apIP */
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", myIP);
  }

  if (!MDNS.begin(userConfig->getDeviceName())) {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Error setting up MDNS responder!");
  }

  MDNS.addService("http", "_tcp", 80);
  MDNS.addServiceTxt("http", "_tcp", "lf", "0");
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "Connected to %s IP address: %s", userConfig->getSsid(), myIP.toString().c_str());
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "Open http://%s.local/", userConfig->getDeviceName());

  // Initialize DirCon MDNS service
  if (DirConManager::start()) {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "DirCon service started successfully");
  } else {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Error starting DirCon service");
  }

  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  if (WiFi.getMode() == WIFI_STA) {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Syncing clock...");
    configTime(0, 0, "pool.ntp.org");  // get UTC time via NTP
    time_t now = time(nullptr);
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Waiting for clock sync");
    while (now < 10) {  // wait 10 seconds
      SS2K_LOG(".", ".");
      delay(100);
      now = time(nullptr);
    }
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Clock synced to: %.f", difftime(now, (time_t)0));
  }
}

void stopWifi() {
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "Closing connection to: %s", userConfig->getSsid());
  // Stop DirCon service before disconnecting WiFi
  DirConManager::stop();
  WiFi.disconnect();
}

void HTTP_Server::start() {
  server.enableCORS(true);
  server.onNotFound(handleIndexFile);

  /***************************Begin Handlers*******************/
  server.on("/", handleIndexFile);
  server.on("/index.html", handleIndexFile);
  server.on("/generate_204", handleIndexFile);         // Android captive portal
  server.on("/fwlink", handleIndexFile);               // Microsoft captive portal
  server.on("/hotspot-detect.html", handleIndexFile);  // Apple captive portal
  server.on("/style.css", handleLittleFSFile);
  server.on("/btsimulator.html", handleLittleFSFile);
  server.on("/develop.html", handleLittleFSFile);
  server.on("/shift.html", handleLittleFSFile);
  server.on("/settings.html", handleLittleFSFile);
  server.on("/status.html", handleLittleFSFile);
  server.on("/bluetoothscanner.html", handleBTScanner);
  server.on("/streamfit.html", handleLittleFSFile);
  server.on("/hrtowatts.html", handleLittleFSFile);
  server.on("/favicon.ico", handleLittleFSFile);
  server.on("/send_settings", settingsProcessor);
  server.on("/jquery.js.gz", handleLittleFSFile);

  server.on("/BLEScan", []() {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Scanning from web request");
    String response =
        "<!DOCTYPE html><html><body>Scanning for BLE Devices. Please wait "
        "15 seconds.</body><script> setTimeout(\"location.href = '/bluetoothscanner.html';\",15000);</script></html>";
    // spinBLEClient.resetDevices();
    spinBLEClient.doScan = true;
    server.send(200, "text/html", response);
  });

  server.on("/load_defaults.html", []() {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Setting Defaults from Web Request");
    ss2k->resetDefaultsFlag = true;
    String response =
        "<!DOCTYPE html><html><body><h1>Defaults have been "
        "loaded.</h1><p><br><br> Please reconnect to the device on WiFi "
        "network: " +
        myIP.toString() + "</p></body></html>";
    server.send(200, "text/html", response);
  });

  server.on("/reboot.html", []() {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Rebooting from Web Request");
    String response = "Rebooting....<script> setTimeout(\"location.href = '/index.html';\",500); </script>";
    server.send(200, "text/html", response);
    ss2k->rebootFlag = true;
  });

  server.on("/hrslider", []() {
    String value = server.arg("value");
    if (value == "enable") {
      rtConfig->hr.setSimulate(true);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "HR Simulator turned on");
    } else if (value == "disable") {
      rtConfig->hr.setSimulate(false);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "HR Simulator turned off");
    } else {
      rtConfig->hr.setValue(value.toInt());
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "HR is now: %d", rtConfig->hr.getValue());
      server.send(200, "text/plain", "OK");
    }
  });

  server.on("/wattsslider", []() {
    String value = server.arg("value");
    if (value == "enable") {
      rtConfig->watts.setSimulate(true);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Watt Simulator turned on");
    } else if (value == "disable") {
      rtConfig->watts.setSimulate(false);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Watt Simulator turned off");
    } else {
      rtConfig->watts.setValue(value.toInt());
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Watts are now: %d", rtConfig->watts.getValue());
      server.send(200, "text/plain", "OK");
    }
  });

  server.on("/cadslider", []() {
    String value = server.arg("value");
    if (value == "enable") {
      rtConfig->cad.setSimulate(true);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "CAD Simulator turned on");
    } else if (value == "disable") {
      rtConfig->cad.setSimulate(false);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "CAD Simulator turned off");
    } else {
      rtConfig->cad.setValue(value.toInt());
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "CAD is now: %d", rtConfig->cad.getValue());
      server.send(200, "text/plain", "OK");
    }
  });

  server.on("/ergmode", []() {
    String value = server.arg("value");
    if (value == "enable") {
      rtConfig->setFTMSMode(FitnessMachineControlPointProcedure::SetTargetPower);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "ERG Mode turned on");
    } else {
      rtConfig->setFTMSMode(FitnessMachineControlPointProcedure::RequestControl);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "ERG Mode turned off");
    }
  });

  server.on("/targetwattsslider", []() {
    String value = server.arg("value");
    if (value == "enable") {
      rtConfig->setSimTargetWatts(true);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Target Watts Simulator turned on");
    } else if (value == "disable") {
      rtConfig->setSimTargetWatts(false);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Target Watts Simulator turned off");
    } else {
      rtConfig->watts.setTarget(value.toInt());
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Target Watts are now: %d", rtConfig->watts.getTarget());
      server.send(200, "text/plain", "OK");
    }
  });

  server.on("/shift", []() {
    int value = server.arg("value").toInt();
    if ((value > -10) && (value < 10)) {
      rtConfig->setShifterPosition(rtConfig->getShifterPosition() + value);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Shift From HTML");
    } else {
      rtConfig->setShifterPosition(value);
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Invalid HTML Shift");
      server.send(200, "text/plain", "OK");
    }
    // BLE Shift notifications are handles by the shift processing in main.cpp
  });

  server.on("/configJSON", []() {
    String tString;
    tString = userConfig->returnJSON();
    server.send(200, "text/plain", tString);
  });

  server.on("/runtimeConfigJSON", []() {
    String tString;
    tString = rtConfig->returnJSON();
    server.send(200, "text/plain", tString);
  });

  server.on("/login", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", OTALoginIndex);
  });

  server.on("/OTAIndex", HTTP_GET, []() {
    ss2k->stopTasks();
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", OTAServerIndex);
  });

  server.on(
      "/update", HTTP_POST,
      // This is the onComplete callback. It is executed ONLY after the upload is fully finished.
      // This is the correct and only place to send the final response to the client.
      []() {
        server.sendHeader("Connection", "close");
        if (otaUploadRejected) {
          server.send(400, "text/plain", otaUploadError.isEmpty() ? String("Wrong image filename. Expected ") + FW_BINFILE + " or " + FS_BINFILE + "."
                                                                   : otaUploadError);
          return;
        }
        // Check if the Update process reported an error and send the final status.
        if (otaFilesystemUpload && Update.hasError()) {
          // You can get more specific error information if you want
          // size_t len = Update.getErrorString(error_string_buffer, 128);
          // server.send(500, "text/plain", error_string_buffer);
          server.send(500, "text/plain", "FAIL");
        } else {
          if (otaFilesystemUpload) {
            // Update replaced the mounted partition underneath LittleFS. Remount
            // the new image before restoring the in-memory user configuration.
            LittleFS.end();
            if (LittleFS.begin(false)) {
              userConfig->saveToLittleFS();
              SS2K_LOG(HTTP_SERVER_LOG_TAG, "Settings saved to the uploaded LittleFS image");
            } else {
              SS2K_LOGE(HTTP_SERVER_LOG_TAG, "Failed to remount LittleFS before saving settings");
            }
          }
          server.send(200, "text/plain", "OK");
          if (otaFilesystemUpload) {
            // The filesystem was replaced underneath the running web server. Reboot
            // directly after allowing the response to reach the browser.
            delay(500);
            ESP.restart();
          } else {
            // Firmware uploads can use the normal cooperative reboot path.
            ss2k->rebootFlag = true;
          }
        }
      },
      // This is the onUpload callback. It handles the file data as it arrives.
      // It should not send any response to the client.
      []() {
        HTTPUpload &upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
          otaUploadRejected    = false;
          otaFilesystemUpload = upload.filename == FS_BINFILE;
          resetFirmwareUploadValidation();
        }
        if (upload.filename == FW_BINFILE) {
          if (upload.status == UPLOAD_FILE_START) {
            ss2k->isUpdating = true;  // Set the updating flag to true
            SS2K_LOG(HTTP_SERVER_LOG_TAG, "Update Start: %s", upload.filename.c_str());
          } else if (upload.status == UPLOAD_FILE_WRITE) {
            size_t chunkOffset = 0;
            if (!otaFirmwareUpdateBegun && !otaUploadRejected) {
              const size_t headerBytesNeeded = sizeof(otaFirmwareHeader) - otaFirmwareHeaderLength;
              const size_t headerBytesInChunk = min(headerBytesNeeded, upload.currentSize);
              memcpy(otaFirmwareHeader + otaFirmwareHeaderLength, upload.buf, headerBytesInChunk);
              otaFirmwareHeaderLength += headerBytesInChunk;
              chunkOffset += headerBytesInChunk;

              if (otaFirmwareHeaderLength == sizeof(otaFirmwareHeader)) {
                const FirmwareImageHeaderValidation validation = validateFirmwareImageHeader(otaFirmwareHeader, otaFirmwareHeaderLength);
                if (validation.result != FirmwareImageHeaderResult::Valid) {
                  otaUploadRejected = true;
                  otaUploadError    = String("Rejected firmware image: ") + firmwareImageHeaderResultName(validation.result) + ".";
                  ss2k->isUpdating  = false;
                  SS2K_LOGE(HTTP_SERVER_LOG_TAG, "Rejected uploaded firmware image: %s (expected chip 0x%04x, image chip 0x%04x)",
                            firmwareImageHeaderResultName(validation.result), CONFIG_IDF_FIRMWARE_CHIP_ID, static_cast<uint16_t>(validation.imageChipId));
                  return;
                }

                if (!beginFirmwareUpload()) return;

                if (!writeFirmwareUpload(otaFirmwareHeader, sizeof(otaFirmwareHeader))) return;
              }
            }

            if (!otaUploadRejected && otaFirmwareUpdateBegun && chunkOffset < upload.currentSize &&
                !writeFirmwareUpload(upload.buf + chunkOffset, upload.currentSize - chunkOffset)) {
              return;
            }
          } else if (upload.status == UPLOAD_FILE_END) {
            // DO NOT send a response here.
            if (otaUploadRejected) {
              abortFirmwareUpload();
              ss2k->isUpdating = false;
            } else if (!otaFirmwareUpdateBegun) {
              otaUploadRejected = true;
              if (otaUploadError.isEmpty()) otaUploadError = "Firmware image header is incomplete.";
              ss2k->isUpdating = false;
            } else if (finishFirmwareUpload()) {
              SS2K_LOG(HTTP_SERVER_LOG_TAG, "Firmware Upload Finished Successfully.");
            }
            // The reboot will be triggered in the onComplete handler after the response.
          } else if (upload.status == UPLOAD_FILE_ABORTED) {
            rejectFirmwareUpload("Firmware upload was aborted.");
          }
        } else if (upload.filename == FS_BINFILE) {
          if (upload.status == UPLOAD_FILE_START) {
            SS2K_LOG(HTTP_SERVER_LOG_TAG, "Update Start: %s", upload.filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
              Update.printError(Serial);
            }
          } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
              Update.printError(Serial);
            }
          } else if (upload.status == UPLOAD_FILE_END) {
            // Finalize the update.
            // DO NOT send a response here.
            if (Update.end(true)) {
              SS2K_LOG(HTTP_SERVER_LOG_TAG, "Littlefs Upload Finished Successfully.");
            } else {
              Update.printError(Serial);
            }
          }
        } else if (upload.filename.endsWith(".bin")) {
          if (upload.status == UPLOAD_FILE_START) {
            otaUploadRejected = true;
            otaUploadError    = String("Wrong image filename. Expected ") + FW_BINFILE + " or " + FS_BINFILE + ".";
            SS2K_LOG(HTTP_SERVER_LOG_TAG, "Rejected image %s; expected %s or %s", upload.filename.c_str(), FW_BINFILE, FS_BINFILE);
          }
        } else {  // Handles other file uploads to LittleFS
          if (upload.status == UPLOAD_FILE_START) {
            String filename = upload.filename;
            if (!filename.startsWith("/")) {
              filename = "/" + filename;
            }
            SS2K_LOG(HTTP_SERVER_LOG_TAG, "handleFileUpload Name: %s", filename.c_str());
            fsUploadFile = LittleFS.open(filename, "w");
            filename     = String();
          } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (fsUploadFile) {
              fsUploadFile.write(upload.buf, upload.currentSize);
            }
          } else if (upload.status == UPLOAD_FILE_END) {
            if (fsUploadFile) {
              fsUploadFile.close();
            }
            SS2K_LOG(HTTP_SERVER_LOG_TAG, "handleFileUpload Size: %zu", upload.totalSize);
            // For non-firmware files, it's okay to send a response here,
            // but for consistency, it's better to let the onComplete handler do it.
            // For this example, we assume the main onComplete handler is for firmware.
            // A more robust solution would check which type of file was uploaded.
          }
        }
      });

  /********************************************End Server
   * Handlers*******************************/

  server.begin();
  server.enableDelay(false);
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "HTTP server started");
}

void HTTP_Server::webClientUpdate() {
  static unsigned long int _webClientTimer = millis();
  if (millis() - _webClientTimer > WEBSERVER_DELAY) {
    _webClientTimer                = millis();
    static unsigned long mDnsTimer = millis();  // NOLINT: There is no overload in String for uint64_t
    server.handleClient();
    // if (WiFi.getMode() != WIFI_MODE_STA) {
    //   dnsServer.processNextRequest();
    // }
    //  Keep MDNS alive
    if ((millis() - mDnsTimer) > 30000) {
      MDNS.addServiceTxt("http", "_tcp", "lf", String(mDnsTimer));
      mDnsTimer = millis();
    }
  }
}

void HTTP_Server::handleBTScanner() {
  spinBLEClient.doScan = true;
  handleLittleFSFile();
}

void HTTP_Server::handleIndexFile() {
  String filename = LittleFS.exists("/index.html.gz") ? "/index.html.gz" : "/index.html";
  if (LittleFS.exists(filename)) {
    File file = LittleFS.open(filename, FILE_READ);
    server.streamFile(file, "text/html");
    file.close();
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Served %s", filename.c_str());
  } else {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "%s not found. Sending builtin Index.html", filename.c_str());
    server.send(200, "text/html", noIndexHTML);
  }
}

void HTTP_Server::handleLittleFSFile() {
  String filename = server.uri();
  if (!LittleFS.exists(filename) && LittleFS.exists(filename + ".gz")) {
    filename += ".gz";
  }
  if (LittleFS.exists(filename)) {
    File file = LittleFS.open(filename, FILE_READ);
    server.streamFile(file, contentTypeForPath(filename));
    file.close();
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Served %s", filename.c_str());
  } else if (!filesystemIndexExists()) {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "%s not found and no filesystem. Sending builtin index.html", filename.c_str());
    handleIndexFile();
  } else {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "%s not found. Sending 404.", filename.c_str());
    String outputhtml = "<html><body><h1>ERROR 404 <br> FILE NOT FOUND!" + filename + "</h1></body></html>";
    server.send(404, "text/html", outputhtml);
  }
}

void HTTP_Server::settingsProcessor() {
  String tString;
  bool wasBTUpdate       = false;
  bool wasSettingsUpdate = false;
  bool reboot            = false;
  if (!server.arg("ssid").isEmpty()) {
    tString = server.arg("ssid");
    tString.trim();
    userConfig->setSsid(tString);
  }
  if (!server.arg("password").isEmpty()) {
    tString = server.arg("password");
    tString.trim();
    userConfig->setPassword(tString);
  }
  if (!server.arg("deviceName").isEmpty()) {
    tString = server.arg("deviceName");
    tString.trim();
    userConfig->setDeviceName(tString);
  }
  if (!server.arg("shiftStep").isEmpty()) {
    uint64_t shiftStep = server.arg("shiftStep").toInt();
    if (shiftStep >= 10 && shiftStep <= 6000) {
      userConfig->setShiftStep(shiftStep);
    }
    wasSettingsUpdate = true;
  }
  if (!server.arg("stepperPower").isEmpty()) {
    uint64_t stepperPower = server.arg("stepperPower").toInt();
    if (stepperPower >= 100 && stepperPower <= 2000) {
      userConfig->setStepperPower(stepperPower);
      ss2k->updateStepperPower();
    }
  }
  if (!server.arg("maxWatts").isEmpty()) {
    uint64_t maxWatts = server.arg("maxWatts").toInt();
    if (maxWatts >= 0 && maxWatts <= 2000) {
      userConfig->setMaxWatts(maxWatts);
    }
  }
  if (!server.arg("minWatts").isEmpty()) {
    uint64_t minWatts = server.arg("minWatts").toInt();
    if (minWatts >= 0 && minWatts <= 200) {
      userConfig->setMinWatts(minWatts);
    }
  }
  if (!server.arg("ERGSensitivity").isEmpty()) {
    float ERGSensitivity = server.arg("ERGSensitivity").toFloat();
    if (ERGSensitivity >= .1 && ERGSensitivity <= 20) {
      userConfig->setERGSensitivity(ERGSensitivity);
    }
  }
  if (!server.arg("stepperDir").isEmpty()) {
    userConfig->setStepperDir(true);
  } else if (wasSettingsUpdate) {
    userConfig->setStepperDir(false);
  }
  if (!server.arg("shifterDir").isEmpty()) {
    userConfig->setShifterDir(true);
  } else if (wasSettingsUpdate) {
    userConfig->setShifterDir(false);
  }
  if (!server.arg("udpLogEnabled").isEmpty()) {
    userConfig->setUdpLogEnabled(true);
  } else if (wasSettingsUpdate) {
    userConfig->setUdpLogEnabled(false);
  }
  if (!server.arg("pTab4Pwr").isEmpty()) {
    userConfig->setPTab4Pwr(true);
  } else if (wasSettingsUpdate) {
    userConfig->setPTab4Pwr(false);
  }
  if (!server.arg("stealthChop").isEmpty()) {
    userConfig->setStealthChop(true);
    ss2k->updateStealthChop();
  } else if (wasSettingsUpdate) {
    userConfig->setStealthChop(false);
    ss2k->updateStealthChop();
  }
  if (!server.arg("inclineMultiplier").isEmpty()) {
    float inclineMultiplier = server.arg("inclineMultiplier").toFloat();
    if (inclineMultiplier >= 0 && inclineMultiplier <= 10) {
      userConfig->setInclineMultiplier(inclineMultiplier);
    }
  }
  if (!server.arg("powerCorrectionFactor").isEmpty()) {
    float powerCorrectionFactor = server.arg("powerCorrectionFactor").toFloat();
    if (powerCorrectionFactor >= MIN_PCF && powerCorrectionFactor <= MAX_PCF) {
      userConfig->setPowerCorrectionFactor(powerCorrectionFactor);
    }
  }
  if (!server.arg("blePMDropdown").isEmpty()) {
    wasBTUpdate = true;
    if (server.arg("blePMDropdown")) {
      tString = server.arg("blePMDropdown");
      if (tString != userConfig->getConnectedPowerMeter()) {
        userConfig->setConnectedPowerMeter(tString);
        spinBLEClient.reconnectAllDevices();
      }
    } else {
      userConfig->setConnectedPowerMeter(String(ANY));
    }
  }
  if (!server.arg("bleHRDropdown").isEmpty()) {
    wasBTUpdate = true;
    if (server.arg("bleHRDropdown")) {
      tString = server.arg("bleHRDropdown");
      if (tString != userConfig->getConnectedHeartMonitor()) {
        spinBLEClient.reconnectAllDevices();
      }
      userConfig->setConnectedHeartMonitor(server.arg("bleHRDropdown"));
    } else {
      userConfig->setConnectedHeartMonitor(String(NONE));
    }
  }
  if (!server.arg("bleRemoteDropdown").isEmpty()) {
    wasBTUpdate = true;
    if (server.arg("bleRemoteDropdown")) {
      tString = server.arg("bleRemoteDropdown");
      if (tString != userConfig->getConnectedRemote()) {
        spinBLEClient.reconnectAllDevices();
      }
      userConfig->setConnectedRemote(server.arg("bleRemoteDropdown"));
    } else {
      userConfig->setConnectedRemote(String(NONE));
    }
  }

  String response = "<!DOCTYPE html><html><body><h2>";

  if (wasBTUpdate) {  // Special BT page update response
    response +=
        "Selections Saved!</h2></body><script> setTimeout(\"location.href "
        "= '/bluetoothscanner.html';\",1000);</script></html>";
  } else if (wasSettingsUpdate) {  // Special Settings Page update response
    response +=
        "Network settings will be applied at next reboot. <br> Everything "
        "else is available immediately.</h2></body><script> "
        "setTimeout(\"location.href = '/settings.html';\",1000);</script></html>";
  } else {  // Normal response
    response +=
        "Network settings will be applied at next reboot. <br> Everything "
        "else is available immediately.</h2></body><script> "
        "setTimeout(\"location.href = '/index.html';\",1000);</script></html>";
  }
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "Config Updated From Web");
  ss2k->saveFlag = true;
  if (reboot) {
    response +=
        "Please wait while your settings are saved and SmartSpin2k reboots.</h2></body><script> "
        "setTimeout(\"location.href = '/bluetoothscanner.html';\",5000);</script></html>";
    server.send(200, "text/html", response);
    ss2k->rebootFlag = true;
  }
  server.send(200, "text/html", response);
}

void HTTP_Server::stop() {
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "Stopping Http Server");
  server.stop();
  server.close();
}

// github fingerprint
// 70:94:DE:DD:E6:C4:69:48:3A:92:70:A1:48:56:78:2D:18:64:E0:B7

void HTTP_Server::syncWebServerFiles() {
  // HTTPClient keeps a non-owning pointer to the network client. Declare the
  // network client first so HTTPClient is destroyed before its transport.
  WiFiClientSecure localClient;
  HTTPClient http;
  localClient.setCACert(rootCACertificate);
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "Checking web filesystem:");
  int httpCode = beginHttpGet(http, localClient, userConfig->getFirmwareUpdateURL() + String(FW_VERSIONFILE));
  if (httpCode != HTTP_CODE_OK) {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "error downloading %s %d", FW_VERSIONFILE, httpCode);
    httpServer.internetConnection = false;
    http.end();
    return;
  }

  String serverVersion = http.getString();
  serverVersion.trim();
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "  - Server version: %s", serverVersion.c_str());
  httpServer.internetConnection = true;
  http.end();

  const String filesystemVersion = getNVSFilesystemVersion();
  const bool indexPresent = filesystemIndexExists();
  Version availableVer(serverVersion.c_str());
  Version currentVer(FIRMWARE_VERSION);
  // Development builds append branch/commit data to the release they follow.
  // Treat that suffix as newer when the numeric date components are equal.
  const bool firmwareIsAhead = currentVer > availableVer || (currentVer == availableVer && serverVersion != FIRMWARE_VERSION);
  const bool filesystemUpgradeAvailable = !firmwareIsAhead &&
                                          (filesystemVersion.isEmpty() || availableVer > Version(filesystemVersion.c_str()));
  const bool refreshFilesystem = !indexPresent || filesystemUpgradeAvailable;

  SS2K_LOG(HTTP_SERVER_LOG_TAG, "  - Filesystem version: %s%s", filesystemVersion.isEmpty() ? "not recorded" : filesystemVersion.c_str(),
           refreshFilesystem ? " (refresh required)" : " (checking for missing files)");
  if (firmwareIsAhead && indexPresent) {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "  - Firmware is ahead of the server; existing release files will not be replaced");
  }

  httpCode = beginHttpGet(http, localClient, DATA_UPDATEURL DATA_FILELIST);
  JsonDocument doc;
  if (httpCode == HTTP_CODE_OK) {
    DeserializationError error = deserializeJson(doc, http.getStream());
    if (error) {
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Failed to read file list");
      http.end();
      return;
    }
    httpServer.internetConnection = true;
  } else {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "error downloading %s %d", DATA_FILELIST, httpCode);
    httpServer.internetConnection = false;
  }
  http.end();
  if (httpCode != HTTP_CODE_OK) return;

  JsonArray files = doc.as<JsonArray>();
  if (files.isNull() || (!manifestContains(files, "/index.html.gz") && !manifestContains(files, "/index.html"))) {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Filesystem file list is invalid or has no index page");
    return;
  }

  bool filesystemUpdateSucceeded = refreshFilesystem ? pruneFilesystem(files) : true;
  bool downloadedFile = false;
  for (JsonVariant v : files) {
    String fileName = "/" + v.as<String>();
    if (!refreshFilesystem && LittleFS.exists(fileName)) continue;

    downloadedFile = true;
    httpCode = beginHttpGet(http, localClient, DATA_UPDATEURL + fileName);
    if (httpCode == HTTP_CODE_OK) {
      LittleFS.remove(fileName);
      File file = LittleFS.open(fileName, FILE_WRITE, true);
      if (!file) {
        SS2K_LOG(HTTP_SERVER_LOG_TAG, "Failed to create file, %s", fileName.c_str());
        filesystemUpdateSucceeded = false;
        http.end();
        continue;
      }
      int bytesWritten = http.writeToStream(&file);
      file.close();
      if (bytesWritten < 0) {
        LittleFS.remove(fileName);
        SS2K_LOG(HTTP_SERVER_LOG_TAG, "Error writing %s (%d)", fileName.c_str(), bytesWritten);
        httpServer.internetConnection = false;
        filesystemUpdateSucceeded = false;
      } else {
        if (fileName.endsWith(".gz")) {
          String uncompressedName = fileName.substring(0, fileName.length() - 3);
          LittleFS.remove(uncompressedName);
        }
        SS2K_LOG(HTTP_SERVER_LOG_TAG, "Created: %s (%d bytes)", fileName.c_str(), bytesWritten);
        httpServer.internetConnection = true;
      }
    } else {
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Error downloading %s %d", fileName.c_str(), httpCode);
      httpServer.internetConnection = false;
      filesystemUpdateSucceeded = false;
    }
    http.end();
  }

  if (refreshFilesystem) {
    if (filesystemUpdateSucceeded && filesystemIndexExists()) {
      if (setNVSFilesystemVersion(serverVersion)) {
        SS2K_LOG(HTTP_SERVER_LOG_TAG, "Filesystem updated to version %s", serverVersion.c_str());
      } else {
        SS2K_LOG(HTTP_SERVER_LOG_TAG, "Failed to save filesystem version %s", serverVersion.c_str());
      }
    } else {
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Filesystem update incomplete; version was not changed");
    }
  } else if (filesystemUpdateSucceeded) {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, downloadedFile ? "Missing web files restored" : "Web filesystem is complete");
  }
}
