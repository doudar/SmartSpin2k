
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

server.on("/send_settings", [](){
config.setSsid                (server.arg("ssid"));
config.setPassword            (server.arg("password"));
config.setInclineStep         (server.arg("inclineStep").toFloat());
config.setShiftStep           (server.arg("shiftStep").toInt());
config.setInclineMultiplier   (server.arg("inclineMultiplier").toFloat());
server.send(200, "text/plain", "OK");
Serial.println("Config Updated From Web");
});

server.on("/load_defaults.html", [](){
Serial.println("Setting Defaults from Web Request");
config.setDefaults();
config.saveToSPIFFS();
server.send(200, "text/plain", "OK");
});

server.on("/reboot.html", [](){
Serial.println("Rebooting from Web Request");
server.send(200, "text/plain", "OK");
ESP.restart();
});

server.on("/hrslider", []() {
  String value=server.arg("value");
    if(value == "enable"){config.setSimulateHr(true);
    server.send(200, "text/plain", "OK");
    Serial.println("HR Simulator turned on");
    }else if(value == "disable"){config.setSimulateHr(false);
    server.send(200, "text/plain", "OK");
    Serial.println("HR Simulator turned off");
    }else{
  config.setSimulatedHr(value.toInt());
  Serial.printf("HR is now: %d BPM\n", config.getSimulatedHr());
  server.send(200, "text/plain", "OK");
    }
  Serial.println("Webclient High Water Mark:");
  Serial.println(uxTaskGetStackHighWaterMark(webClientTask));
  });

  server.on("/wattsslider", []() {
  String value=server.arg("value");
    if(value == "enable"){config.setSimulatePower(true);
    server.send(200, "text/plain", "OK");
    Serial.println("Watt Simulator turned on");
    }else if(value == "disable"){config.setSimulatePower(false);
    server.send(200, "text/plain", "OK");
    Serial.println("Watt Simulator turned off");
    }else{
  config.setSimulatedWatts(value.toInt());
  Serial.printf("Watts are now: %d \n", config.getSimulatedWatts());
  server.send(200, "text/plain", "OK");
    }
  Serial.println("Webclient High Water Mark:");
  Serial.println(uxTaskGetStackHighWaterMark(webClientTask));
 });

 server.on("/hrValue", [](){
 char outString[MAX_BUFFER_SIZE];
 snprintf(outString, MAX_BUFFER_SIZE, "%d", config.getSimulatedHr());
 server.send(200, "text/plain", outString);
 });

server.on("/wattsValue", [](){
 char outString[MAX_BUFFER_SIZE];
 snprintf(outString, MAX_BUFFER_SIZE, "%d", config.getSimulatedWatts());
 server.send(200, "text/plain", outString);
 });

 server.on("/configJSON", [](){
 server.send(200, "text/plain", config.returnJSON());
 });




/********************************************End Server Handlers*******************************/

  xTaskCreatePinnedToCore(
    webClientUpdate,   /* Task function. */
    "webClientUpdate",     /* name of task. */
    3000,       /* Stack size of task */
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
  Serial.printf("Connecting to %s\n", config.getSsid());
  if(String(WiFi.SSID()) != config.getSsid()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.getSsid(), config.getPassword());
  }

  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(1000/portTICK_RATE_MS);
    Serial.print(".");
    i++;
    if(i>5)
    {
      i = 0;
      Serial.println("Couldn't connect. Switching to AP mode");
      break;
    }
  }
  if(WiFi.status() == WL_CONNECTED){
  Serial.println("");
  Serial.printf("Connected to %s IP address: ", config.getSsid()); //Need to implement Fallback Timer <--------------------------------------------------------
  Serial.println(WiFi.localIP());
  }

// Couldn't connect to existing network, Create SoftAP
if(WiFi.status() != WL_CONNECTED){
WiFi.softAP(config.getSsid(), config.getPassword());
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
