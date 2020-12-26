// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

/* Assioma Pedal Information for later
BLE Advertised Device found: Name: ASSIOMA17287L, Address: e8:fe:6e:91:9f:16, appearance: 1156, manufacturer data: 640302018743, serviceUUID: 00001818-0000-1000-8000-00805f9b34fb
*/

#include "BLE_Common.h"
#include "Main.h"
#include <ArduinoJson.h>
#include <NimBLEDevice.h>

float crankRev[2] = {0, 0};
float crankEventTime[2] = {0, 0};
int noReadingIn = 0;
int cscCumulativeCrankRev = 0;
int cscLastCrankEvtTime = 0;

void setupBLE() //Common BLE setup for both Client and Server
{
  debugDirector("Starting Arduino BLE Client application...");
  BLEDevice::init(userConfig.getDeviceName());
  startBLEClient();
  startBLEServer();
  if ((String(userConfig.getconnectedPowerMeter()) != "none") && (String(userConfig.getconnectedHeartMonitor()) != "none"))
  {
    BLEServerScan(true);
  }



} // End of setup