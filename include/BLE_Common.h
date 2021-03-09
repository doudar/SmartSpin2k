// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive
//

#pragma once

//#define CONFIG_SW_COEXIST_ENABLE 1

#include <memory>
#include <NimBLEDevice.h>
#include <Arduino.h>
#include <Main.h>

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

// GATT service/characteristic UUIDs for Flywheel Bike from ptx2/gymnasticon/
#define FLYWHEEL_UART_SERVICE_UUID BLEUUID((uint16_t)0xCA9E)
#define FLYWHEEL_UART_RX_UUID BLEUUID((uint16_t)0xCA9E)
#define FLYWHEEL_UART_TX_UUID BLEUUID((uint16_t)0xCA9E)

// macros to convert different types of bytes into int The naming here sucks and should be fixed.
#define bytes_to_s16(MSB, LSB) (((signed int)((signed char)MSB))) << 8 | (((signed char)LSB))
#define bytes_to_u16(MSB, LSB) (((signed int)((signed char)MSB))) << 8 | (((unsigned char)LSB))
#define bytes_to_int(MSB, LSB) (((int)((unsigned char)MSB))) << 8 | (((unsigned char)LSB))
// Potentially, something like this is a better way of doing this ^^  data.getUint16(1, true)

//Setup
void setupBLE();
extern TaskHandle_t BLECommunicationTask;
//***********************Common**********************************/
void BLECommunications(void *pvParameters);

//*****************************Server****************************/
extern int bleConnDesc; //These all need re
extern bool _BLEClientConnected;
extern bool updateConnParametersFlag;
extern bool GlobalBLEClientConnected;

void startBLEServer();
void computeERG(int, int);
void computeCSC();
void updateIndoorBikeDataChar();
void updateCyclingPowerMesurementChar();
void calculateInstPwrFromHR();
void updateHeartRateMeasurementChar();

class MyServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *, ble_gap_conn_desc *desc);
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

//UUID's the client has methods for
//BLEUUID serviceUUIDs[4] = {FITNESSMACHINESERVICE_UUID, CYCLINGPOWERSERVICE_UUID, HEARTSERVICE_UUID, FLYWHEEL_UART_SERVICE_UUID};
//BLEUUID charUUIDs[4] = {FITNESSMACHINEINDOORBIKEDATA_UUID, CYCLINGPOWERMEASUREMENT_UUID, HEARTCHARACTERISTIC_UUID, FLYWHEEL_UART_TX_UUID};


enum userSelect : uint8_t
{
    HR = 1,
    PM = 2,
    CSC = 3,
    CT = 4
};


class myAdvertisedBLEDevice
{
public: //eventually these shoul be made private
    BLEAdvertisedDevice *advertisedDevice = nullptr;
    NimBLEAddress peerAddress;
    int connectedClientID = BLE_HS_CONN_HANDLE_NONE;
    BLEUUID serviceUUID = (uint16_t)0x0000;
    BLEUUID charUUID = (uint16_t)0x0000;
    bool userSelectedHR = false;
    bool userSelectedPM = false;
    bool userSelectedCSC = false;
    bool userSelectedCT = false;
    bool doConnect = false;

    void set(BLEAdvertisedDevice *device, int id = BLE_HS_CONN_HANDLE_NONE, BLEUUID inserviceUUID = (uint16_t)0x0000, BLEUUID incharUUID = (uint16_t)0x0000)
    {
        advertisedDevice = device;
        peerAddress = device->getAddress();
        connectedClientID = id;
        serviceUUID = BLEUUID(inserviceUUID);
        charUUID = BLEUUID(incharUUID);
    }

    void reset()
    {
        advertisedDevice = nullptr;
        //NimBLEAddress peerAddress;
        connectedClientID = BLE_HS_CONN_HANDLE_NONE;
        serviceUUID = (uint16_t)0x0000;
        charUUID = (uint16_t)0x0000;
        userSelectedHR = false;
        userSelectedPM = false;
        userSelectedCSC = false;
        userSelectedCT = false;
        doConnect = false;
    }

    //userSelectHR  = 1,userSelectPM  = 2,userSelectCSC = 3,userSelectCT  = 4
    void setSelected(userSelect flags)
    {
        switch (flags)
        {
        case 1:
            userSelectedHR = true;
            break;
        case 2:
            userSelectedPM = true;
            break;
        case 3:
            userSelectedCSC = true;
            break;
        case 4:
            userSelectedCT = true;
            break;
        }
    }
};

class SpinBLEClient
{

public: //Not all of these need to be public. This should be cleaned up later.
    boolean connectedPM = false;
    boolean connectedHR = false;
    boolean connectedCD = false;
    boolean doScan = false;
    bool intentionalDisconnect = false;
    int noReadingIn = 0;
    int cscCumulativeCrankRev = 0;
    int cscLastCrankEvtTime = 0;

    BLERemoteCharacteristic *pRemoteCharacteristic = nullptr;

    //BLEDevices myBLEDevices;
    myAdvertisedBLEDevice myBLEDevices[NUM_BLE_DEVICES];

    void start();
    void serverScan(bool connectRequest);
    bool connectToServer();
    void scanProcess();
    void disconnect();
    //Check for duplicate services of BLEClient and remove the previoulsy connected one.
    void removeDuplicates(BLEClient *pClient);

private:
    class MyAdvertisedDeviceCallback : public NimBLEAdvertisedDeviceCallbacks
    {
    public:
        void onResult(NimBLEAdvertisedDevice *);
    };

    class MyClientCallback : public NimBLEClientCallbacks
    {
    public:
        void onConnect(BLEClient *);
        void onDisconnect(BLEClient *);
        uint32_t onPassKeyRequest();
        bool onConfirmPIN(uint32_t);
        void onAuthenticationComplete(ble_gap_conn_desc);
    };
};

extern SpinBLEClient spinBLEClient;

class SensorData
{
public:
    SensorData(String id) : id(id){};

    String getId();
    virtual bool hasHeartRate() = 0;
    virtual bool hasCadence() = 0;
    virtual bool hasPower() = 0;
    virtual bool hasSpeed() = 0;
    virtual int getHeartRate() = 0;
    virtual float getCadence() = 0;
    virtual int getPower() = 0;
    virtual float getSpeed() = 0;
    virtual void decode(uint8_t *data, size_t length) = 0;

protected:
    String id;
};

class KnownDevice
{
public:
    KnownDevice(NimBLEUUID uuid, std::shared_ptr<SensorData> sensorData): uuid(uuid), sensorData(sensorData){};
    NimBLEUUID getUUID();
    std::shared_ptr<SensorData> decode(uint8_t *data, size_t length);
    
private:
    NimBLEUUID uuid;
    std::shared_ptr<SensorData> sensorData;
};

class SensorDataFactory
{
public:
    SensorDataFactory(){};
        
    std::shared_ptr<SensorData> getSensorData(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length);

private:
    std::vector<KnownDevice*> knownDevices;
    static std::shared_ptr<SensorData> NULL_SENSOR_DATA;
};

class NullData : public SensorData
{
public:
    NullData() : SensorData("Null"){};

    virtual bool hasHeartRate();
    virtual bool hasCadence();
    virtual bool hasPower();
    virtual bool hasSpeed();
    virtual int getHeartRate();
    virtual float getCadence();
    virtual int getPower();
    virtual float getSpeed();
    virtual void decode(uint8_t *data, size_t length);
};

class HeartRateData : public SensorData
{
public:
    HeartRateData() : SensorData("HRM"){};

    virtual bool hasHeartRate();
    virtual bool hasCadence();
    virtual bool hasPower();
    virtual bool hasSpeed();
    virtual int getHeartRate();
    virtual float getCadence();
    virtual int getPower();
    virtual float getSpeed();
    virtual void decode(uint8_t *data, size_t length);

private:
    int heartrate;
};

class CyclePowerData : public SensorData
{
public:
    CyclePowerData() : SensorData("CPS"){};

    virtual bool hasHeartRate();
    virtual bool hasCadence();
    virtual bool hasPower();
    virtual bool hasSpeed();
    virtual int getHeartRate();
    virtual float getCadence();
    virtual int getPower();
    virtual float getSpeed();
    virtual void decode(uint8_t *data, size_t length);

private:
    int power;
    float cadence;
    float crankRev = 0;
    float lastCrankRev = 0;
    float lastCrankEventTime = 0;
    float crankEventTime = 0;
    uint8_t missedReadingCount = 0;
};

class FlywheelData : public SensorData
{
public:
    FlywheelData() : SensorData("FLYW"){};

    virtual bool hasHeartRate();
    virtual bool hasCadence();
    virtual bool hasPower();
    virtual bool hasSpeed();
    virtual int getHeartRate();
    virtual float getCadence();
    virtual int getPower();
    virtual float getSpeed();
    virtual void decode(uint8_t *data, size_t length);

private:
    bool hasData = false;
    float cadence;
    int power;
};

class FitnessMachineIndoorBikeData : public SensorData
{
public:
    FitnessMachineIndoorBikeData() : SensorData("FTMS"){};

    enum Types : uint8_t
    {
        InstantaneousSpeed = 0,
        AverageSpeed = 1,
        InstantaneousCadence = 2,
        AverageCadence = 3,
        TotalDistance = 4,
        ResistanceLevel = 5,
        InstantaneousPower = 6,
        AveragePower = 7,
        TotalEnergy = 8,
        EnergyPerHour = 9,
        EnergyPerMinute = 10,
        HeartRate = 11,
        MetabolicEquivalent = 12,
        ElapsedTime = 13,
        RemainingTime = 14
    };

    static constexpr uint8_t FieldCount = Types::RemainingTime + 1;

    virtual bool hasHeartRate();
    virtual bool hasCadence();
    virtual bool hasPower();
    virtual bool hasSpeed();
    virtual int getHeartRate();
    virtual float getCadence();
    virtual int getPower();
    virtual float getSpeed();
    virtual void decode(uint8_t *data, size_t length);

private:
    double_t values[FieldCount];

    static uint8_t const flagBitIndices[];
    static uint8_t const flagEnabledValues[];
    static size_t const byteSizes[];
    static uint8_t const signedFlags[];
    static double_t const resolutions[];
    static int convert(int value, size_t length, uint8_t isSigned);
};

/******************CPS***************************/
void BLE_CPSDecode(BLERemoteCharacteristic *pBLERemoteCharacteristic);

/******************FTMS**************************/
void BLE_FTMSDecode(BLERemoteCharacteristic *pBLERemoteCharacteristic);