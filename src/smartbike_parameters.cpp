// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive
#include <smartbike_parameters.h>
#include <FS.h>                     //Filesystem read/write
#include <ArduinoJson.h>
#include <SPIFFS.h>                 //needed for Spiffs

// Set all defaults except filename as that is set on object initialization.
void userParameters::setDefaults()
{
    incline             = 0.0;
    simulatedWatts      = 100;
    simulatedHr         = 60;
    inclineStep         = 400;
    shiftStep           = 400;
    inclineMultiplier   = 1.0;
    simulatePower       = false;
    simulateHr          = true;
    ERGMode             = false;
    wifiOn              = true;
    ssid                = "SmartBike2k";
    password            = "password";
    foundDevices        = "";
    connectedPowerMeter    = ""; 
    connectedHeartMonitor = "";
}

//---------------------------------------------------------------------------------
//-- return all config as one a single JSON string
String userParameters::returnJSON()
{
  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
  StaticJsonDocument<512> doc;
  // Set the values in the document

    doc["filename"]           = filename;
    doc["incline"]            = incline;
    doc["simulatedWatts"]     = simulatedWatts;
    doc["simulatedHr"]        = simulatedHr;
    doc["inclineStep"]        = inclineStep;
    doc["shiftStep"]          = shiftStep;
    doc["inclineMultiplier"]  = inclineMultiplier;
    doc["simulatePower"]      = simulatePower;
    doc["simulateHr"]         = simulateHr;
    doc["ERGMode"]            = ERGMode;
    doc["wifiOn"]             = wifiOn;
    doc["ssid"]               = ssid;
    doc["password"]           = password;
    //doc["foundDevices"]       = foundDevices;  I don't see a need currently in keeping this boot to boot
    doc["connectedPowerMeter"]   = connectedPowerMeter; 
    doc["connectedHeartMonitor"]   = connectedHeartMonitor; 
  String output;
  serializeJson(doc, output);
  return output;
}

//-- Saves all parameters to SPIFFS
void userParameters::saveToSPIFFS()
{
  // Delete existing file, otherwise the configuration is appended to the file
  SPIFFS.remove(filename);

  // Open file for writing
  Serial.printf("Writing File: %s \n", filename.c_str());
  File file = SPIFFS.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println(F("Failed to create file"));
    return;
  }

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
  StaticJsonDocument<512> doc;

  // Set the values in the document

    doc["filename"]           = filename;
    doc["incline"]            = incline;
    doc["simulatedWatts"]     = simulatedWatts;
    doc["simulatedHr"]        = simulatedHr;
    doc["inclineStep"]        = inclineStep;
    doc["shiftStep"]          = shiftStep;
    doc["inclineMultiplier"]  = inclineMultiplier;
    doc["simulatePower"]      = simulatePower;
    doc["simulateHr"]         = simulateHr;
    doc["ERGMode"]            = ERGMode;
    doc["wifiOn"]             = wifiOn;
    doc["ssid"]               = ssid;
    doc["password"]           = password;
    //doc["foundDevices"]       = foundDevices; 
    doc["connectedPowerMeter"]   = connectedPowerMeter; 
    doc["connectedHeartMonitor"]   = connectedHeartMonitor; 

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
  }
  // Close the file
   file.close();
}

// Loads the JSON configuration from a file into a userParameters Object
void userParameters::loadFromSPIFFS() {
  // Open file for reading
  Serial.printf("Reading File: %s \n", filename.c_str());
  File file = SPIFFS.open(filename);

  //load defaults if filename doesn't exist
 if(!file){ 
   Serial.println("Couldn't find configuration file. Loading Defaults");
   setDefaults();
  return;
  }
  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<700> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error){
    Serial.println(F("Failed to read file, using default configuration"));
    setDefaults();
    return;
  }

  // Copy values from the JsonDocument to the Config
    setFilename         (doc["filename"]);
    setIncline          (doc["incline"]);
    setSimulatedWatts   (doc["simulatedWatts"]);
    setSimulatedHr      (doc["simulatedHr"]);
    setInclineStep      (doc["inclineStep"]);
    setShiftStep        (doc["shiftStep"]);
    setInclineMultiplier(doc["inclineMultiplier"]);
    setSimulatePower    (doc["simulatePower"]);
    setSimulateHr       (doc["simulateHr"]);
    setERGMode          (doc["ERGMode"]);
    setWifiOn           (doc["wifiOn"]);
    setSsid             (doc["ssid"]);
    setPassword         (doc["password"]);
    //setfoundDevices     (doc["foundDevices"]);
    setConnectedPowerMeter (doc["connectedPowerMeter"]);
    setConnectedHeartMonitor (doc["connectedHeartMonitor"]);

    Serial.printf("Config File Loaded: %s \n", filename.c_str());        
    file.close();
}

// Prints the content of a file to the Serial
void userParameters::printFile() {
  // Open file for reading
  Serial.printf("Contents of file: %s\n", filename.c_str());
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
