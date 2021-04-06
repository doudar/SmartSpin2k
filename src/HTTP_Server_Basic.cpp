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
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

File fsUploadFile;

TaskHandle_t webClientTask;
#define MAX_BUFFER_SIZE 20

IPAddress myIP;
bool internetConnection = false;

// DNS server
const byte DNS_PORT = 53;
DNSServer dnsServer;

WiFiClientSecure client;
AsyncWebServer server(80);

bool gRebootTriggered  = false;
bool gBleScanTriggered = false;

#ifdef USE_TELEGRAM
#include <UniversalTelegramBot.h>
TaskHandle_t telegramTask;
bool telegramMessageWaiting = false;
UniversalTelegramBot bot(TELEGRAM_TOKEN, client);
String telegramMessage = "";
#endif

// Forward declarations
void settingsProcessor(AsyncWebServerRequest *request);
void doUpdate(size_t index, uint8_t *data, size_t len, bool final);
void doUpload(String filename, size_t index, uint8_t *data, size_t len, bool final);

// ********************************WIFI Setup*************************
void startWifi() {
  int i = 0;

  // Trying Station mode first:
  debugDirector("Connecting to: " + String(userConfig.getSsid()));
  if (String(WiFi.SSID()) != userConfig.getSsid()) {
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.begin(userConfig.getSsid(), userConfig.getPassword());
  }

  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(1000 / portTICK_RATE_MS);
    debugDirector(".", false);
    i++;
    if (i > WIFI_CONNECT_TIMEOUT || (String(userConfig.getSsid()) == DEVICE_NAME)) {
      i = 0;
      debugDirector("Couldn't Connect. Switching to AP mode");
      WiFi.disconnect();
      WiFi.mode(WIFI_AP);
      break;
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    myIP               = WiFi.localIP();
    internetConnection = true;
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
    debugDirector("Error setting up MDNS responder!");
  }

  MDNS.addService("http", "_tcp", 80);
  MDNS.addServiceTxt("http", "_tcp", "lf", "0");
  debugDirector("Connected to " + String(userConfig.getSsid()) + " IP address: " + myIP.toString(), true, true);
  debugDirector(String("Open http://") + userConfig.getDeviceName() + ".local/");
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  if (WiFi.getMode() == WIFI_STA) {
    Serial.print("Retrieving time: ");
    configTime(0, 0, "pool.ntp.org");  // get UTC time via NTP
    time_t now = time(nullptr);
    while (now < 5 * 3600) {
      Serial.print(".");
      delay(100);
      now = time(nullptr);
    }
    Serial.println(now);
  }
}

bool filterSpiffsEmpty(AsyncWebServerRequest *request) {
  // No index.html, probably no file system
  return !SPIFFS.exists("/index.html");
}

void startHttpServer() {
  // Move these requests to a single endpoint
  server.rewrite("/generate_204", "/index.html");         // Android captive portal
  server.rewrite("/gen_204", "/index.html");              // Android captive portal
  server.rewrite("/fwlink", "/index.html");               // Microsoft captive portal
  server.rewrite("/hotspot-detect.html", "/index.html");  // Apple captive portal

  server
      .on("/index.html",
          [](AsyncWebServerRequest *request) {
            // If there's no SPIFFS filesystem can't serve /index.html. Tell user to flash a filesystem
            request->send(200, "text/html", noIndexHTML);
          })
      .setFilter(filterSpiffsEmpty);

  // Serve other resources
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("/index.html");

  server.on("/send_settings", [](AsyncWebServerRequest *request) { settingsProcessor(request); });

  server.on("/BLEScan", [](AsyncWebServerRequest *request) {
    debugDirector("Scanning from web request");
    String response =
        "<!DOCTYPE html><html><body>Scanning for BLE Devices. Please wait "
        "15 seconds.</body><script> setTimeout(\"location.href = 'http://" +
        myIP.toString() + "/bluetoothscanner.html';\",15000);</script></html>";
    gBleScanTriggered = true;
    request->send(200, "text/html", response);
  });

  server.on("/load_defaults.html", [](AsyncWebServerRequest *request) {
    debugDirector("Setting Defaults from Web Request");
    SPIFFS.format();
    userConfig.setDefaults();
    userConfig.saveToSPIFFS();
    String response =
        "<!DOCTYPE html><html><body><h1>Defaults have been "
        "loaded.</h1><p><br><br> Please reconnect to the device on WiFi "
        "network: " +
        myIP.toString() + "</p></body></html>";
    request->send(200, "text/html", response);
    gRebootTriggered = true;
  });

  server.on("/reboot.html", [](AsyncWebServerRequest *request) {
    debugDirector("Rebooting from Web Request");
    String response = "Rebooting....<script> setTimeout(\"location.href = 'http://" + myIP.toString() + "/index.html';\",500); </script>";
    request->send(200, "text/html", response);
    gRebootTriggered = true;
  });

  server.on("/hrslider", [](AsyncWebServerRequest *request) {
    if (!request->hasParam("value")) {
      // Invalid request
      request->send(400);
    }

    String value = request->getParam("value")->value();
    if (value == "enable") {
      userConfig.setSimulateHr(true);
      request->send(200, "text/plain", "OK");
      debugDirector("HR Simulator turned on");
    } else if (value == "disable") {
      userConfig.setSimulateHr(false);
      request->send(200, "text/plain", "OK");
      debugDirector("HR Simulator turned off");
    } else {
      userConfig.setSimulatedHr(value.toInt());
      debugDirector("HR is now: " + String(userConfig.getSimulatedHr()));
      request->send(200, "text/plain", "OK");
    }
  });

  server.on("/wattsslider", [](AsyncWebServerRequest *request) {
    if (!request->hasParam("value")) {
      // Invalid request
      request->send(400);
    }

    String value = request->getParam("value")->value();
    if (value == "enable") {
      userConfig.setSimulateWatts(true);
      request->send(200, "text/plain", "OK");
      debugDirector("Watt Simulator turned on");
    } else if (value == "disable") {
      userConfig.setSimulateWatts(false);
      request->send(200, "text/plain", "OK");
      debugDirector("Watt Simulator turned off");
    } else {
      userConfig.setSimulatedWatts(value.toInt());
      debugDirector("Watts are now: " + String(userConfig.getSimulatedWatts()));
      request->send(200, "text/plain", "OK");
    }
  });

  server.on("/hrValue", [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("text/plain");
    response->printf("%d", userConfig.getSimulatedHr());
    request->send(response);
  });

  server.on("/wattsValue", [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("text/plain");
    response->printf("%d", userConfig.getSimulatedWatts());
    request->send(response);
  });

  server.on("/configJSON", [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonObject doc                = userConfig.json();
    doc["debug"]                  = debugToHTML;
    doc["firmwareVersion"]        = String(FIRMWARE_VERSION);
    serializeJson(doc, *response);
    request->send(response);
    // Clear cached debug log
    debugToHTML = " ";
  });

  server.on("/PWCJSON", [](AsyncWebServerRequest *request) {
    String tString;
    tString = userPWC.returnJSON();
    request->send(200, "text/plain", tString);
    debugToHTML = " ";
  });

  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", OTALoginIndex);
    response->addHeader("Connection", "close");
    request->send(response);
  });

  server.on("/OTAIndex", HTTP_GET, [](AsyncWebServerRequest *request) {
    spinBLEClient.disconnect();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", OTAServerIndex);
    response->addHeader("Connection", "close");
    request->send(response);
  });

  /*handling uploading firmware file */
  server.on(
      "/update", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        response->addHeader("Connection", "close");
        request->send(response);
      },
      [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (filename == String("firmware.bin")) {
          doUpdate(index, data, len, final);
          if (final) {
            request->send(200, "text/plain", "Firmware Uploaded Sucessfully. Rebooting...");
            // Do the reboot in the task loop rather than here so the response makes it out
            gRebootTriggered = true;
          }
        } else {
          // TODO Move this to a separate endpoint
          doUpload(filename, index, data, len, final);
          if (final) {
            request->send(200, "text/plain", String(filename + " Uploaded Sucessfully."));
          }
        }
      });

  server.onNotFound([](AsyncWebServerRequest *request) {
    debugDirector("Link Not Found: " + request->url());
    request->send(404);
  });

  xTaskCreatePinnedToCore(webClientUpdate,   /* Task function. */
                          "webClientUpdate", /* name of task. */
                          4500,              /* Stack size of task Used to be 3000*/
                          NULL,              /* parameter of the task */
                          1,                 /* priority of the task  - 29 worked*/
                          &webClientTask,    /* Task handle to keep track of created task */
                          1);                /* pin task to core 1 */

#ifdef USE_TELEGRAM
  xTaskCreatePinnedToCore(telegramUpdate,   /* Task function. */
                          "telegramUpdate", /* name of task. */
                          4900,             /* Stack size of task*/
                          NULL,             /* parameter of the task */
                          1,                /* priority of the task  - higher number is higher priority*/
                          &telegramTask,    /* Task handle to keep track of created task */
                          1);               /* pin task to core 1 */
#endif

  server.begin();
  debugDirector("HTTP server started");
}

void doUpdate(size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    debugDirector("Update Start");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  }
  if (!Update.hasError()) {
    if (Update.write(data, len) != len) {
      Update.printError(Serial);
    }
  }
  if (final) {
    if (Update.end(true)) {
      debugDirector("Update Success");
    } else {
      Update.printError(Serial);
    }
  }
}

void doUpload(String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    debugDirector("Upload Name: " + filename);
    fsUploadFile = SPIFFS.open(filename, "w");
  }
  for (size_t i = 0; i < len; i++) {
    if (fsUploadFile) {
      fsUploadFile.write(data, len);
    }
  }
  if (final) {
    if (fsUploadFile) {
      fsUploadFile.close();
    }
    debugDirector("Upload completed: " + filename);
  }
}

/********************************************End Server
 * Handlers*******************************/

void webClientUpdate(void *pvParameters) {
  static unsigned long mDnsTimer = millis();  // NOLINT: There is no overload in String for uint64_t
  for (;;) {
    // Successful updates will trigger a reboot
    if (gRebootTriggered) {
      vTaskDelay(2000 / portTICK_PERIOD_MS);
      ESP.restart();
    }

    if (gBleScanTriggered) {
      spinBLEClient.resetDevices();
      spinBLEClient.serverScan(true);
      gBleScanTriggered = false;
    }

    vTaskDelay(WEBSERVER_DELAY / portTICK_RATE_MS);
    if (WiFi.getMode() == WIFI_AP) {
      dnsServer.processNextRequest();
    }
    // Keep MDNS alive
    if ((millis() - mDnsTimer) > 60000) {
      MDNS.addServiceTxt("http", "_tcp", "lf", String(mDnsTimer));
      mDnsTimer = millis();
    }
  }
}

void settingsProcessor(AsyncWebServerRequest *request) {
  String tString;
  bool wasBTUpdate = false;
  if (request->hasParam("ssid")) {
    tString = request->getParam("ssid")->value();
    tString.trim();
    userConfig.setSsid(tString);
  }
  if (request->hasParam("password")) {
    tString = request->getParam("password")->value();
    tString.trim();
    userConfig.setPassword(tString);
  }
  if (request->hasParam("deviceName")) {
    tString = request->getParam("deviceName")->value();
    tString.trim();
    userConfig.setDeviceName(tString);
  }
  if (request->hasParam("shiftStep")) {
    uint64_t shiftStep = request->getParam("shiftStep")->value().toInt();
    if (shiftStep >= 50 && shiftStep <= 6000) {
      userConfig.setShiftStep(shiftStep);
    }
  }
  if (request->hasParam("stepperPower")) {
    uint64_t stepperPower = request->getParam("stepperPower")->value().toInt();
    if (stepperPower >= 500 && stepperPower <= 2000) {
      userConfig.setStepperPower(stepperPower);
      updateStepperPower();
    }
  }
  // checkboxes don't report off, so need to check using another parameter
  // that's always present on that page
  if (request->hasParam("stepperPower")) {
    if (request->hasParam("autoUpdate")) {
      userConfig.setAutoUpdate(true);
    } else {
      userConfig.setAutoUpdate(false);
    }
    if (request->hasParam("stealthchop")) {
      userConfig.setStealthChop(true);
      updateStealthchop();
    } else {
      userConfig.setStealthChop(false);
      updateStealthchop();
    }
  }
  if (request->hasParam("inclineMultiplier")) {
    float inclineMultiplier = request->getParam("inclineMultiplier")->value().toFloat();
    if (inclineMultiplier >= 1 && inclineMultiplier <= 5) {
      userConfig.setInclineMultiplier(inclineMultiplier);
    }
  }
  if (request->hasParam("powerCorrectionFactor")) {
    float powerCorrectionFactor = request->getParam("powerCorrectionFactor")->value().toFloat();
    if (powerCorrectionFactor >= 0 && powerCorrectionFactor <= 2) {
      userConfig.setPowerCorrectionFactor(powerCorrectionFactor);
    }
  }
  if (request->hasParam("blePMDropdown")) {
    wasBTUpdate = true;
    if (request->getParam("blePMDropdown")->value()) {
      tString = request->getParam("blePMDropdown")->value();
      userConfig.setConnectedPowerMeter(request->getParam("blePMDropdown")->value());
    } else {
      userConfig.setConnectedPowerMeter("any");
    }
  }
  if (request->hasParam("bleHRDropdown")) {
    wasBTUpdate = true;
    if (request->getParam("bleHRDropdown")->value()) {
      tString = request->getParam("bleHRDropdown")->value();
      userConfig.setConnectedHeartMonitor(request->getParam("bleHRDropdown")->value());
    } else {
      userConfig.setConnectedHeartMonitor("any");
    }
  }

  if (request->hasParam("session1HR")) {  // Needs checking for unrealistic numbers.
    userPWC.session1HR = request->getParam("session1HR")->value().toInt();
  }
  if (request->hasParam("session1Pwr")) {
    userPWC.session1Pwr = request->getParam("session1Pwr")->value().toInt();
  }
  if (request->hasParam("session2HR")) {
    userPWC.session2HR = request->getParam("session2HR")->value().toInt();
  }
  if (request->hasParam("session2Pwr")) {
    userPWC.session2Pwr = request->getParam("session2Pwr")->value().toInt();
    if (request->hasParam("hr2Pwr")) {
      userPWC.hr2Pwr = true;
    } else {
      userPWC.hr2Pwr = false;
    }
  }
  String response = "<!DOCTYPE html><html><body><h2>";

  if (wasBTUpdate) {  // Special BT update response
    response +=
        "Selections Saved!</h2></body><script> setTimeout(\"location.href "
        "= 'http://" +
        myIP.toString() + "/bluetoothscanner.html';\",1000);</script></html>";
    gBleScanTriggered = true;
  } else {  // Normal response
    response +=
        "Network settings will be applied at next reboot. <br> Everything "
        "else is availiable immediatly.</h2></body><script> "
        "setTimeout(\"location.href = 'http://" +
        myIP.toString() + "/index.html';\",1000);</script></html>";
  }
  request->send(200, "text/html", response);
  debugDirector("Config Updated From Web");
  userConfig.saveToSPIFFS();
  userConfig.printFile();
  userPWC.saveToSPIFFS();
  userPWC.printFile();
}

// github fingerprint
// 70:94:DE:DD:E6:C4:69:48:3A:92:70:A1:48:56:78:2D:18:64:E0:B7

void FirmwareUpdate() {
  HTTPClient http;
  // WiFiClientSecure client;

  client.setCACert(rootCACertificate);
  debugDirector("Checking for newer firmware:");
  http.begin(userConfig.getFirmwareUpdateURL() + String(FW_VERSIONFILE),
             rootCACertificate);  // check version URL
  delay(100);
  int httpCode = http.GET();  // get data from version file
  delay(100);
  String payload;
  if (httpCode == HTTP_CODE_OK) {  // if version received
    payload = http.getString();    // save received version
    payload.trim();
    debugDirector("  - Server version: " + payload);
    internetConnection = true;
  } else {
    debugDirector("error downloading " + String(FW_VERSIONFILE) + " " + String(httpCode));
    internetConnection = false;
  }

  http.end();
  if (httpCode == HTTP_CODE_OK) {  // if version received
    bool updateAnyway = false;
    if (!SPIFFS.exists("/index.html")) {
      updateAnyway = true;
      debugDirector("  -index.html not found. Forcing update");
    }
    Version availiableVer(payload.c_str());
    Version currentVer(FIRMWARE_VERSION);

    if ((availiableVer > currentVer) || (updateAnyway)) {
      debugDirector("New firmware detected!");
      debugDirector("Upgrading from " + String(FIRMWARE_VERSION) + " to " + payload);

      // Update Spiffs
      httpUpdate.setLedPin(LED_BUILTIN, LOW);
      debugDirector("Updating FileSystem");
      t_httpUpdate_return ret = httpUpdate.updateSpiffs(client, userConfig.getFirmwareUpdateURL() + String(FW_SPIFFSFILE));
      vTaskDelay(100 / portTICK_PERIOD_MS);
      switch (ret) {
        case HTTP_UPDATE_OK:
          debugDirector("Saving Config.txt");
          userConfig.saveToSPIFFS();
          userPWC.saveToSPIFFS();
          debugDirector("Updating Program");
          break;

        case HTTP_UPDATE_NO_UPDATES:
          debugDirector("HTTP_UPDATE_NO_UPDATES");
          break;

        case HTTP_UPDATE_FAILED:
          debugDirector("SPIFFS Update Failed: " + String(httpUpdate.getLastError()) + " : " + httpUpdate.getLastErrorString());
          break;
      }

      // Update Firmware
      ret = httpUpdate.update(client, userConfig.getFirmwareUpdateURL() + String(FW_BINFILE));
      switch (ret) {
        case HTTP_UPDATE_FAILED:
          debugDirector("HTTP_UPDATE_FAILD Error " + String(httpUpdate.getLastError()) + " : " + httpUpdate.getLastErrorString());
          break;

        case HTTP_UPDATE_NO_UPDATES:
          debugDirector("HTTP_UPDATE_NO_UPDATES");
          break;

        case HTTP_UPDATE_OK:
          debugDirector("HTTP_UPDATE_OK");
          break;
      }
    } else {  // don't update
      debugDirector("  - Current Version: " + String(FIRMWARE_VERSION));
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
  client.setInsecure();
  for (;;) {
    static int telegramFailures = 0;
    if (telegramMessageWaiting && internetConnection) {
      telegramMessageWaiting = false;
      bool rm                = (bot.sendMessage(TELEGRAM_CHAT_ID, telegramMessage, ""));
      if (!rm) {
        telegramFailures++;
        debugDirector("Telegram failed to send!", +TELEGRAM_CHAT_ID);
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
#endif
    vTaskDelay(4000 / portTICK_RATE_MS);
  }
}
#endif
