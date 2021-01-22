// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive
//

#ifndef BLE_COMMON_H
#define BLE_COMMON_H

//#define CONFIG_SW_COEXIST_ENABLE 1

#include <NimBLEDevice.h>
#include <Arduino.h>

//Heart Service
#define HEARTSERVICE_UUID BLEUUID((uint16_t)0x180D)
#define HEARTCHARACTERISTIC_UUID BLEUUID((uint16_t)0x2A37)

//Cycling Power Service
#define CSCSERVICE_UUID BLEUUID((uint16_t)0x1816)
#define CSCMEASUREMENT_UUID BLEUUID((uint16_t)0x2A5B)
#define CYCLINGPOWERSERVICE_UUID BLEUUID((uint16_t)0x1818)
#define CYCLINGPOWERMEASUREMENT_UUID BLEUUID((uint16_t)0x2A63)
#define CYCLINGPOWERFEATURE_UUID BLEUUID((uint16_t)0x2A65)
#define SENSORLOCATION_UUID BLEUUID((uint16_t)0x2A5D)

//Fitness Machine Service
#define FITNESSMACHINESERVICE_UUID BLEUUID((uint16_t)0x1826)
#define FITNESSMACHINEFEATURE_UUID BLEUUID((uint16_t)0x2ACC)
#define FITNESSMACHINECONTROLPOINT_UUID BLEUUID((uint16_t)0x2AD9)
#define FITNESSMACHINESTATUS_UUID BLEUUID((uint16_t)0x2ADA)
#define FITNESSMACHINEINDOORBIKEDATA_UUID BLEUUID((uint16_t)0x2AD2)
#define FITNESSMACHINERESISTANCELEVELRANGE_UUID BLEUUID((uint16_t)0x2AD6)
#define FITNESSMACHINEPOWERRANGE_UUID BLEUUID((uint16_t)0x2AD8)

// macros to convert different types of bytes into int The naming here sucks and should be fixed.
#define bytes_to_s16(MSB, LSB) (((signed int)((signed char)MSB))) << 8 | (((signed char)LSB))
#define bytes_to_u16(MSB, LSB) (((signed int)((signed char)MSB))) << 8 | (((unsigned char)LSB))
#define bytes_to_int(MSB, LSB) (((int)((unsigned char)MSB))) << 8 | (((unsigned char)LSB))
// Potentially, something like this is a better way of doing this ^^  data.getUint16(1, true)

//Setup
void setupBLE();

//*****************************Server*****************************
extern bool GlobalBLEClientConnected;
void startBLEServer();
void BLENotify(void *pvParameters);
void computeERG(int, int);
void computeCSC();
void updateIndoorBikeDataChar();
void updateCyclingPowerMesurementChar();
void calculateInstPwrFromHR();

class MyServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *, ble_gap_conn_desc* desc);
    void onDisconnect(BLEServer *);
};

class MyCallbacks : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *);
};

//*****************************Client*****************************

//Keeping the task outside the class so we don't need a mask. 
//We're only going to run one anyway.
void bleClientTask(void *pvParameters);  

class SpinBLEClient{ 
    
    public: //Not all of these need to be public. This should be cleaned up later.
    boolean doConnectPM         = false;
    boolean doConnectHR         = false;
    boolean connectedPM         = false;
    boolean connectedHR         = false;
    boolean doScan              = false;
    float crankRev[2]           = {0, 0};
    float crankEventTime[2]     = {0, 0};
    int noReadingIn             = 0;
    int cscCumulativeCrankRev   = 0;
    int cscLastCrankEvtTime     = 0;
    int lastConnectedPMID       = 0;

    BLERemoteCharacteristic *pRemoteCharacteristic  = nullptr;
    BLEAdvertisedDevice     *myPowerMeter           = nullptr;
    BLEAdvertisedDevice     *myHeartMonitor         = nullptr;

    void start();
    void serverScan(bool connectRequest);
    bool connectToServer();

private:
   
    class MyAdvertisedDeviceCallback : public NimBLEAdvertisedDeviceCallbacks
    {
       public:
       void onResult(NimBLEAdvertisedDevice *);
    };

    class MyClientCallback : public NimBLEClientCallbacks
    {
        public:
        void        onConnect(BLEClient *);
        void        onDisconnect(BLEClient *);
        uint32_t    onPassKeyRequest();
        bool        onConfirmPIN(uint32_t);
        void        onAuthenticationComplete(ble_gap_conn_desc);
    };
};

extern SpinBLEClient spinBLEClient;
#endif