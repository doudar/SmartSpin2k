// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive


#include <Main.h>
#include <WebServer.h>
#include <HTTP_Server_Basic.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include <Update.h>

File fsUploadFile;

TaskHandle_t webClientTask;
#define MAX_BUFFER_SIZE 20

//Automatic Firmware update Defines
HTTPClient http;
#define URL_fw_Server "https://raw.githubusercontent.com/doudar/OTAUpdates/main/"
#define URL_fw_Version "https://raw.githubusercontent.com/doudar/OTAUpdates/main/version.txt"
#define URL_fw_Bin "https://raw.githubusercontent.com/doudar/OTAUpdates/main/firmware.bin"
#define URL_fw_spiffs "https://raw.githubusercontent.com/doudar/OTAUpdates/main/spiffs.bin"

//OTA Update pages. Not stored in SPIFFS because we will use this to restore the webserver files if they get corrupt.
/* Style */
String OTAStyle =
    "<style>#file-input,input{width:100%;height:44px;border-radius:4px;margin:10px auto;font-size:15px}"
    "input{background:#f1f1f1;border:0;padding:0 15px}body{background:#3498db;font-family:sans-serif;font-size:14px;color:#777}"
    "#file-input{padding:0;border:1px solid #ddd;line-height:44px;text-align:left;display:block;cursor:pointer}"
    "#bar,#prgbar{background-color:#f1f1f1;border-radius:10px}#bar{background-color:#3498db;width:0%;height:10px}"
    "form{background:#fff;max-width:258px;margin:75px auto;padding:30px;border-radius:5px;text-align:center}"
    ".btn{background:#3498db;color:#fff;cursor:pointer}</style>";

/* Login page */
String OTALoginIndex =
    "<form name=loginForm>"
    "<h1>ESP32 Login</h1>"
    "<input name=userid placeholder='User ID'> "
    "<input name=pwd placeholder=Password type=Password> "
    "<input type=submit onclick=check(this.form) class=btn value=Login></form>"
    "<script>"
    "function check(form) {"
    "if(form.userid.value=='admin' && form.pwd.value=='admin')"
    "{window.open('/OTAIndex')}"
    "else"
    "{alert('Error Password or Username')}"
    "}"
    "</script>" +
    OTAStyle;

String noIndexHTML = 
"<!DOCTYPE html>"
"<html>"
"<body>"
"The webserver files need to be updated."
"Please enter the credentials for your network or upload a new filesystem image using the link below."
"<head>"
"  <title>SmartSpin2K Web Server</title>"
"  <meta name='viewport' content='width=device-width, initial-scale=1'>"
" <link rel='icon' href='data:,'>"
"</head>"
"<body>"
"  <h1><strong>Network Settings</strong></h1>"
"  <h2>"
"    <p style='text-align: left;'></p>"
"    <form action='/send_settings'>"
"      <table border='0' style='height: 30px; width: 36.0582%; border-collapse: collapse; border-style: none;'"
"        height='182' cellspacing='1'>"
"        <tbody>"
"          <tr style='height: 20px;'>"
"            <td style='width: 14%; height: 20px;'>"
"              <p><label for='ssid'>SSID</label></p>"
"            </td>"
"            <td style='width: 14%; height: 20px;'><input type='text' id='ssid' name='ssid' value='' /></td>"
"          </tr>"
"          <tr style='height: 20px;'>"
"            <td style='width: 14%; height: 20px;'>"
"              <p><label for='password'>Password</label></p>"
"            </td>"
"            <td style='width: 14%; height: 20px;'><input type='text' id='password' name='password' value='' />"
"            </td>"
"          </tr>"
"          </tr>"
"        </tbody>"
"      </table>"
"      <input type='submit' value='Submit' />"
"    </form>"
"<p style='text-align: center;'><strong><a href='login'>Update Firmware</a></strong></p></h2>"
"<p><br></p>"
"    <form action='/reboot.html'>"
"      <input type='submit' value='Reboot!'>"
"    </form>"
"</body>"
"</html> " + OTAStyle;

/* Server Index Page */
String OTAServerIndex =
    "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
    "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
    "<input type='file' name='update' id='file' onchange='sub(this)' style=display:none>"
    "<label id='file-input' for='file'>   Choose file...</label>"
    "<input type='submit' class=btn value='Update'>"
    "<br><br>"
    "<div id='prg'></div>"
    "<br><div id='prgbar'><div id='bar'></div></div><br></form>"
    "<script>"
    "function sub(obj){"
    "var fileName = obj.value.split('\\\\');"
    "document.getElementById('file-input').innerHTML = '   '+ fileName[fileName.length-1];"
    "};"
    "$('form').submit(function(e){"
    "e.preventDefault();"
    "var form = $('#upload_form')[0];"
    "var data = new FormData(form);"
    "$.ajax({"
    "url: '/update',"
    "type: 'POST',"
    "data: data,"
    "contentType: false,"
    "processData:false,"
    "xhr: function() {"
    "var xhr = new window.XMLHttpRequest();"
    "xhr.upload.addEventListener('progress', function(evt) {"
    "if (evt.lengthComputable) {"
    "var per = evt.loaded / evt.total;"
    "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
    "$('#bar').css('width',Math.round(per*100) + '%');"
    "}"
    "}, false);"
    "return xhr;"
    "},"
    "success:function(d, s) {"
    "console.log('success!') "
    "},"
    "error: function (a, b, c) {"
    "}"
    "});"
    "});"
    "</script>" +
    OTAStyle;

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
    if (i > 5)
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
    WiFi.softAP(userConfig.getSsid(), userConfig.getPassword());
    vTaskDelay(50);
    IPAddress myIP = WiFi.softAPIP();
    debugDirector("AP IP address: " + myIP.toString());
  }
  MDNS.begin("SmartSpin2k"); //<-----------Need to add variable to change this globally
  debugDirector("Open http://SmartSpin2k.local/");
}

WebServer server(80);
void startHttpServer()
{
  /********************************************Begin Handlers***********************************/
  server.on("/", handleIndexFile);
  server.on("/style.css", handleStyleCss);
  server.on("/index.html", handleIndexFile);

  server.on("/btsimulator.html", []() {
    File file = SPIFFS.open("/btsimulator.html", "r");
    server.streamFile(file, "text/html");
    debugDirector("Served: " + String(file.name()));
    file.close();
  });

  server.on("/settings.html", []() {
    File file = SPIFFS.open("/settings.html", "r");
    server.streamFile(file, "text/html");
    debugDirector("Served: " + String(file.name()));
    file.close();
  });

    server.on("/status.html", []() {
    File file = SPIFFS.open("/status.html", "r");
    server.streamFile(file, "text/html");
    debugDirector("Served: " + String(file.name()));
    file.close();
  });

  server.on("/bluetoothscanner.html", []() {
    File file = SPIFFS.open("/bluetoothscanner.html", "r");
    server.streamFile(file, "text/html");
    debugDirector("Served: " + String(file.name()));
    file.close();
  });

  server.on("/send_settings", []() {
    String tString;
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
    if (!server.arg("inclineStep").isEmpty())
    {
      userConfig.setInclineStep(server.arg("inclineStep").toFloat());
    }
    if (!server.arg("shiftStep").isEmpty())
    {
      userConfig.setShiftStep(server.arg("shiftStep").toInt());
    }
    if (!server.arg("inclineMultiplier").isEmpty())
    {
      userConfig.setInclineMultiplier(server.arg("inclineMultiplier").toFloat());
    }
    if (!server.arg("bleDropdown").isEmpty())
    {
      userConfig.setConnectedPowerMeter(server.arg("bleDropdown"));
    }
    String response = "<!DOCTYPE html><html><body>Saving Settings....</body><script> setTimeout(\"location.href = 'http://SmartSpin2k.local/settings.html';\",1000);</script></html>";
    server.send(200, "text/html", response);
    debugDirector("Config Updated From Web");
    userConfig.printFile();
    userConfig.saveToSPIFFS();
  });

  server.on("/BLEServers", []() {
    debugDirector("Sending BLE device list to HTTP Client");
    server.send(200, "text/plain", userConfig.getFoundDevices());
  });

  server.on("/BLEScan", []() {
    debugDirector("Scanning for BLE Devices");
    BLEServerScan(false);
    String response = "<!DOCTYPE html><html><body>Scanning for BLE Devices. Please wait.</body><script> setTimeout(\"location.href = 'http://SmartSpin2k.local/bluetoothscanner.html';\",5000);</script></html>";
    server.send(200, "text/html", response);
  });

  server.on("/load_defaults.html", []() {
    debugDirector("Setting Defaults from Web Request");
    SPIFFS.format();
    userConfig.setDefaults();
    userConfig.saveToSPIFFS();
    String response = "Loading Defaults....<script> setTimeout(\"location.href ='http://SmartSpin2k.local/settings.html';\",1000); </script>";
    server.send(200, "text/html", response);
  });

  server.on("/reboot.html", []() {
    debugDirector("Rebooting from Web Request");
    String response = "Rebooting....<script> setTimeout(\"location.href = 'http://SmartSpin2k.local/index.html';\",1000); </script>";
    server.send(200, "text/html", response);
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
    tString.remove(tString.length()-1,1);
    tString += String(",\"debug\":") + "\"" + debugToHTML + "\"}";
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
      1,                 /* priority of the task  - 29 worked*/
      &webClientTask,    /* Task handle to keep track of created task */
      tskNO_AFFINITY);   /* pin task to core 0 */
  //vTaskStartScheduler();
  debugDirector("WebClientTaskCreated");

  server.begin();
  debugDirector("HTTP server started");
}

void webClientUpdate(void *pvParameters)
{
  for (;;)
  {
    server.handleClient();
    vTaskDelay(10 / portTICK_RATE_MS);
  }
}

void handleIndexFile()
{
  if(SPIFFS.exists("/index.html")){
  File file = SPIFFS.open("/index.html", "r");
  server.streamFile(file, "text/html");
  file.close();
  }else{
    debugDirector("index.html not found. Sending builtin");
    server.send(200, "text/html", noIndexHTML);
  }

}

void handleStyleCss()
{
  File file = SPIFFS.open("/style.css", "r");
  server.streamFile(file, "text/css");
  file.close();
}

void FirmwareUpdate()
{
  debugDirector("Checking for newer firmware");
  http.begin(URL_fw_Server + String("version.txt")); // check version URL
  delay(100);
  int httpCode = http.GET(); // get data from version file
  delay(100);
  String payload;
  if (httpCode == HTTP_CODE_OK) // if version received
  {
    debugDirector("Version info recieved:");
    payload = http.getString(); // save received version
    payload.trim();
    debugDirector(payload);
  }
  else
  {
    debugDirector("error in downloading version file: " + String(httpCode));
  }

  http.end();
  if (httpCode == HTTP_CODE_OK) // if version received
  {
    if (payload == FirmwareVer)
    {
      debugDirector("Device already on latest firmware version");
    }
    else
    {
      debugDirector("New firmware detected!");
      debugDirector("Upgrading from " + FirmwareVer + " to " + payload);
      WiFiClientSecure client;
      httpUpdate.setLedPin(LED_BUILTIN, LOW);
      debugDirector("Updating FileSystem");
      t_httpUpdate_return ret = httpUpdate.updateSpiffs(client, URL_fw_spiffs);
      vTaskDelay(100/portTICK_PERIOD_MS);
      if (ret == HTTP_UPDATE_OK) {
      debugDirector("Saving Config.txt");
      userConfig.saveToSPIFFS();
      debugDirector("Updating Program");
      ret = httpUpdate.update(client, URL_fw_Bin);
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
  }
}


