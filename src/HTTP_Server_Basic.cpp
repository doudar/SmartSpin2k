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
#include <WebServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <BLE_Custom_Characteristic.h>

File fsUploadFile;

IPAddress myIP;

// DNS server
const byte DNS_PORT = 53;
DNSServer dnsServer;
HTTP_Server httpServer;
WebServer server(80);

void _staSetup() {
  WiFi.setHostname(userConfig->getDeviceName());
  WiFi.mode(WIFI_STA);
  WiFi.begin(userConfig->getSsid(), userConfig->getPassword());
  WiFi.setAutoReconnect(true);
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
        "15 seconds.</body><script> setTimeout(\"location.href = 'http://" +
        myIP.toString() + "/bluetoothscanner.html';\",15000);</script></html>";
    // spinBLEClient.resetDevices();
    spinBLEClient.doScan        = true;
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
    String response = "Rebooting....<script> setTimeout(\"location.href = 'http://" + myIP.toString() + "/index.html';\",500); </script>";
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

  /*handling uploading firmware file */
  server.on(
      "/update", HTTP_POST,
      []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
      },
      []() {
        HTTPUpload &upload = server.upload();
        if (upload.filename == String("firmware.bin").c_str()) {
          ss2k->isUpdating = true;  // Set the updating flag to true
          if (upload.status == UPLOAD_FILE_START) {
            SS2K_LOG(HTTP_SERVER_LOG_TAG, "Update: %s", upload.filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {  // start with max
                                                                // available size
              Update.printError(Serial);
            }
          } else if (upload.status == UPLOAD_FILE_WRITE) {
            /* flashing firmware to ESP*/
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
              Update.printError(Serial);
            }
          } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {  // true to set the size to the
                                     // current progress
              server.send(200, "text/plain", "Firmware Uploaded Successfully. Rebooting...");
              ESP.restart();
            } else {
              Update.printError(Serial);
            }
          }
          ss2k->isUpdating = false;  // Reset the updating flag
        } else if (upload.filename == String("littlefs.bin").c_str()) {
          if (upload.status == UPLOAD_FILE_START) {
            SS2K_LOG(HTTP_SERVER_LOG_TAG, "Update: %s", upload.filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {  // start with max
                                                                 // available size
              Update.printError(Serial);
            }
          } else if (upload.status == UPLOAD_FILE_WRITE) {
            /* flashing firmware to ESP*/
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
              Update.printError(Serial);
            }
          } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {  // true to set the size to the
                                     // current progress
              server.send(200, "text/plain", "Littlefs Uploaded Successfully. Rebooting...");
              userConfig->saveToLittleFS();
              ss2k->rebootFlag == true;
            } else {
              Update.printError(Serial);
            }
          }
        } else {
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
            server.send(200, "text/plain", String(upload.filename + " Uploaded Successfully."));
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
  String filename = "/index.html";
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
  int dotPosition = filename.lastIndexOf(".");
  String fileType = filename.substring((dotPosition + 1), filename.length());
  if (LittleFS.exists(filename)) {
    File file = LittleFS.open(filename, FILE_READ);
    if (fileType == "gz") {
      fileType = "html";  // no need to change content type as it's done automatically by .streamfile below VV
    }
    server.streamFile(file, "text/" + fileType);
    file.close();
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Served %s", filename.c_str());
  } else if (!LittleFS.exists("/index.html")) {
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
  // checkboxes don't report off, so need to check using another parameter
  // that's always present on that page
  if (!server.arg("autoUpdate").isEmpty()) {
    userConfig->setAutoUpdate(true);
  } else if (wasSettingsUpdate) {
    userConfig->setAutoUpdate(false);
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
      tString    = server.arg("bleHRDropdown");
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
      tString    = server.arg("bleRemoteDropdown");
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
        "= 'http://" +
        myIP.toString() + "/bluetoothscanner.html';\",1000);</script></html>";
  } else if (wasSettingsUpdate) {  // Special Settings Page update response
    response +=
        "Network settings will be applied at next reboot. <br> Everything "
        "else is available immediately.</h2></body><script> "
        "setTimeout(\"location.href = 'http://" +
        myIP.toString() + "/settings.html';\",1000);</script></html>";
  } else {  // Normal response
    response +=
        "Network settings will be applied at next reboot. <br> Everything "
        "else is available immediately.</h2></body><script> "
        "setTimeout(\"location.href = 'http://" +
        myIP.toString() + "/index.html';\",1000);</script></html>";
  }
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "Config Updated From Web");
  ss2k->saveFlag = true;
  if (reboot) {
    response +=
        "Please wait while your settings are saved and SmartSpin2k reboots.</h2></body><script> "
        "setTimeout(\"location.href = 'http://" +
        myIP.toString() + "/bluetoothscanner.html';\",5000);</script></html>";
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

void HTTP_Server::FirmwareUpdate() {
  HTTPClient http;
  WiFiClientSecure localClient;
  localClient.setCACert(rootCACertificate);
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "Checking for newer firmware:");
  http.begin(userConfig->getFirmwareUpdateURL() + String(FW_VERSIONFILE),
             rootCACertificate);  // check version URL
  delay(100);
  int httpCode = http.GET();  // get data from version file
  delay(100);
  String payload;
  if (httpCode == HTTP_CODE_OK) {  // if version received
    payload = http.getString();    // save received version
    payload.trim();
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "  - Server version: %s", payload.c_str());
    httpServer.internetConnection = true;
  } else {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "error downloading %s %d", FW_VERSIONFILE, httpCode);
    httpServer.internetConnection = false;
  }

  http.end();
  if (httpCode == HTTP_CODE_OK) {  // if version received
    bool updateAnyway = false;
    if (!LittleFS.exists("/index.html")) {
      // force firmware update if index.html is missing
      // updateAnyway = true;
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "  -index.html not found.");
    }
    Version availableVer(payload.c_str());
    Version currentVer(FIRMWARE_VERSION);

    if (((availableVer > currentVer) && (userConfig->getAutoUpdate())) || (!LittleFS.exists("/index.html"))) {
      //////////////// Update LittleFS//////////////
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Updating FileSystem");
      http.begin(DATA_UPDATEURL DATA_FILELIST, rootCACertificate);  // check version URL
      delay(100);
      httpCode = http.GET();  // get data from version file
      delay(100);
      JsonDocument doc;
      if (httpCode == HTTP_CODE_OK) {  // if version received
        payload = http.getString();  // save received version
        payload.trim();
        // Deserialize the JSON document
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
          SS2K_LOG(HTTP_SERVER_LOG_TAG, "Failed to read file list");
          http.end();  // Make sure to end HTTP before returning
          return;
        }
        httpServer.internetConnection = true;
      } else {
        SS2K_LOG(HTTP_SERVER_LOG_TAG, "error downloading %s %d", DATA_FILELIST, httpCode);
        httpServer.internetConnection = false;
      }
      // End HTTP connection after file list download
      http.end();

      JsonArray files = doc.as<JsonArray>();
      // iterate through file list and download files individually
      for (JsonVariant v : files) {
        String fileName = "/" + v.as<String>();
        http.begin(DATA_UPDATEURL + fileName,
                   rootCACertificate);  // check version URL
        delay(100);
        httpCode = http.GET();
        delay(100);
        if (httpCode == HTTP_CODE_OK) {
          payload = http.getString();
          payload.trim();
          LittleFS.remove(fileName);
          File file = LittleFS.open(fileName, FILE_WRITE, true);
          if (!file) {
            SS2K_LOG(HTTP_SERVER_LOG_TAG, "Failed to create file, %s", fileName);
            http.end();  // End HTTP before returning
            return;
          }
          file.print(payload);
          file.close();
          SS2K_LOG(HTTP_SERVER_LOG_TAG, "Created: %s", fileName);
          httpServer.internetConnection = true;
        } else {
          SS2K_LOG(HTTP_SERVER_LOG_TAG, "Error downloading %s %d", fileName, httpCode);
          httpServer.internetConnection = false;
        }
        // End HTTP connection after each file download
        http.end();
      }

      //////// Update Firmware /////////
      if (((availableVer > currentVer) || updateAnyway) && (userConfig->getAutoUpdate())) {
        SS2K_LOG(HTTP_SERVER_LOG_TAG, "New firmware detected!");
        SS2K_LOG(HTTP_SERVER_LOG_TAG, "Upgrading from %s to %s", FIRMWARE_VERSION, payload.c_str());
        t_httpUpdate_return ret = httpUpdate.update(localClient, userConfig->getFirmwareUpdateURL() + String(FW_BINFILE));
        switch (ret) {
          case HTTP_UPDATE_FAILED:
            SS2K_LOG(HTTP_SERVER_LOG_TAG, "HTTP_UPDATE_FAILED Error %d : %s", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            break;

          case HTTP_UPDATE_NO_UPDATES:
            SS2K_LOG(HTTP_SERVER_LOG_TAG, "HTTP_UPDATE_NO_UPDATES");
            break;

          case HTTP_UPDATE_OK:
            SS2K_LOG(HTTP_SERVER_LOG_TAG, "HTTP_UPDATE_OK");
            break;
        }
      }
    } else {  // don't update
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "  - Current Version: %s", FIRMWARE_VERSION);
    }
  } else {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Could not connect to Github. httpCode: %d", httpCode);
  }
  // localClient will be automatically destroyed when the function exits
}
