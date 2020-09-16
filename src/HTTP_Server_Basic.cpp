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



TaskHandle_t webClientTask;
#define MAX_BUFFER_SIZE 20

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
if(!server.arg("bleDropdown").isEmpty())      {userConfig.setConnectedDevices    (server.arg("bleDropdown"));                 }
String response = "<!DOCTYPE html><html><body>Saving Settings....</body><script> setTimeout(\"location.href = 'http://smartbike2k.local/settings.html';\",5000);</script></html>" ;
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
BLEserverScan();
String response = "<!DOCTYPE html><html><body>Scanning for BLE Devices. Please wait 10 seconds.</body><script> setTimeout(\"location.href = 'http://smartbike2k.local/bluetoothscanner.html';\",10000);</script></html>" ;
server.send(200, "text/html", response);
});

server.on("/load_defaults.html", [](){
Serial.println("Setting Defaults from Web Request");
userConfig.setDefaults();
userConfig.saveToSPIFFS();
String response = "Loading Defaults....<script> setTimeout(\"location.href ='http://smartbike2k.local/settings.html';\",5000); </script>";
server.send(200, "text/html", response);
});

server.on("/reboot.html", [](){
Serial.println("Rebooting from Web Request");
String response = "Rebooting....<script> setTimeout(\"location.href = 'http://smartbike2k.local/index.html';\",5000); </script>";
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

void stopHttpServer() //had crashing issues with this function currently using a reboot instead
{
Serial.println("Stopping HTTP vTask");
 vTaskDelete(webClientTask);
 Serial.println("Http Task Deleted");
 //delay(1000);
 Serial.println("Closing HTTP Connections");
 //server.
 //server.close();
 //Serial.println("Stopping HTTP Server");
 //server.stop();
 //Serial.println("Server Stopped");
 //vTaskDelay(50/portTICK_PERIOD_MS);

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



void stopWifi(){ //This function causes a crash. Instead of using it, we're currently just rebooting the device. 

  // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#wi-fi-deinit-phase  
  // Wi-Fi Deinit phase s8.1-s8.3:  call esp_wifi_disconnect(); esp_wifi_stop(); esp_wifi_deinit() ; 
  // then wait five minutes for ASSOC_EXPIRE or AUTH_EXPIRE?;    
  // maybe consider just sending a de-authentication with esp_wifi_deauth_sta() and skip the five minute wait? 
  
  Serial.println("Disconnecting WiFi");
  WiFi.setSleep(true);
  WiFi.disconnect(true); 
  Serial.println("Turning WiFi Off");
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFI Turned Off");
 //WiFi.forceSleepBegin();
}
