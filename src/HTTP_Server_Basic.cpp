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
#include <WebServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

File fsUploadFile;

TaskHandle_t webClientTask;

IPAddress myIP;

// DNS server
const byte DNS_PORT = 53;
DNSServer dnsServer;
HTTP_Server httpServer;
WiFiClientSecure client;
WebServer server(80);

// WiFi Scanner
int _numNetworks    = 0;
int _minimumQuality = -1;
bool haveScan       = false;

#ifdef USE_TELEGRAM
#include <UniversalTelegramBot.h>
TaskHandle_t telegramTask;
bool telegramMessageWaiting = false;
UniversalTelegramBot bot(TELEGRAM_TOKEN, client);
String telegramMessage = "";
#endif  // USE_TELEGRAM

// ********************************WIFI Setup*************************
void startWifi() {
  int i = 0;

  // Trying Station mode first:
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "Connecting to: %s", userConfig.getSsid());
  if (String(WiFi.SSID()) != userConfig.getSsid()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(userConfig.getSsid(), userConfig.getPassword());
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.setAutoReconnect(true);
  }

  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(1000 / portTICK_RATE_MS);
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Waiting for connection to be established...");
    i++;
    if (i > WIFI_CONNECT_TIMEOUT || (String(userConfig.getSsid()) == DEVICE_NAME)) {
      i = 0;
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Couldn't Connect. Switching to AP mode");
      WiFi.disconnect();
      WiFi.mode(WIFI_AP);
      break;
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    myIP                          = WiFi.localIP();
    httpServer.internetConnection = true;
  }

  // Couldn't connect to existing network, Create SoftAP
  if (WiFi.status() != WL_CONNECTED) {
    String t_pass = DEFAULT_PASSWORD;
    if (String(userConfig.getSsid()) == DEVICE_NAME) {
      // If default SSID is still in use, let the user
      // select a new password.
      // Else Fall Back to the default password (probably "password")
      String t_pass = String(userConfig.getPassword());
    }
    WiFi.softAP(userConfig.getDeviceName(), t_pass.c_str());
    vTaskDelay(50);
    myIP = WiFi.softAPIP();
    /* Setup the DNS server redirecting all the domains to the apIP */
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", myIP);
  }

  if (!MDNS.begin(userConfig.getDeviceName())) {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Error setting up MDNS responder!");
  }

  MDNS.addService("http", "_tcp", 80);
  MDNS.addServiceTxt("http", "_tcp", "lf", "0");
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "Connected to %s IP address: %s", userConfig.getSsid(), myIP.toString().c_str());
#ifdef USE_TELEGRAM
  SEND_TO_TELEGRAM("Connected to " + String(userConfig.getSsid()) + " IP address: " + myIP.toString());
#endif
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "Open http://%s.local/", userConfig.getDeviceName());
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  if (WiFi.getMode() == WIFI_STA) {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Syncing clock...");
    configTime(0, 0, "pool.ntp.org");  // get UTC time via NTP
    time_t now = time(nullptr);
    while (now < 10) {  // wait 10 seconds
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Waiting for clock sync...");
      delay(100);
      now = time(nullptr);
    }
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Clock synced to: %.f", difftime(now, (time_t)0));
  }
}

void stopWifi() {
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "Closing connection to: %s", userConfig.getSsid());
  WiFi.disconnect();
}

void HTTP_Server::start() {
  server.enableCORS(true);
  server.onNotFound([]() { SS2K_LOG(HTTP_SERVER_LOG_TAG, "Link Not Found: %s", server.uri().c_str()); });

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
  server.on("/bluetoothscanner.html", handleLittleFSFile);
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
        myIP.toString() + "/bluetoothscanner.html';\",100);</script></html>";
    spinBLEClient.scanProcess(DEFAULT_SCAN_DURATION);
    server.send(200, "text/html", response);
  });

  server.on("/load_defaults.html", []() {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Setting Defaults from Web Request");
    LittleFS.format();
    userConfig.setDefaults();
    userConfig.saveToLittleFS();
    String response =
        "<!DOCTYPE html><html><body><h1>Defaults have been "
        "loaded.</h1><p><br><br> Please reconnect to the device on WiFi "
        "network: " +
        myIP.toString() + "</p></body></html>";
    server.send(200, "text/html", response);
    ESP.restart();
  });

  server.on("/reboot.html", []() {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Rebooting from Web Request");
    String response = "Rebooting....<script> setTimeout(\"location.href = 'http://" + myIP.toString() + "/index.html';\",500); </script>";
    server.send(200, "text/html", response);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    ESP.restart();
  });

  server.on("/hrslider", []() {
    String value = server.arg("value");
    if (value == "enable") {
      rtConfig.hr.setSimulate(true);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "HR Simulator turned on");
    } else if (value == "disable") {
      rtConfig.hr.setSimulate(false);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "HR Simulator turned off");
    } else {
      rtConfig.hr.setValue(value.toInt());
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "HR is now: %d", rtConfig.hr.getValue());
      server.send(200, "text/plain", "OK");
    }
  });

  server.on("/wattsslider", []() {
    String value = server.arg("value");
    if (value == "enable") {
      rtConfig.watts.setSimulate(true);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Watt Simulator turned on");
    } else if (value == "disable") {
      rtConfig.watts.setSimulate(false);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Watt Simulator turned off");
    } else {
      rtConfig.watts.setValue(value.toInt());
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Watts are now: %d", rtConfig.watts.getValue());
      server.send(200, "text/plain", "OK");
    }
  });

  server.on("/cadslider", []() {
    String value = server.arg("value");
    if (value == "enable") {
      rtConfig.cad.setSimulate(true);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "CAD Simulator turned on");
    } else if (value == "disable") {
      rtConfig.cad.setSimulate(false);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "CAD Simulator turned off");
    } else {
      rtConfig.cad.setValue(value.toInt());
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "CAD is now: %d", rtConfig.cad.getValue());
      server.send(200, "text/plain", "OK");
    }
  });

  server.on("/ergmode", []() {
    String value = server.arg("value");
    if (value == "enable") {
      rtConfig.setFTMSMode(FitnessMachineControlPointProcedure::SetTargetPower);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "ERG Mode turned on");
    } else {
      rtConfig.setFTMSMode(FitnessMachineControlPointProcedure::RequestControl);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "ERG Mode turned off");
    }
  });

  server.on("/targetwattsslider", []() {
    String value = server.arg("value");
    if (value == "enable") {
      rtConfig.setSimTargetWatts(true);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Target Watts Simulator turned on");
    } else if (value == "disable") {
      rtConfig.setSimTargetWatts(false);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Target Watts Simulator turned off");
    } else {
      rtConfig.watts.setTarget(value.toInt());
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Target Watts are now: %d", rtConfig.watts.getTarget());
      server.send(200, "text/plain", "OK");
    }
  });

  server.on("/shift", []() {
    int value = server.arg("value").toInt();
    if ((value > -10) && (value < 10)) {
      rtConfig.setShifterPosition(rtConfig.getShifterPosition() + value);
      server.send(200, "text/plain", "OK");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Shift From HTML");
    } else {
      rtConfig.setShifterPosition(value);
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Invalid HTML Shift");
      server.send(200, "text/plain", "OK");
    }
  });

  server.on("/configJSON", []() {
    String tString;
    tString = userConfig.returnJSON();
    server.send(200, "text/plain", tString);
  });

  server.on("/runtimeConfigJSON", []() {
    String tString;
    tString = rtConfig.returnJSON();
    server.send(200, "text/plain", tString);
  });

  server.on("/PWCJSON", []() {
    String tString;
    tString = userPWC.returnJSON();
    server.send(200, "text/plain", tString);
  });

  server.on("/login", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    String page = FPSTR(OTALOGININDEX);
    page += FPSTR(HTTP_STYLE);
    server.send(200, "text/html", page);
  });

  server.on("/wifi", []() {
    if (!server.arg("refresh").isEmpty()) {
      WiFi_scanNetworks();
    }
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", httpServer.processWiFiHTML());
  });

  server.on("/OTAIndex", HTTP_GET, []() {
    ss2k.stopTasks();
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
              userConfig.saveToLittleFS();
              userPWC.saveToLittleFS();
              vTaskDelay(100);
              ESP.restart();
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

  xTaskCreatePinnedToCore(HTTP_Server::webClientUpdate,       /* Task function. */
                          "webClientUpdate",                  /* name of task. */
                          6000 + (DEBUG_LOG_BUFFER_SIZE * 2), /* Stack size of task Used to be 3000*/
                          NULL,                               /* parameter of the task */
                          4,                                  /* priority of the task */
                          &webClientTask,                     /* Task handle to keep track of created task */
                          0);                                 /* pin task to core */

#ifdef USE_TELEGRAM
  xTaskCreatePinnedToCore(telegramUpdate,   /* Task function. */
                          "telegramUpdate", /* name of task. */
                          4900,             /* Stack size of task*/
                          NULL,             /* parameter of the task */
                          1,                /* priority of the task  - higher number is higher priority*/
                          &telegramTask,    /* Task handle to keep track of created task */
                          1);               /* pin task to core 1 */
#endif                                      // USE_TELEGRAM
  server.begin();
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "HTTP server started");
}

void HTTP_Server::webClientUpdate(void *pvParameters) {
  static unsigned long mDnsTimer = millis();  // NOLINT: There is no overload in String for uint64_t
  bool recheck                   = false;
  for (;;) {
    server.handleClient();
    // vTaskDelay(WEBSERVER_DELAY / portTICK_RATE_MS);
    if (WiFi.getMode() != WIFI_MODE_STA) {
      if (WiFi.softAPgetStationNum()) {  // Client Connected to AP
        httpServer.isServing = true;
      }
      dnsServer.processNextRequest();
      if (!haveScan) {
        WiFi_scanNetworks();
        haveScan = true;
      }
    }
    // Keep MDNS alive
    if ((millis() - mDnsTimer) > 30000) {
      if (httpServer.isServing) {
        if (recheck) {
          httpServer.isServing = false;  // reset BLE communications slowdown.
          recheck              = false;
        } else {
          recheck = true;  // check again in 30 seconds.
        }
      }
      MDNS.addServiceTxt("http", "_tcp", "lf", String(mDnsTimer));
      mDnsTimer = millis();
#ifdef DEBUG_STACK
      Serial.printf("HttpServer: %d \n", uxTaskGetStackHighWaterMark(webClientTask));
#endif  // DEBUG_STACK
    }
  }
}

void HTTP_Server::handleIndexFile() {
  httpServer.isServing = true;
  String filename      = "/index.html";
  server.sendHeader("Connection", "close");
  if (LittleFS.exists(filename)) {
    File file   = LittleFS.open(filename, FILE_READ);
    String page = "";
    page += FPSTR(HTTP_HEAD_START);
    page.replace(FPSTR(T_v), DEVICE_NAME);
    page += FPSTR(HTTP_STYLE);
    while (file.available()) {
      page += char(file.read());
    }
    server.send(200, "text/html", page);
    // server.streamFile(file, "text/html");
    file.close();
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "Served %s", filename.c_str());
  } else {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "%s not found. Sending builtin Index.html", filename.c_str());
    server.send(200, "text/html", httpServer.processWiFiHTML());
  }
}

String HTTP_Server::processWiFiHTML() {
  String page = getHTTPHead();  // @token titlewifi
  if (!haveScan) {
    WiFi_scanNetworks();  // wifiscan, force if arg refresh
  }

  page.replace(FPSTR(T_1), reinterpret_cast<const char *>(WIFI_PAGE_TITLE));
  page += getScanItemOut();

  String pitem = "";

  pitem = FPSTR(HTTP_FORM_START);
  pitem.replace(FPSTR(T_v), F("send_settings"));  // set form action
  page += pitem;

  pitem = FPSTR(HTTP_FORM_WIFI);
  pitem.replace(FPSTR(T_v), reinterpret_cast<const char *>(userConfig.getSsid()));

  pitem.replace(FPSTR(T_p), reinterpret_cast<const char *>(DEFAULT_PASSWORD));

  page += pitem;

  page += FPSTR(HTTP_BR);
  page += FPSTR(HTTP_FORM_WIFI_END);
  page += FPSTR(HTTP_FORM_END);
  page += FPSTR(HTTP_SCAN_LINK);
  if (!LittleFS.exists("/index.html")) {
    page += FPSTR(NOINDEXHTML);
  }
  page += FPSTR(HTTP_REBOOT_LINK);

  page += FPSTR(HTTP_END);

  return page;
}

bool WiFi_scanNetworks() {
  // if 0 networks, rescan @note this was a kludge, now disabling to test real cause ( maybe wifi not init etc)
  // enable only if preload failed?
  if (_numNetworks == 0) {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "NO APs found forcing new scan");
  }

  int8_t res;

  SS2K_LOG(HTTP_SERVER_LOG_TAG, "WiFi Scan SYNC started");
  res = WiFi.scanNetworks();

  if (res == WIFI_SCAN_FAILED) {
    SS2K_LOG(HTTP_SERVER_LOG_TAG, "WiFi Scan failed");
  } else if (res == WIFI_SCAN_RUNNING) {
    while (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
      Serial.print(".");
      delay(100);
    }
    _numNetworks = WiFi.scanComplete();
  } else if (res >= 0) {
    _numNetworks = res;
    haveScan     = true;
  }

  if (res > 0) {
    // Serial.printf("Networks found: %d", _numNetworks);
    return true;
  } else {
    return false;
  }
}

String getHTTPHead() {
  String page;
  page += FPSTR(HTTP_HEAD_START);
  page.replace(FPSTR(T_v), DEVICE_NAME);
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page += FPSTR(HTTP_TITLE);
  page += FPSTR(HTTP_HEAD_END);
  return page;
}

String getScanItemOut() {
  String page;

  if (!_numNetworks) WiFi_scanNetworks();  // scan in case this gets called before any scans

  int n = _numNetworks;
  if (n == 0) {
#ifdef WM_DEBUG_LEVEL
    DEBUG_WM(F("No networks found"));
#endif
    page += FPSTR(S_nonetworks);  // @token nonetworks
    page += F("<br/><br/>");
  } else {
#ifdef WM_DEBUG_LEVEL
    DEBUG_WM(n, F("networks found"));
#endif
    // sort networks
    int indices[n];
    for (int i = 0; i < n; i++) {
      indices[i] = i;
    }

    // RSSI SORT
    for (int i = 0; i < n; i++) {
      for (int j = i + 1; j < n; j++) {
        if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
          std::swap(indices[i], indices[j]);
        }
      }
    }

    /* test std:sort
      std::sort(indices, indices + n, [](const int & a, const int & b) -> bool
      {
      return WiFi.RSSI(a) > WiFi.RSSI(b);
      });
     */

    // remove duplicates ( must be RSSI sorted )
    String cssid;
    for (int i = 0; i < n; i++) {
      if (indices[i] == -1) continue;
      cssid = WiFi.SSID(indices[i]);
      for (int j = i + 1; j < n; j++) {
        if (cssid == WiFi.SSID(indices[j])) {
#ifdef WM_DEBUG_LEVEL
          DEBUG_WM(WM_DEBUG_VERBOSE, F("DUP AP:"), WiFi.SSID(indices[j]));
#endif
          indices[j] = -1;  // set dup aps to index -1
        }
      }
    }

    
    // token precheck, to speed up replacements on large ap lists
    String HTTP_ITEM_STR = FPSTR(HTTP_ITEM);

    // toggle icons with percentage
    // HTTP_ITEM_STR.replace("{qp}", FPSTR(HTTP_ITEM_QP));
    HTTP_ITEM_STR.replace("{qi}", FPSTR(HTTP_ITEM_QI));

    // set token precheck flags
    bool tok_r = HTTP_ITEM_STR.indexOf(FPSTR(T_r)) > 0;
    bool tok_R = HTTP_ITEM_STR.indexOf(FPSTR(T_R)) > 0;
    bool tok_e = HTTP_ITEM_STR.indexOf(FPSTR(T_e)) > 0;
    bool tok_q = HTTP_ITEM_STR.indexOf(FPSTR(T_q)) > 0;
    bool tok_i = HTTP_ITEM_STR.indexOf(FPSTR(T_i)) > 0;

    // display networks in page
    for (int i = 0; i < n; i++) {
      if (indices[i] == -1) continue;  // skip dups

#ifdef WM_DEBUG_LEVEL
      DEBUG_WM(WM_DEBUG_VERBOSE, F("AP: "), (String)WiFi.RSSI(indices[i]) + " " + (String)WiFi.SSID(indices[i]));
#endif

      int rssiperc     = getRSSIasQuality(WiFi.RSSI(indices[i]));
      uint8_t enc_type = WiFi.encryptionType(indices[i]);

      if (_minimumQuality == -1 || _minimumQuality < rssiperc) {
        String item = HTTP_ITEM_STR;
        if (WiFi.SSID(indices[i]) == "") {
          // Serial.println(WiFi.BSSIDstr(indices[i]));
          continue;  // No idea why I am seeing these, lets just skip them for now
        }
        item.replace(FPSTR(T_V), htmlEntities(WiFi.SSID(indices[i])));        // ssid no encoding
        item.replace(FPSTR(T_v), htmlEntities(WiFi.SSID(indices[i]), true));  // ssid no encoding
        if (tok_e) item.replace(FPSTR(T_e), AUTH_MODE_NAMES[enc_type]);
        if (tok_r) item.replace(FPSTR(T_r), (String)rssiperc);                                  // rssi percentage 0-100
        if (tok_R) item.replace(FPSTR(T_R), (String)WiFi.RSSI(indices[i]));                     // rssi db
        if (tok_q) item.replace(FPSTR(T_q), (String) int(round(map(rssiperc, 0, 100, 1, 4))));  // quality icon 1-4
        if (tok_i) {
          if (enc_type != WIFI_AUTH_OPEN) {
            item.replace(FPSTR(T_i), F("l"));
          } else {
            item.replace(FPSTR(T_i), "");
          }
        }
        // Serial.println(WiFi.SSID(indices[i]));
        page += item;
        delay(0);
      } else {
#ifdef WM_DEBUG_LEVEL
        DEBUG_WM(WM_DEBUG_VERBOSE, F("Skipping , does not meet _minimumQuality"));
#endif
      }
    }
    page += FPSTR(HTTP_BR);
  }

  return page;
}

int getRSSIasQuality(int RSSI) {
  int quality = 0;
  if (RSSI <= -100) {
    quality = 0;
  } else if (RSSI >= -50) {
    quality = 100;
  } else {
    quality = 2 * (RSSI + 100);
  }
  Serial.println(quality);
  return quality;
}

String htmlEntities(String str, bool whitespace) {
  str.replace("&", "&amp;");
  str.replace("<", "&lt;");
  str.replace(">", "&gt;");
  str.replace("'", "&#39;");
  if (whitespace) str.replace(" ", "&#160;");
  return str;
}

void HTTP_Server::handleLittleFSFile() {
  server.sendHeader("Connection", "close");
  httpServer.isServing = true;
  String filename      = server.uri();
  int dotPosition      = filename.lastIndexOf(".");
  String fileType      = filename.substring((dotPosition + 1), filename.length());
  if (LittleFS.exists(filename)) {
    File file = LittleFS.open(filename, FILE_READ);

    if (fileType == "ico") {  // send favicon.ico
      server.streamFile(file, "image/png");
    } else {
      String page = "";
      page += FPSTR(HTTP_HEAD_START);
      page.replace(FPSTR(T_v), DEVICE_NAME);
      page += FPSTR(HTTP_STYLE);
      while (file.available()) {
        page += char(file.read());
      }
      server.send(200, "text/html", page);
    }
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
    if (tString != userConfig.getSsid()) {
      reboot = true;
    }
    userConfig.setSsid(tString);
  }
  if (!server.arg("password").isEmpty()) {
    tString = server.arg("password");
    tString.trim();
    if (tString != userConfig.getPassword()) {
      reboot = true;
    }
    userConfig.setPassword(tString);
  }
  if (!server.arg("deviceName").isEmpty()) {
    tString = server.arg("deviceName");
    tString.trim();
    userConfig.setDeviceName(tString);
  }
  if (!server.arg("shiftStep").isEmpty()) {
    uint64_t shiftStep = server.arg("shiftStep").toInt();
    if (shiftStep >= 50 && shiftStep <= 6000) {
      userConfig.setShiftStep(shiftStep);
    }
    wasSettingsUpdate = true;
  }
  if (!server.arg("stepperPower").isEmpty()) {
    uint64_t stepperPower = server.arg("stepperPower").toInt();
    if (stepperPower >= 500 && stepperPower <= 2000) {
      userConfig.setStepperPower(stepperPower);
      ss2k.updateStepperPower();
    }
  }
  if (!server.arg("maxWatts").isEmpty()) {
    uint64_t maxWatts = server.arg("maxWatts").toInt();
    if (maxWatts >= 0 && maxWatts <= 2000) {
      userConfig.setMaxWatts(maxWatts);
    }
  }
  if (!server.arg("minWatts").isEmpty()) {
    uint64_t minWatts = server.arg("minWatts").toInt();
    if (minWatts >= 0 && minWatts <= 200) {
      userConfig.setMinWatts(minWatts);
    }
  }
  if (!server.arg("ERGSensitivity").isEmpty()) {
    float ERGSensitivity = server.arg("ERGSensitivity").toFloat();
    if (ERGSensitivity >= .5 && ERGSensitivity <= 20) {
      userConfig.setERGSensitivity(ERGSensitivity);
    }
  }
  // checkboxes don't report off, so need to check using another parameter
  // that's always present on that page
  if (!server.arg("autoUpdate").isEmpty()) {
    userConfig.setAutoUpdate(true);
  } else if (wasSettingsUpdate) {
    userConfig.setAutoUpdate(false);
  }
  if (!server.arg("stepperDir").isEmpty()) {
    userConfig.setStepperDir(true);
  } else if (wasSettingsUpdate) {
    userConfig.setStepperDir(false);
  }
  if (!server.arg("shifterDir").isEmpty()) {
    userConfig.setShifterDir(true);
  } else if (wasSettingsUpdate) {
    userConfig.setShifterDir(false);
  }
  if (!server.arg("udpLogEnabled").isEmpty()) {
    userConfig.setUdpLogEnabled(true);
  } else if (wasSettingsUpdate) {
    userConfig.setUdpLogEnabled(false);
  }
  if (!server.arg("logComm").isEmpty()) {
    userConfig.setLogComm(true);
  } else if (wasSettingsUpdate) {
    userConfig.setLogComm(false);
  }
  if (!server.arg("stealthChop").isEmpty()) {
    userConfig.setStealthChop(true);
    ss2k.updateStealthChop();
  } else if (wasSettingsUpdate) {
    userConfig.setStealthChop(false);
    ss2k.updateStealthChop();
  }
  if (!server.arg("inclineMultiplier").isEmpty()) {
    float inclineMultiplier = server.arg("inclineMultiplier").toFloat();
    if (inclineMultiplier >= 1 && inclineMultiplier <= 10) {
      userConfig.setInclineMultiplier(inclineMultiplier);
    }
  }
  if (!server.arg("powerCorrectionFactor").isEmpty()) {
    float powerCorrectionFactor = server.arg("powerCorrectionFactor").toFloat();
    if (powerCorrectionFactor >= MIN_PCF && powerCorrectionFactor <= MAX_PCF) {
      userConfig.setPowerCorrectionFactor(powerCorrectionFactor);
    }
  }
  if (!server.arg("blePMDropdown").isEmpty()) {
    wasBTUpdate = true;
    if (server.arg("blePMDropdown")) {
      tString = server.arg("blePMDropdown");
      if (tString != userConfig.getConnectedPowerMeter()) {
        userConfig.setConnectedPowerMeter(tString);
        reboot = true;
      }
    } else {
      userConfig.setConnectedPowerMeter("any");
    }
  }
  if (!server.arg("bleHRDropdown").isEmpty()) {
    wasBTUpdate = true;
    if (server.arg("bleHRDropdown")) {
      bool reset = false;
      tString    = server.arg("bleHRDropdown");
      if (tString != userConfig.getConnectedHeartMonitor()) {
        reboot = true;
      }
      userConfig.setConnectedHeartMonitor(server.arg("bleHRDropdown"));
    } else {
      userConfig.setConnectedHeartMonitor("any");
    }
  }
  if (!server.arg("bleRemoteDropdown").isEmpty()) {
    wasBTUpdate = true;
    if (server.arg("bleRemoteDropdown")) {
      bool reset = false;
      tString    = server.arg("bleRemoteDropdown");
      if (tString != userConfig.getConnectedRemote()) {
        reboot = true;
      }
      userConfig.setConnectedRemote(server.arg("bleRemoteDropdown"));
    } else {
      userConfig.setConnectedRemote("any");
    }
  }
  if (!server.arg("session1HR").isEmpty()) {  // Needs checking for unrealistic numbers.
    userPWC.session1HR = server.arg("session1HR").toInt();
  }
  if (!server.arg("session1Pwr").isEmpty()) {
    userPWC.session1Pwr = server.arg("session1Pwr").toInt();
  }
  if (!server.arg("session2HR").isEmpty()) {
    userPWC.session2HR = server.arg("session2HR").toInt();
  }
  if (!server.arg("session2Pwr").isEmpty()) {
    userPWC.session2Pwr = server.arg("session2Pwr").toInt();

    if (!server.arg("hr2Pwr").isEmpty()) {
      userPWC.hr2Pwr = true;
    } else {
      userPWC.hr2Pwr = false;
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
  userConfig.saveToLittleFS();
  userConfig.printFile();
  userPWC.saveToLittleFS();
  userPWC.printFile();
  if (reboot) {
    response +=
        "Please wait while your settings are saved and SmartSpin2k reboots.</h2></body><script> "
        "setTimeout(\"location.href = 'http://" +
        myIP.toString() + "/bluetoothscanner.html';\",5000);</script></html>";
    server.send(200, "text/html", response);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    ESP.restart();
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
  // WiFiClientSecure client;

  client.setCACert(rootCACertificate);
  SS2K_LOG(HTTP_SERVER_LOG_TAG, "Checking for newer firmware:");
  http.begin(userConfig.getFirmwareUpdateURL() + String(FW_VERSIONFILE),
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
      updateAnyway = true;
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "  -index.html not found. Forcing update");
    }
    Version availableVer(payload.c_str());
    Version currentVer(FIRMWARE_VERSION);

    if (((availableVer > currentVer) && (userConfig.getAutoUpdate())) || (updateAnyway)) {
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "New firmware detected!");
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Upgrading from %s to %s", FIRMWARE_VERSION, payload.c_str());

      //////////////// Update LittleFS//////////////
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Updating FileSystem");
      http.begin(DATA_UPDATEURL + String(DATA_FILELIST),
                 rootCACertificate);  // check version URL
      vTaskDelay(100 / portTICK_PERIOD_MS);
      httpCode = http.GET();  // get data from version file
      vTaskDelay(100 / portTICK_PERIOD_MS);
      StaticJsonDocument<500> doc;
      if (httpCode == HTTP_CODE_OK) {  // if version received
        String payload;
        payload = http.getString();  // save received version
        payload.trim();
        // Deserialize the JSON document
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
          SS2K_LOG(HTTP_SERVER_LOG_TAG, "Failed to read file list");
          return;
        }
        httpServer.internetConnection = true;
      } else {
        SS2K_LOG(HTTP_SERVER_LOG_TAG, "error downloading %s %d", DATA_FILELIST, httpCode);
        httpServer.internetConnection = false;
      }
      JsonArray files = doc.as<JsonArray>();
      // iterate through file list and download files individually
      for (JsonVariant v : files) {
        String fileName = "/" + v.as<String>();
        http.begin(DATA_UPDATEURL + fileName,
                   rootCACertificate);  // check version URL
        vTaskDelay(100 / portTICK_PERIOD_MS);
        httpCode = http.GET();
        vTaskDelay(100 / portTICK_PERIOD_MS);
        if (httpCode == HTTP_CODE_OK) {
          String payload;
          payload = http.getString();
          payload.trim();
          LittleFS.remove(fileName);
          File file = LittleFS.open(fileName, FILE_WRITE, true);
          if (!file) {
            SS2K_LOG(HTTP_SERVER_LOG_TAG, "Failed to create file, %s", fileName);
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
      }

      //////// Update Firmware /////////
      SS2K_LOG(HTTP_SERVER_LOG_TAG, "Updating Firmware...Please Wait");
      if (((availableVer > currentVer) || updateAnyway) && (userConfig.getAutoUpdate())) {
        t_httpUpdate_return ret = httpUpdate.update(client, userConfig.getFirmwareUpdateURL() + String(FW_BINFILE));
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
  }
}

#ifdef USE_TELEGRAM
// Function to handle sending telegram text to the non blocking task
void sendTelegram(String textToSend) {
  static int numberOfMessages = 0;
  static uint64_t timeout     = 120000;  // reset every two minutes
  static uint64_t startTime   = millis();

  if (millis() - startTime > timeout) {  // Let one message send every two minutes
    numberOfMessages = MAX_TELEGRAM_MESSAGES - 1;
    telegramMessage += " " + String(userConfig.getSsid()) + " ";
    startTime = millis();
  }

  if ((numberOfMessages < MAX_TELEGRAM_MESSAGES) && (WiFi.getMode() == WIFI_STA)) {
    telegramMessage += "\n" + textToSend;
    telegramMessageWaiting = true;
    numberOfMessages++;
  }
}

// Non blocking task to send telegram message
void telegramUpdate(void *pvParameters) {
  // client.setInsecure();
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  for (;;) {
    static int telegramFailures = 0;
    if (telegramMessageWaiting && internetConnection) {
      telegramMessageWaiting = false;
      bool rm                = (bot.sendMessage(TELEGRAM_CHAT_ID, telegramMessage, ""));
      if (!rm) {
        telegramFailures++;
        SS2K_LOG(HTTP_SERVER_LOG_TAG, "Telegram failed to send! %s", TELEGRAM_CHAT_ID);
        if (telegramFailures > 2) {
          internetConnection = false;
        }
      } else {  // Success - reset Telegram Failures
        telegramFailures = 0;
      }

      client.stop();
      telegramMessage = "";
    }
#ifdef DEBUG_STACK
    Serial.printf("Telegram: %d \n", uxTaskGetStackHighWaterMark(telegramTask));
    Serial.printf("Web: %d \n", uxTaskGetStackHighWaterMark(webClientTask));
    Serial.printf("Free: %d \n", ESP.getFreeHeap());
#endif  // DEBUG_STACK
    vTaskDelay(4000 / portTICK_RATE_MS);
  }
}
#endif  // USE_TELEGRAM
