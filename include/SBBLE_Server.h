// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive
// 


#ifndef SBBLE_SERVER_H
#define SBBLE_SERVER_H

#define HEARTSERVICE_UUID BLEUUID((uint16_t)0x180D)
#define HEARTCHARACTERISTIC_UUID BLEUUID((uint16_t)0x2A37)

#define CSCSERVICE_UUID BLEUUID((uint16_t)0x1816)
#define CSCMEASUREMENT_UUID BLEUUID((uint16_t)0x2A5B)

#define CYCLINGPOWERSERVICE_UUID BLEUUID((uint16_t)0x1818)
#define CYCLINGPOWERMEASUREMENT_UUID BLEUUID((uint16_t)0x2A63)
#define CYCLINGPOWERFEATURE_UUID BLEUUID((uint16_t)0x2A65)
#define SENSORLOCATION_UUID BLEUUID((uint16_t)0x2A5D)

#define FITNESSMACHINESERVICE_UUID BLEUUID((uint16_t)0x1826)
#define FITNESSMACHINEFEATURE_UUID BLEUUID((uint16_t)0x2ACC)
#define FITNESSMACHINECONTROLPOINT_UUID BLEUUID((uint16_t)0x2AD9)
#define FITNESSMACHINESTATUS_UUID BLEUUID((uint16_t)0x2ADA)
#define FITNESSMACHINEINDOORBIKEDATA_UUID BLEUUID((uint16_t)0x2AD2)
#define FITNESSMACHINERESISTANCELEVELRANGE_UUID BLEUUID((uint16_t)0x2AD6)
#define FITNESSMACHINEPOWERRANGE_UUID BLEUUID((uint16_t)0x2AD8)

void startBLEServer();
void BLENotify();
bool connectToServer(String UUID_Of_Service);
void BLEServerScan(bool connectRequest);
void bleClient();
void startBleClient();
void computeERG(int, int);
void setupBLE();
void computeCSC();
void updateIndoorBikeDataChar();
void updateCyclingPowerMesurementChar();

#endif