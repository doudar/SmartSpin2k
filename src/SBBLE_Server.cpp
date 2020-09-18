// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

/* Assioma Pedal Information for later
BLE Advertised Device found: Name: ASSIOMA17287L, Address: e8:fe:6e:91:9f:16, appearance: 1156, manufacturer data: 640302018743, serviceUUID: 00001818-0000-1000-8000-00805f9b34fb
*/

//#include <smartbike_parameters.h>
//#include <Main.h>
#include <SBBLE_Server.h>
//#include <BLEDevice.h>
//#include <BLEUtils.h>
//#include <BLEServer.h>
//#include <BLE2902.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>

//BLE Server Settings
String BLEName = "SmartBike2K";
bool _BLEClientConnected = false;
int toggle = false;

//PID Constants
const double kp = 2;
const double ki = 5;
const double kd = 1;
//PID Variables
unsigned long currentTime, previousTime;
double elapsedTime;
double error;
double lastError = 0;
double input, output, setPoint;
double cumError, rateError;

BLECharacteristic *heartRateMeasurementCharacteristic;
BLECharacteristic *cyclingPowerMeasurementCharacteristic;
BLECharacteristic *fitnessMachineFeature;

/********************************Bit field Flag Example***********************************/
// 0000000000001 - 1   - 0x001 - Pedal Power Balance Present
// 0000000000010 - 2   - 0x002 - Pedal Power Balance Reference
// 0000000000100 - 4   - 0x004 - Accumulated Torque Present
// 0000000001000 - 8   - 0x008 - Accumulated Torque Source
// 0000000010000 - 16  - 0x010 - Wheel Revolution Data Present
// 0000000100000 - 32  - 0x020 - Crank Revolution Data Present
// 0000001000000 - 64  - 0x040 - Extreme Force Magnitudes Present
// 0000010000000 - 128 - 0x080 - Extreme Torque Magnitudes Present
// 0000100000000 - Extreme Angles Present (bit8)
// 0001000000000 - Top Dead Spot Angle Present (bit 9)
// 0010000000000 - Bottom Dead Spot Angle Present (bit 10)
// 0100000000000 - Accumulated Energy Present (bit 11)
// 1000000000000 - Offset Compensation Indicator (bit 12)
byte heartRateMeasurement[5] = {0b00000, 60, 0, 0, 0};
byte cyclingPowerMeasurement[19] = {0b0000000000000, 0, 200, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};            // 3rd byte is reported power
byte ftmsService[16] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};                                                 // 5th byte to enable the FTMS bike service
byte ftmsFeature[32] = {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; //3rd bit enables incline support
byte ftmsControlPoint[8] = {0, 0, 0, 0, 0, 0, 0, 0};                                                                     //0x08 we need to return a value of 1 for any sucessful change
byte ftmsMachineStatus[8] = {0, 0, 0, 0, 0, 0, 0, 0};                                                                    //server needs to expose this. Not certain what it does.

#define HEARTSERVICE_UUID BLEUUID((uint16_t)0x180D)
#define HEARTCHARACTERISTIC_UUID BLEUUID((uint16_t)0x2A37)
#define CYCLINGPOWERSERVICE_UUID BLEUUID((uint16_t)0x1818)
#define CYCLINGPOWERMEASUREMENT_UUID BLEUUID((uint16_t)0x2A63)

//FitnessMachineServiceDefines//
#define FITNESSMACHINESERVICE_UUID BLEUUID((uint16_t)0x1826)
#define FITNESSMACHINEFEATURE_UUID BLEUUID((uint16_t)0x2ACC)
#define FITNESSMACHINECONTROLPOINT_UUID BLEUUID((uint16_t)0x2AD9)
#define FITNESSMACHINESTATUS_UUID BLEUUID((uint16_t)0x2ADA)

// This creates a macro that converts 8 bit LSB,MSB to Signed 16b
#define bytes_to_s16(MSB, LSB) (((signed int)((signed char)MSB))) << 8 | (((signed char)LSB))
#define bytes_to_u16(MSB, LSB) (((signed int)((signed char)MSB))) << 8 | (((unsigned char)LSB))

//Creating Server Callbacks
class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    _BLEClientConnected = true;
    Serial.println("Bluetooth Client Connected!");
    // vTaskDelay(5000);
  };

  void onDisconnect(BLEServer *pServer)
  {
    _BLEClientConnected = false;
    Serial.println("Bluetooth Client Disconnected!");
    // vTaskDelay(5000);
  }
};


/***********************************BLE CLIENT. EXPERIMENTAL TESTING ********************/
void bleClient(){}
void BLEserverScan(){}

/**********************************END OF EXPERIMENTAL BLE CLIENT TESTING*****************/

/**************BLE Server Callbacks *************************/
class MyCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    std::string rxValue = pCharacteristic->getValue();

    if (rxValue.length() > 1)
    {

      Serial.println("*********");
      Serial.println("Received Data From Zwift!");

      for (const auto &text : rxValue)
      { // Range-for!
        Serial.printf(" %d", text);
      }
      /* 17 means FTMS Incline Control Mode  (aka SIM mode)*/

      if ((int)rxValue[0] == 17)
      {
        signed char buf[2];
        buf[0] = rxValue[3]; // (Least significant byte)
        buf[1] = rxValue[4]; // (Most significant byte)

        int port = bytes_to_u16(buf[1], buf[0]);
        userConfig.setIncline(port);
        if (userConfig.getERGMode())
        {
          userConfig.setERGMode(false);
        }
        Serial.print("   Target Incline: ");
        Serial.print(userConfig.getIncline() / 100);
        Serial.println("*********");
      }

      /* 5 means FTMS Watts Control Mode (aka ERG mode) */

      if ((int)rxValue[0] == 5)
      {
        int targetWatts = bytes_to_s16(rxValue[2], rxValue[1]);
        if (!userConfig.getERGMode())
        {
          userConfig.setERGMode(true);
        }
        userConfig.setIncline(computePID(userConfig.getSimulatedWatts(), targetWatts)); //Updating incline via PID...This should be interesting.....
        Serial.printf("   Target Watts: %d", targetWatts);
        Serial.print(userConfig.getSimulatedWatts()); //not displaying numbers less than 256 correctly but they do get sent to Zwift correctly.
        Serial.println("*********");
      }
    }
  }
};

void startBLEServer()
{
  //Server Setup
  BLEDevice::init(BLEName.c_str());
  NimBLEServer *pServer = BLEDevice::createServer();
  
  //HEART RATE MONITOR SERVICE SETUP
  BLEService *pService = pServer->createService(HEARTSERVICE_UUID);
  BLECharacteristic *heartRateMeasurementCharacteristic = pService->createCharacteristic(
      HEARTCHARACTERISTIC_UUID,
      NIMBLE_PROPERTY::NOTIFY);

  //Power Meter MONITOR SERVICE SETUP
  BLEService *pPowerMonitor = pServer->createService(CYCLINGPOWERSERVICE_UUID);
  BLECharacteristic *cyclingPowerMeasurementCharacteristic = pPowerMonitor->createCharacteristic(
      CYCLINGPOWERMEASUREMENT_UUID,
      NIMBLE_PROPERTY::NOTIFY);

  //Fitness Machine service setup
  BLEService *pFitnessMachineService = pServer->createService(FITNESSMACHINESERVICE_UUID);
  BLECharacteristic *fitnessMachineFeature = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINEFEATURE_UUID,
      NIMBLE_PROPERTY::READ);

  BLECharacteristic *fitnessMachineControlPoint = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINECONTROLPOINT_UUID,
      NIMBLE_PROPERTY::WRITE |
          NIMBLE_PROPERTY::NOTIFY |
          NIMBLE_PROPERTY::INDICATE);

  BLECharacteristic *fitnessMachineStatus = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINESTATUS_UUID,
      NIMBLE_PROPERTY::NOTIFY);

  pServer->setCallbacks(new MyServerCallbacks());
  //Creating Characteristics

  //Bluetooth Server Setup
  Serial.println("Starting BLE work!");

  //Create BLE Server


  heartRateMeasurementCharacteristic->setValue(heartRateMeasurement, 5);
  cyclingPowerMeasurementCharacteristic->setValue(cyclingPowerMeasurement, 19);
  fitnessMachineFeature->setValue(ftmsFeature, 32);
  fitnessMachineControlPoint->setValue(ftmsControlPoint, 8);
  fitnessMachineControlPoint->setCallbacks(new MyCallbacks());

  fitnessMachineStatus->setValue(ftmsMachineStatus, 8);

  pServer->getAdvertising()->addServiceUUID(HEARTSERVICE_UUID);
  pServer->getAdvertising()->addServiceUUID(CYCLINGPOWERSERVICE_UUID);
  pServer->getAdvertising()->addServiceUUID(FITNESSMACHINESERVICE_UUID);
  //pAdvertising->setMinPreferred(0x01);  // functions that help with iPhone connections issue
  //pAdvertising->setMaxPreferred(0x02); // I haven't experienced a need for these fixed upstream?

  pService->start();               //heart rate service
  pPowerMonitor->start();          //Power Meter Service
  pFitnessMachineService->start(); //Fitness Machine Service

  pServer->getAdvertising()->start();

  Serial.println("BLuetooth Characteristic defined!");
}

void BLENotify()
{ //!!!!!!!!!!!!!!!!High priority to spin this and the http loop both off as their own xtasks
  if (_BLEClientConnected)
  {
    //update the BLE information on the server
    heartRateMeasurement[1] = userConfig.getSimulatedHr();
    cyclingPowerMeasurement[2] = userConfig.getSimulatedWatts();
    heartRateMeasurementCharacteristic->setValue(heartRateMeasurement, 5);
    heartRateMeasurementCharacteristic->notify();
    //vTaskDelay(10/portTICK_RATE_MS);
    //Set New Watts.
    int remainder, quotient;
    quotient = userConfig.getSimulatedWatts() / 256;
    remainder = userConfig.getSimulatedWatts() % 256;
    cyclingPowerMeasurement[2] = remainder;
    cyclingPowerMeasurement[3] = quotient;
    cyclingPowerMeasurementCharacteristic->setValue(cyclingPowerMeasurement, 19);
    cyclingPowerMeasurementCharacteristic->notify();
    //vTaskDelay(10/portTICK_RATE_MS);
    fitnessMachineFeature->notify();
    //vTaskDelay(2000/portTICK_RATE_MS);
    //pfitnessMachineControlPoint->notify();
  }
}

double computePID(double inp, double Setpoint)
{
  currentTime = millis();                             //get current time
  elapsedTime = (double)(currentTime - previousTime); //compute time elapsed from previous computation

  error = Setpoint - inp;                        // determine error
  cumError += error * elapsedTime;               // compute integral
  rateError = (error - lastError) / elapsedTime; // compute derivative

  double out = kp * error + ki * cumError + kd * rateError; //PID output

  lastError = error;          //remember current error
  previousTime = currentTime; //remember current time

  return out; //have function return the PID output
}