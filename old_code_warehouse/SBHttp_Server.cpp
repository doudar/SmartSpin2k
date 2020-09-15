// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Physical hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

/* Async Webpage server */

#include <SBHttp_Server.h>
//#include <HTTP_Server_Basic.h>
#include <smartbike_parameters.h>
#include <Main.h>

// Import required libraries
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <FS.h>
#include <string.h>
#define MAX_BUFFER_SIZE 20

// Set LED GPIO
const int ledPin = 2; //Blue LED on the ESP32
// Stores LED state
String ledState;
// Webpage Parameter varables
const char* webStringParameter = "state";
const char* webIntParameter = "value";
int i; //Multi use integer for general purpose

/********************Added Function for SPIFFS File Testing*****************************/
void printFile(String filename) {
  // Open file for reading
  Serial.printf("Contents of file: %s/n", filename.c_str());
  File file = SPIFFS.open(filename);
  if (!file) {
    Serial.println(F("Failed to read file"));
    return;
  }

  // Extract each characters by one by one
  while (file.available()) {
    Serial.print((char)file.read());
  }
  Serial.println();

  // Close the file
  file.close();
}

/***************************************************************************************/
// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Replaces placeholder with LED state value
String processor(const String& var){
  char outString[MAX_BUFFER_SIZE];
  Serial.printf("Processing Variable from HTTP Server: %s \n", var.c_str());
  
  if(var == "STATE"){
    if(digitalRead(ledPin)){
      ledState = "ON";
    }
    else{
      ledState = "OFF";
    }
    Serial.printf("LED State is %s \n", ledState.c_str());
    return ledState;
  }

  if(var =="HROUTPUT"){ 
      if(userConfig.getSimulateHr()){return "checked";}
      else{return "";}
  }

    if(var =="WATTSOUTPUT"){ 
      if(userConfig.getSimulatePower()){return "checked";}
      else{return "";}
  }

  if(var =="HRVALUE"){
  snprintf(outString, MAX_BUFFER_SIZE, "%d", userConfig.getSimulatedHr());
  strncat(outString, " BPM", 4);
  //Serial.printf("outString: %s \n", outString);
  return outString;
  }

  if(var =="WATTSVALUE"){ 
  snprintf(outString, MAX_BUFFER_SIZE, "%d", userConfig.getSimulatedWatts());
  strncat(outString, " Watts", 6);
  return outString;
  }
  
  return String();
}

void startHttpServer(){

  pinMode(ledPin, OUTPUT);

  // Connect to Wi-Fi
  Serial.println();
  Serial.println("Configuring access point...");
  WiFi.softAP(userConfig.getSsid(), userConfig.getPassword());
  vTaskDelay(50);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  server.begin();
  Serial.println("WiFi Server started");

  // Route for root / web page
  /* I think we need to expand this one server instance to keep from crashing instead of using multiple server.on instances */
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    //request->send(SPIFFS, "/index.html", "text/html");
    request->send(SPIFFS, "/index.html", String(), true, processor);
    //request->
    //request->send(SPIFFS, "/style.css", "text/css");
    Serial.println("Index.html Sent");
    //printFile("/index.html");
  });
  
  // hopefully we can eventually delete all of this below
  // Route to load style.css file
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/style.css", "text/css");
    Serial.println("/style.css Sent");
    printFile("/style.css");
  });
  // Route to set GPIO to HIGH
  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request){
    digitalWrite(ledPin, HIGH);    
    request->send(SPIFFS, "/index.html", String(), true, processor);
  });
  
  // Route to set GPIO to LOW
  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request){
    digitalWrite(ledPin, LOW);    
    request->send(SPIFFS, "/index.html", String(), true, processor);
  });

  //***************************************Trying to get a value from a range slider
  server.on("/hrslider", HTTP_GET, [] (AsyncWebServerRequest *request) {
        // GET input1 value on <ESP_IP>/hrslider?value=<inputMessage>
    String inputMessage;
    if (request->hasParam(webIntParameter)) {
      inputMessage = request->getParam(webIntParameter)->value();
      userConfig.setSimulatedHr(inputMessage.toInt());
      Serial.printf("Heart Rate in Config is %d\n", userConfig.getSimulatedHr());
    }
    else {
      inputMessage = "No message sent";
    }
    Serial.println(inputMessage);
    request->send(SPIFFS, "/index.html", String(), true, processor);
    //request->send(200, "text/plain", "OK");
  });

    server.on("/wattsslider", HTTP_GET, [] (AsyncWebServerRequest *request) {
        // GET input1 value on <ESP_IP>/hrslider?value=<inputMessage>
    String inputMessage;
    if (request->hasParam(webIntParameter)) {
      inputMessage = request->getParam(webIntParameter)->value();
      userConfig.setSimulatedWatts(inputMessage.toInt());
      Serial.printf("Watt in Config is %d\n", userConfig.getSimulatedWatts());
    }
    else {
      inputMessage = "No message sent";
    }
    Serial.println(inputMessage);
    request->send(SPIFFS, "/index.html", String(), true, processor);
    //request->send(200, "text/plain", "OK");
  });

    server.on("/hrenable", HTTP_GET, [] (AsyncWebServerRequest *request) {
        // GET input1 value on <ESP_IP>/hrslider?value=<inputMessage>
    String inputMessage;
    if (request->hasParam(webIntParameter)) {
      inputMessage = request->getParam(webIntParameter)->value();
      if(inputMessage == "checked"){userConfig.setSimulateHr(true);}
      else{userConfig.setSimulateHr(false);}
      Serial.printf("HR output is set to %s\n", userConfig.getSimulateHr() ? "On":"Off");
    }
    else {
      inputMessage = "No message sent";
    }
    Serial.println(inputMessage);
    request->send(SPIFFS, "/index.html", String(), true, processor);
    //request->send(200, "text/plain", "OK");
  });

      server.on("/wattsenable", HTTP_GET, [] (AsyncWebServerRequest *request) {
        // GET input1 value on <ESP_IP>/hrslider?value=<inputMessage>
    String inputMessage;
    if (request->hasParam(webIntParameter)) {
      inputMessage = request->getParam(webIntParameter)->value();
      if(inputMessage == "checked"){userConfig.setSimulatePower(true);}
      else{userConfig.setSimulatePower(false);}
      Serial.printf("Power output is set to %s\n", userConfig.getSimulatePower() ? "On":"Off");
    }
    else {
      inputMessage = "No message sent";
    }
    Serial.println(inputMessage);
    request->send(SPIFFS, "/index.html", String(), true, processor);
    //request->send(200, "text/plain", "OK");
  });//
//********************************End Range Slider testing**********************************
  // Start server
  server.begin();

  Serial.println("End of Http setup");

}
/* 
void loop(){
  //Autosave every 30 seconds
  vTaskDelay(30000/portTICK_PERIOD_MS);
  Serial.println("Autosaving Configuration");
  userConfig.saveToSPIFFS();
  userConfig.printFile();
}*/
