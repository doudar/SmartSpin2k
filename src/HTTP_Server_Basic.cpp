// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

#include "Main.h"
#include "Version_Converter.h"
#include "Builtin_Pages.h"
#include "HTTP_Server_Basic.h"
#include <WebServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <DNSServer.h>

File fsUploadFile;

TaskHandle_t webClientTask;
#define MAX_BUFFER_SIZE 20

// DNS server
const byte DNS_PORT = 53;
DNSServer dnsServer;

//Automatic Firmware update Defines
HTTPClient http;

//********************************WIFI Setup*************************//
void startWifi()
{

  int i = 0;

  //Trying Station mode first:
  debugDirector("Connecting to: " + String(userConfig.getSsid()));
  if (String(WiFi.SSID()) != userConfig.getSsid())
  {
    WiFi.mode(WIFI_STA);
    WiFi.begin(userConfig.getSsid(), userConfig.getPassword());
  }

  while (WiFi.status() != WL_CONNECTED)
  {
    vTaskDelay(1000 / portTICK_RATE_MS);
    debugDirector(".", false);
    i++;
    if (i > WIFI_CONNECT_TIMEOUT)
    {
      i = 0;
      debugDirector("Couldn't Connect. Switching to AP mode");
      WiFi.disconnect();
      WiFi.mode(WIFI_AP);
      break;
    }
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    debugDirector("Connected to " + String(userConfig.getSsid()) + " IP address: " + WiFi.localIP().toString());
  }

  // Couldn't connect to existing network, Create SoftAP
  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.softAP(userConfig.getDeviceName(), userConfig.getPassword());
    vTaskDelay(50);
    IPAddress myIP = WiFi.softAPIP();
    debugDirector("AP IP address: " + myIP.toString());
    /* Setup the DNS server redirecting all the domains to the apIP */
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", myIP);
  }
  MDNS.begin(userConfig.getDeviceName());
  MDNS.addService("http", "tcp", 80);
  debugDirector(String("Open http://") + userConfig.getDeviceName() + ".local/");
}

WebServer server(80);
void startHttpServer()
{
  /********************************************Begin Handlers***********************************/
  server.on("/", handleIndexFile);
  server.on("/index.html", handleIndexFile);
  server.on("/generate_204", handleIndexFile);        //Android captive portal
  server.on("/fwlink", handleIndexFile);              //Microsoft captive portal
  server.on("/hotspot-detect.html", handleIndexFile); //Apple captive portal
  server.on("/style.css", handleSpiffsFile);
  server.on("/btsimulator.html", handleSpiffsFile);
  server.on("/settings.html", handleSpiffsFile);
  server.on("/status.html", handleSpiffsFile);
  server.on("/bluetoothscanner.html", handleSpiffsFile);

  server.on("/send_settings", []() {
    String tString;
    bool wasBTUpdate = false;
    if (!server.arg("ssid").isEmpty())
    {
      tString = server.arg("ssid");
      tString.trim();
      userConfig.setSsid(tString);
    }
    if (!server.arg("password").isEmpty())
    {
      tString = server.arg("password");
      tString.trim();
      userConfig.setPassword(tString);
    }
    if (!server.arg("deviceName").isEmpty())
    {
      tString = server.arg("deviceName");
      tString.trim();
      userConfig.setDeviceName(tString);
    }
    if (!server.arg("shiftStep").isEmpty())
    {
      userConfig.setShiftStep(server.arg("shiftStep").toInt());
    }
    if (!server.arg("stepperPower").isEmpty())
    {
      userConfig.setStepperPower(server.arg("stepperPower").toInt());
      updateStepperPower();
    }
    //checkboxes don't report off, so need to check using another parameter that's always present on that page
    if (!server.arg("stepperPower").isEmpty()) 
    {
      if (!server.arg("autoUpdate").isEmpty())
      {
        userConfig.setAutoUpdate(true);
      }
      else
      {
        userConfig.setAutoUpdate(false);
      }
      if (!server.arg("stealthchop").isEmpty())
      {
        userConfig.setStealthChop(true);
        updateStealthchop();
      }
      else
      {
        userConfig.setStealthChop(false);
        updateStealthchop();
      }
    }
    if (!server.arg("inclineMultiplier").isEmpty())
    {
      userConfig.setInclineMultiplier(server.arg("inclineMultiplier").toFloat());
    }
    if (!server.arg("blePMDropdown").isEmpty())
    {
      wasBTUpdate = true;
      if (server.arg("blePMDropdown"))
      {
        tString = server.arg("blePMDropdown");
        userConfig.setConnectedPowerMeter(server.arg("blePMDropdown"));
      }
      else
      {
        userConfig.setConnectedPowerMeter("any");
      }
    }
    if (!server.arg("bleHRDropdown").isEmpty())
    {
      wasBTUpdate = true;
      if (server.arg("bleHRDropdown"))
      {
        tString = server.arg("bleHRDropdown");
        userConfig.setConnectedHeartMonitor(server.arg("bleHRDropdown"));
      }
      else
      {
        userConfig.setConnectedHeartMonitor("any");
      }
    }
    String response = "<!DOCTYPE html><html><body><h2>";
    if (wasBTUpdate) //Special BT update response
    {
      response += "Selections Saved!</h2></body><script> setTimeout(\"location.href = 'http://" + String(userConfig.getDeviceName()) + ".local/bluetoothscanner.html';\",1000);</script></html>";
    }
    else
    { //Normal response
      response += "Network settings will be applied at next reboot. <br> Everything else is availiable immediatly.</h2></body><script> setTimeout(\"location.href = 'http://" + String(userConfig.getDeviceName()) + ".local/index.html';\",1000);</script></html>";
    }
    server.send(200, "text/html", response);
    debugDirector("Config Updated From Web");
    userConfig.saveToSPIFFS();
    userConfig.printFile();
  });

  server.on("/BLEServers", []() {
    debugDirector("Sending BLE device list to HTTP Client");
    String tString = "";
    if (String(userConfig.getFoundDevices()) == "null")
    {
      tString += "{";
    }
    else
    {
      tString += String(userConfig.getFoundDevices());
      tString.remove(tString.length() - 1, 1);
      tString += ",";
    }

    tString += String("\"connectedHeartMonitor\":\"") + userConfig.getconnectedHeartMonitor() + "\",\"connectedPowerMeter\":\"" + userConfig.getconnectedPowerMeter() + "\"}";
    server.send(200, "text/plain", tString);
  });

  server.on("/BLEScan", []() {
    debugDirector("Scanning for BLE Devices");
    String response = "<!DOCTYPE html><html><body>Scanning for BLE Devices. Please wait 5 seconds.</body><script> setTimeout(\"location.href = 'http://" + String(userConfig.getDeviceName()) + ".local/bluetoothscanner.html';\",5000);</script></html>";
    spinBLEClient.serverScan(true);
    server.send(200, "text/html", response);
  });

  server.on("/load_defaults.html", []() {
    debugDirector("Setting Defaults from Web Request");
    SPIFFS.format();
    userConfig.setDefaults();
    userConfig.saveToSPIFFS();
    String response = "<!DOCTYPE html><html><body><h1>Defaults have been loaded.</h1><p><br><br> Please reconnect to the device on WiFi network: " + String(userConfig.getSsid()) + "</p></body></html>";
    server.send(200, "text/html", response);
    ESP.restart();
  });

  server.on("/reboot.html", []() {
    debugDirector("Rebooting from Web Request");
    String response = "Rebooting....<script> setTimeout(\"location.href = 'http://" + String(userConfig.getDeviceName()) + ".local/index.html';\",500); </script>";
    server.send(200, "text/html", response);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    ESP.restart();
  });

  server.on("/hrslider", []() {
    String value = server.arg("value");
    if (value == "enable")
    {
      userConfig.setSimulateHr(true);
      server.send(200, "text/plain", "OK");
      debugDirector("HR Simulator turned on");
    }
    else if (value == "disable")
    {
      userConfig.setSimulateHr(false);
      server.send(200, "text/plain", "OK");
      debugDirector("HR Simulator turned off");
    }
    else
    {
      userConfig.setSimulatedHr(value.toInt());
      debugDirector("HR is now: " + String(userConfig.getSimulatedHr()));
      server.send(200, "text/plain", "OK");
    }
    debugDirector("Webclient High Water Mark: " + String(uxTaskGetStackHighWaterMark(webClientTask)));
  });

  server.on("/wattsslider", []() {
    String value = server.arg("value");
    if (value == "enable")
    {
      userConfig.setSimulatePower(true);
      server.send(200, "text/plain", "OK");
      debugDirector("Watt Simulator turned on");
    }
    else if (value == "disable")
    {
      userConfig.setSimulatePower(false);
      server.send(200, "text/plain", "OK");
      debugDirector("Watt Simulator turned off");
    }
    else
    {
      userConfig.setSimulatedWatts(value.toInt());
      debugDirector("Watts are now: " + String(userConfig.getSimulatedWatts()));
      server.send(200, "text/plain", "OK");
    }
    debugDirector("Webclient High Water Mark: " + String(uxTaskGetStackHighWaterMark(webClientTask)));
  });

  server.on("/hrValue", []() {
    char outString[MAX_BUFFER_SIZE];
    snprintf(outString, MAX_BUFFER_SIZE, "%d", userConfig.getSimulatedHr());
    server.send(200, "text/plain", outString);
  });

  server.on("/wattsValue", []() {
    char outString[MAX_BUFFER_SIZE];
    snprintf(outString, MAX_BUFFER_SIZE, "%d", userConfig.getSimulatedWatts());
    server.send(200, "text/plain", outString);
  });

  server.on("/configJSON", []() {
    String tString;
    tString = userConfig.returnJSON();
    tString.remove(tString.length() - 1, 1);
    //tString += String(",\"debug\":") + "\"" + debugToHTML + "\"}";
    tString += String(",\"debug\":\"") + debugToHTML + "\",\"firmwareVersion\":\"" + String(FIRMWARE_VERSION) + "\"}";
    server.send(200, "text/plain", tString);
    debugToHTML = " ";
  });

  server.on("/login", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", OTALoginIndex);
  });
  server.on("/OTAIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", OTAServerIndex);
  });
  /*handling uploading firmware file */
  server.on(
      "/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK"); }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.filename == String ("firmware.bin").c_str()){
    if (upload.status == UPLOAD_FILE_START) {
      debugDirector("Update: " + upload.filename);
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
              server.send(200, "text/plain", "Firmware Uploaded Sucessfully. Rebooting...");
              vTaskDelay(2000/portTICK_PERIOD_MS);
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
    debugDirector("handleFileUpload Name: " + filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    //DBG_OUTPUT_PORT.print("handleFileUpload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
    if (fsUploadFile) {
      fsUploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      fsUploadFile.close();
    }
    debugDirector(String("handleFileUpload Size: ") + String(upload.totalSize));
    server.send(200, "text/plain", String(upload.filename + " Uploaded Sucessfully."));
  } } });

  /********************************************End Server Handlers*******************************/

  xTaskCreatePinnedToCore(
      webClientUpdate,   /* Task function. */
      "webClientUpdate", /* name of task. */
      4000,              /* Stack size of task Used to be 3000*/
      NULL,              /* parameter of the task */
      10,                /* priority of the task  - 29 worked*/
      &webClientTask,    /* Task handle to keep track of created task */
      tskNO_AFFINITY);   /* pin task to core 0 */

  server.begin();
  debugDirector("HTTP server started");
}

void webClientUpdate(void *pvParameters)
{
  for (;;)
  {
    server.handleClient();
    vTaskDelay(WEBSERVER_DELAY / portTICK_RATE_MS);
    if (WiFi.getMode() == WIFI_AP)
    {
      dnsServer.processNextRequest();
    }
  }
}

void handleIndexFile()
{
  String filename = "/index.html";
  if (SPIFFS.exists(filename))
  {
    File file = SPIFFS.open(filename, "r");
    server.streamFile(file, "text/html");
    file.close();
  }
  else
  {
    debugDirector(filename + " not found. Sending builtin Index.html");
    server.send(200, "text/html", noIndexHTML);
  }
}

void handleSpiffsFile()
{
  String filename = server.uri();
  int dotPosition = filename.lastIndexOf(".");
  String fileType = filename.substring((dotPosition + 1), filename.length());
  if (SPIFFS.exists(filename))
  {
    File file = SPIFFS.open(filename, "r");
    server.streamFile(file, "text/" + fileType);
    file.close();
    debugDirector("Served " + filename);
  }
  else
  {
    debugDirector(filename + " not found. Sending builtin Index.html");
    server.send(404, "text/html", "<html><body><h1>ERROR 404 <br> FILE NOT FOUND!</h1></body></html>");
  }
}

void FirmwareUpdate()
{
  debugDirector("Checking for newer firmware:");
  http.begin(userConfig.getFirmwareUpdateURL() + String(FW_VERSIONFILE)); // check version URL
  delay(100);
  int httpCode = http.GET(); // get data from version file
  delay(100);
  String payload;
  if (httpCode == HTTP_CODE_OK) // if version received
  {
    payload = http.getString(); // save received version
    payload.trim();
    debugDirector("  -Server Ver " + payload);
  }
  else
  {
    debugDirector("error downloading " + String(FW_VERSIONFILE) + " " + String(httpCode));
  }

  http.end();
  if (httpCode == HTTP_CODE_OK) // if version received
  {
    bool updateAnyway = false;
    if (!SPIFFS.exists("/index.html"))
    {
      updateAnyway = true;
      debugDirector("  -index.html not found. Forcing update");
    }
    Version availiableVer(payload.c_str());
    Version currentVer(FIRMWARE_VERSION);
    if ((availiableVer > currentVer) || (updateAnyway))
    {
      debugDirector("New firmware detected!");
      debugDirector("Upgrading from " + String(FIRMWARE_VERSION) + " to " + payload);
      WiFiClientSecure client;
      httpUpdate.setLedPin(LED_BUILTIN, LOW);
      debugDirector("Updating FileSystem");
      t_httpUpdate_return ret = httpUpdate.updateSpiffs(client, userConfig.getFirmwareUpdateURL() + String(FW_SPIFFSFILE));
      vTaskDelay(100 / portTICK_PERIOD_MS);
      if (ret == HTTP_UPDATE_OK)
      {
        debugDirector("Saving Config.txt");
        userConfig.saveToSPIFFS();
        debugDirector("Updating Program");
        ret = httpUpdate.update(client, userConfig.getFirmwareUpdateURL() + String(FW_BINFILE));
        switch (ret)
        {
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
      }
    }
    else //don't update
    {
      debugDirector("  -Current ver " + String(FIRMWARE_VERSION));
    }
  }
}
