// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

#include <Main.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTP_Server_Basic.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>
#include <Update.h>

TaskHandle_t webClientTask;
#define MAX_BUFFER_SIZE 20

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
"</script>" + OTAStyle;
 
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
"</script>" + OTAStyle;


WebServer server(80);  
void startHttpServer() {


/********************************************Begin Handlers***********************************/
server.on("/", handleIndexFile);
server.on("/style.css", handleStyleCss);
server.on("/index.html", handleIndexFile);

server.on("/btsimulator.html", [](){
File file = SPIFFS.open("/btsimulator.html","r");
  server.streamFile(file, "text/html");
  Serial.printf("Served: %s", file.name());
  file.close();
});

server.on("/settings.html", [](){
File file = SPIFFS.open("/settings.html","r");
  server.streamFile(file, "text/html");
  Serial.printf("Served: %s", file.name());
  file.close();
});

server.on("/bluetoothscanner.html", [](){
File file = SPIFFS.open("/bluetoothscanner.html","r");
  server.streamFile(file, "text/html");
  Serial.printf("Served: %s", file.name());
  file.close();
});

server.on("/send_settings", [](){
if(!server.arg("ssid").isEmpty())             {userConfig.setSsid                (server.arg("ssid"));                        }
if(!server.arg("password").isEmpty())         {userConfig.setPassword            (server.arg("password"));                    }
if(!server.arg("inclineStep").isEmpty())      {userConfig.setInclineStep         (server.arg("inclineStep").toFloat());       }
if(!server.arg("shiftStep").isEmpty())        {userConfig.setShiftStep           (server.arg("shiftStep").toInt());           }
if(!server.arg("inclineMultiplier").isEmpty()){userConfig.setInclineMultiplier   (server.arg("inclineMultiplier").toFloat()); }
if(!server.arg("bleDropdown").isEmpty())      {userConfig.setConnectedPowerMeter    (server.arg("bleDropdown"));                 }
String response = "<!DOCTYPE html><html><body>Saving Settings....</body><script> setTimeout(\"location.href = 'http://smartbike2k.local/settings.html';\",2000);</script></html>" ;
server.send(200, "text/html", response);
Serial.println("Config Updated From Web");
userConfig.printFile();
userConfig.saveToSPIFFS();
});

server.on("/BLEServers", [](){
Serial.println("Sending BLE device list to HTTP Client");
server.send(200, "text/plain", userConfig.getFoundDevices());
});

server.on("/BLEScan", [](){
Serial.println("Scanning for BLE Devices");
//BLEServerScan(false);
String response = "<!DOCTYPE html><html><body>Scanning for BLE Devices. Please wait 10 seconds.</body><script> setTimeout(\"location.href = 'http://smartbike2k.local/bluetoothscanner.html';\",10000);</script></html>" ;
server.send(200, "text/html", response);
});

server.on("/load_defaults.html", [](){
Serial.println("Setting Defaults from Web Request");
userConfig.setDefaults();
userConfig.saveToSPIFFS();
String response = "Loading Defaults....<script> setTimeout(\"location.href ='http://smartbike2k.local/settings.html';\",2000); </script>";
server.send(200, "text/html", response);
});

server.on("/reboot.html", [](){
Serial.println("Rebooting from Web Request");
String response = "Rebooting....<script> setTimeout(\"location.href = 'http://smartbike2k.local/index.html';\",2000); </script>";
server.send(200, "text/html", response);
ESP.restart();
});

server.on("/hrslider", []() {
  String value=server.arg("value");
    if(value == "enable"){userConfig.setSimulateHr(true);
    server.send(200, "text/plain", "OK");
    Serial.println("HR Simulator turned on");
    }else if(value == "disable"){userConfig.setSimulateHr(false);
    server.send(200, "text/plain", "OK");
    Serial.println("HR Simulator turned off");
    }else{
  userConfig.setSimulatedHr(value.toInt());
  Serial.printf("HR is now: %d BPM\n", userConfig.getSimulatedHr());
  server.send(200, "text/plain", "OK");
    }
  Serial.println("Webclient High Water Mark:");
  Serial.println(uxTaskGetStackHighWaterMark(webClientTask));
  });

  server.on("/wattsslider", []() {
  String value=server.arg("value");
    if(value == "enable"){userConfig.setSimulatePower(true);
    server.send(200, "text/plain", "OK");
    Serial.println("Watt Simulator turned on");
    }else if(value == "disable"){userConfig.setSimulatePower(false);
    server.send(200, "text/plain", "OK");
    Serial.println("Watt Simulator turned off");
    }else{
  userConfig.setSimulatedWatts(value.toInt());
  Serial.printf("Watts are now: %d \n", userConfig.getSimulatedWatts());
  server.send(200, "text/plain", "OK");
    }
  Serial.println("Webclient High Water Mark:");
  Serial.println(uxTaskGetStackHighWaterMark(webClientTask));
 });

 server.on("/hrValue", [](){
 char outString[MAX_BUFFER_SIZE];
 snprintf(outString, MAX_BUFFER_SIZE, "%d", userConfig.getSimulatedHr());
 server.send(200, "text/plain", outString);
 });

server.on("/wattsValue", [](){
 char outString[MAX_BUFFER_SIZE];
 snprintf(outString, MAX_BUFFER_SIZE, "%d", userConfig.getSimulatedWatts());
 server.send(200, "text/plain", outString);
 });

 server.on("/configJSON", [](){
 server.send(200, "text/plain", userConfig.returnJSON());
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
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
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
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });

/********************************************End Server Handlers*******************************/

  xTaskCreatePinnedToCore(
    webClientUpdate,   /* Task function. */
    "webClientUpdate",     /* name of task. */
    4000,       /* Stack size of task Used to be 3000*/
    NULL,        /* parameter of the task */
    1,           /* priority of the task  - 29 worked*/
    &webClientTask,     /* Task handle to keep track of created task */
    tskNO_AFFINITY);          /* pin task to core 0 */
  //vTaskStartScheduler();
  Serial.println("WebClientTaskCreated");

server.begin();
Serial.println("HTTP server started");

}

void webClientUpdate(void * pvParameters) {
for(;;){
server.handleClient();
vTaskDelay(10/portTICK_RATE_MS);
}

}

void handleIndexFile()
{
  File file = SPIFFS.open("/index.html","r");
  server.streamFile(file, "text/html");
  file.close();
}

void handleStyleCss()
{
 File file = SPIFFS.open("/style.css","r");
  server.streamFile(file, "text/css");
  file.close();
}

void startWifi(){

  int i = 0;

//Trying Station mode first: 
  Serial.printf("Connecting to %s\n", userConfig.getSsid());
  if(String(WiFi.SSID()) != userConfig.getSsid()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(userConfig.getSsid(), userConfig.getPassword());
  }

  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(1000/portTICK_RATE_MS);
    Serial.print(".");
    i++;
    if(i>5)
    {
      i = 0;
      Serial.println("Couldn't Connect. Switching to AP mode");
      break;
    }
  }
  if(WiFi.status() == WL_CONNECTED){
  Serial.println("");
  Serial.printf("Connected to %s IP address: ", userConfig.getSsid()); //Need to implement Fallback Timer <--------------------------------------------------------
  Serial.println(WiFi.localIP());
  }

// Couldn't connect to existing network, Create SoftAP
if(WiFi.status() != WL_CONNECTED){
WiFi.softAP(userConfig.getSsid(), userConfig.getPassword());
//WiFi.softAPConfig(local_ip, gateway, subnet);
  vTaskDelay(50);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
}
  MDNS.begin("smartbike2k");
  Serial.print("Open http://");
  Serial.print("smartbike2k");
  Serial.println(".local/");
}
