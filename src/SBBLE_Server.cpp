// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

/* Assioma Pedal Information for later
BLE Advertised Device found: Name: ASSIOMA17287L, Address: e8:fe:6e:91:9f:16, appearance: 1156, manufacturer data: 640302018743, serviceUUID: 00001818-0000-1000-8000-00805f9b34fb
*/

#include <SBBLE_Server.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <Main.h>

//BLE Server Settings
String BLEName = "SmartBike2K";
bool _BLEClientConnected = false;
int toggle = false;

/*BLE Client Variables/Settings
int scanTime = 5; //In seconds
bool doConnect = false;
bool connected = false;
static BLEAdvertisedDevice *myDevice;
//static BLERemoteCharacteristic* pRemoteHeartRateCharacteristic;  May need to configure these in order to do 2 notify callbacks.
//static BLERemoteCharacteristic* pRemoteCylingPowerCharacteristic;
BLERemoteCharacteristic *pBLERemoteCharacteristic; */

//PID Constants
const double kp = 1.5;
const double ki = 0;
const double kd = 0;
//PID Variables
unsigned long currentTime, previousTime;
double elapsedTime;
double error;
double lastError = 0;
double input, output, setPoint;
double cumError, rateError;
//Cadence computation Variables
long crankRev[2] = {0, 0};
long crankEventTime[2] = {0, 0};

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
byte cyclingPowerMeasurement[13] = {0b0000000100011, 0, 200, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};                              // 3rd & 2nd byte is reported power
byte ftmsService[16] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};                                                 // 5th byte to enable the FTMS bike service
byte ftmsFeature[32] = {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; //3rd bit enables incline support
byte ftmsControlPoint[8] = {0, 0, 0, 0, 0, 0, 0, 0};                                                                     //0x08 we need to return a value of 1 for any sucessful change
byte ftmsMachineStatus[8] = {0, 0, 0, 0, 0, 0, 0, 0};                                                                    //server needs to expose this. Not certain what it does.

#define HEARTSERVICE_UUID BLEUUID((uint16_t)0x180D)
#define HEARTCHARACTERISTIC_UUID BLEUUID((uint16_t)0x2A37)

#define CSCSERVICE_UUID BLEUUID((uint16_t)0x1816)
#define CSCMEASUREMENT_UUID BLEUUID((uint16_t)0x2A5B)

#define CYCLINGPOWERSERVICE_UUID BLEUUID((uint16_t)0x1818)
#define CYCLINGPOWERMEASUREMENT_UUID BLEUUID((uint16_t)0x2A63)

/*INFO FOR CALCULATING CADENCE FROM THE POWER SERVICE

Instantaneous Cadence = (Difference in two successive Cumulative Crank Revolutions
values) / (Difference in two successive Last Crank Event Time values)

So probably do somthing like this to calculate cadence:

long crankRev[2] = 0,0;
long crankEventTime[2] = 0.0;
crankRev[2] = crankRev[1]; 
crankRev[1] = BLEMeasurement;
crankEventTime[2] = crankEventTime[1];
crankEventTime[1] = BLEMeasurement;
cadenceFromPowerMeter = (crankRev[1]-crankRev[2])/(crankEventTime[1]-crankEventTime[2]);  

To make more fun: The ‘crank event time’ is a free-running-count of 1/1024 second units and it represents the time
when the crank revolution was detected by the crank rotation sensor. Since several crank
events can occur between transmissions, only the Last Crank Event Time value is transmitted.
The Last Crank Event Time value rolls over every 64 seconds.*/

//FitnessMachineServiceDefines//
#define FITNESSMACHINESERVICE_UUID BLEUUID((uint16_t)0x1826)
#define FITNESSMACHINEFEATURE_UUID BLEUUID((uint16_t)0x2ACC)
#define FITNESSMACHINECONTROLPOINT_UUID BLEUUID((uint16_t)0x2AD9)
#define FITNESSMACHINESTATUS_UUID BLEUUID((uint16_t)0x2ADA)

// This creates a macro that converts 8 bit LSB,MSB to Signed 16b
#define bytes_to_s16(MSB, LSB) (((signed int)((signed char)MSB))) << 8 | (((signed char)LSB))
#define bytes_to_u16(MSB, LSB) (((signed int)((signed char)MSB))) << 8 | (((unsigned char)LSB))
// Potentially, something like this is a better way of doing this ^^  data.getUint16(1, true)

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

/***********************************BLE CLIENT. ****************************************/

// The remote service we wish to connect to.
static BLEUUID serviceUUID = CYCLINGPOWERSERVICE_UUID;
// The characteristic of the remote service we are interested in.
static BLEUUID charUUID = CYCLINGPOWERMEASUREMENT_UUID;

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic *pRemoteCharacteristic;
static BLEAdvertisedDevice *myDevice;

static void notifyCallback(
    BLERemoteCharacteristic *pBLERemoteCharacteristic,
    uint8_t *pData,
    size_t length,
    bool isNotify)
{
  Serial.print("Notify callback for characteristic ");
  Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
  Serial.print(" of data length ");
  Serial.println(length);
  Serial.print("data: ");
  Serial.println((char *)pData);
    for (int i = 0; i < length; i++)
  {
    Serial.printf("%x ,", pData[i]);
  }

  if (pBLERemoteCharacteristic->getUUID().toString() == HEARTCHARACTERISTIC_UUID.toString())
  {
    userConfig.setSimulatedHr((int)pData[1]);
  }

  if (pBLERemoteCharacteristic->getUUID().toString() == CYCLINGPOWERMEASUREMENT_UUID.toString())
  {
    for (int i = 0; i < length; i++)
    {
      cyclingPowerMeasurement[i] = (byte)pData[i];
      i++;
    }

    userConfig.setSimulatedWatts(bytes_to_u16(pData[3], pData[2]));
    //This needs to be changed to read the bit field because this data could potentially shift positions in the characteristic
    //depending on what other fields are activated.

    if ((int)pData[0] == 35) //Don't let the hex to decimal confuse you. 
    { //last crank time is present in power Measurement data, lets extract it
      crankRev[1] = crankRev[1];
      crankRev[0] = bytes_to_u16(pData[6], pData[5]);
      crankEventTime[1] = crankEventTime[1];
      crankEventTime[0] = bytes_to_u16(pData[8], pData[7]);
      userConfig.setSimulatedCad((crankRev[0] - crankRev[1]) / (crankEventTime[0] - crankEventTime[1]));
      Serial.printf("Calculated Cadence was: %d", userConfig.getSimulatedCad());
    }
  }
}

/**  None of these are required as they will be handled by the library with defaults. **
 **                       Remove as you see fit for your needs                        */
class MyClientCallback : public BLEClientCallbacks
{
  void onConnect(BLEClient *pclient)
  {
  }

  void onDisconnect(BLEClient *pclient)
  {
    connected = false;
    Serial.println("onDisconnect");
  }
  /***************** New - Security handled here ********************
****** Note: these are the same return values as defaults ********/
  uint32_t onPassKeyRequest()
  {
    Serial.println("Client PassKeyRequest");
    return 123456;
  }
  bool onConfirmPIN(uint32_t pass_key)
  {
    Serial.print("The passkey YES/NO number: ");
    Serial.println(pass_key);
    return true;
  }

  void onAuthenticationComplete(ble_gap_conn_desc desc)
  {
    Serial.println("Starting BLE work!");
  }
  /*******************************************************************/
};

bool connectToServer()
{

  int sucessful = 0;

  Serial.print("Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());

  BLEClient *pClient = BLEDevice::createClient();
  Serial.println(" - Created client");

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remove BLE Server.
  pClient->connect(myDevice); // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  Serial.println(" - Connected to server");

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr)
  {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
  }
  else
  {
    Serial.println(" - Found service:");
    Serial.println(pRemoteService->getUUID().toString().c_str());
    sucessful++;

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr)
    {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
    }
    else
    {
      Serial.println(" - Found Characteristic:");
      Serial.println(pRemoteCharacteristic->getUUID().toString().c_str());
      sucessful++;
    }

    // Read the value of the characteristic.
    if (pRemoteCharacteristic->canRead())
    {
      std::string value = pRemoteCharacteristic->readValue();
      Serial.print("The characteristic value was: ");
      Serial.println(value.c_str());
    }

    if (pRemoteCharacteristic->canNotify())
    {
      Serial.println("Subscribed to notifications");
      pRemoteCharacteristic->registerForNotify(notifyCallback);
    }
    else
    {
      Serial.println("Unable to subscribe to notifications");
    }
  }
  if (sucessful > 0)
  {
    connected = true;
    return true;
  }
  pClient->disconnect();
  return false;
}

/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  /**
   * Called for each advertising BLE server.
   */

  /*** Only a reference to the advertised device is passed now
  void onResult(BLEAdvertisedDevice advertisedDevice) { **/
  void onResult(BLEAdvertisedDevice *advertisedDevice)
  {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice->toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    /********************************************************************************
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
********************************************************************************/
    if (advertisedDevice->haveServiceUUID() && advertisedDevice->isAdvertisingService(serviceUUID))
    {

      BLEDevice::getScan()->stop();
      /*******************************************************************
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
*******************************************************************/
      myDevice = advertisedDevice; /** Just save the reference now, no need to copy the object */
      doConnect = true;
      doScan = true;

    } // Found our server
  }   // onResult
};    // MyAdvertisedDeviceCallbacks

void setupBLE() //COmmon BLE setup for both Client and sServer
{

  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init(BLEName.c_str());

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
} // End of setup.

// This is the Arduino main loop function.
void bleClient()
{

  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
  if (doConnect == true)
  {
    if (connectToServer())
    {
      Serial.println("We are now connected to the BLE Server.");
    }
    else
    {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
    doConnect = false;
  }

  // If we are connected to a peer BLE Server, update the characteristic each time we are reached
  // with the current time since boot.
  if (connected)
  {
    Serial.println("Recieved data from server:");
    Serial.println(pRemoteCharacteristic->getUUID().toString().c_str());
    Serial.println(pRemoteCharacteristic->getValue().c_str());
  }
  else if (doScan)
  {
    Serial.println("Scanning Again for reconnect");
    BLEDevice::getScan()->start(0); // this is just eample to start scan after disconnect, most likely there is better way to do it in arduino
  }

  //delay(1000); // Delay a second between loops.
} // End of loop

void BLEServerScan(bool connectRequest)
{
  doConnect = connectRequest;
  Serial.println("Scanning for BLE servers and putting them into a list...");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  BLEScanResults foundDevices = pBLEScan->start(5, false);

  // Load the scan into a Json String
  int count = foundDevices.getCount();

  StaticJsonDocument<500> devices;

  String device = "";
  //JsonObject root = devices.createNestedObject();
  //JsonArray server = devices.createNestedObject("server");
  for (int i = 0; i < count; i++)
  {
    BLEAdvertisedDevice d = foundDevices.getDevice(i);
    if (d.isAdvertisingService(CYCLINGPOWERSERVICE_UUID) || d.isAdvertisingService(HEARTSERVICE_UUID))
    {
      device = "device " + String(i);
      devices[device]["address"] = d.getAddress().toString();

      if (d.haveName())
      {
        devices[device]["name"] = d.getName();
      }

      if (d.haveServiceUUID())
      {
        devices[device]["UUID"] = d.getServiceUUID().toString();
      }
    }
  }
  String output;
  serializeJson(devices, output);
  Serial.println(output);
  //userConfig.setfoundDevices(output);

  if (doConnect)
  { //Works but inhibits the BLE Server Scan. Too late at night to fix. another day.
    connectToServer();
  }
}



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
  Serial.println("Starting BLE Server");
  //if(!BLEDevice::getInitialized())
  //{
  //  BLEDevice::init(BLEName.c_str());
  //  Serial.println("Device previously initialized");
  //}
  Serial.println(" - Initializing BLE Device in Server");
  NimBLEServer *pServer = BLEDevice::createServer();
  Serial.println(" - Server Created");

  //HEART RATE MONITOR SERVICE SETUP
  BLEService *pHeartService = pServer->createService(HEARTSERVICE_UUID);
  heartRateMeasurementCharacteristic = pHeartService->createCharacteristic(
      HEARTCHARACTERISTIC_UUID,
      NIMBLE_PROPERTY::READ |
          NIMBLE_PROPERTY::NOTIFY);

  //Power Meter MONITOR SERVICE SETUP
  BLEService *pPowerMonitor = pServer->createService(CYCLINGPOWERSERVICE_UUID);
  cyclingPowerMeasurementCharacteristic = pPowerMonitor->createCharacteristic(
      CYCLINGPOWERMEASUREMENT_UUID,
      NIMBLE_PROPERTY::READ |
          NIMBLE_PROPERTY::NOTIFY);

  //Fitness Machine service setup
  BLEService *pFitnessMachineService = pServer->createService(FITNESSMACHINESERVICE_UUID);
  fitnessMachineFeature = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINEFEATURE_UUID,
      NIMBLE_PROPERTY::READ |
          NIMBLE_PROPERTY::NOTIFY);

  BLECharacteristic *fitnessMachineControlPoint = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINECONTROLPOINT_UUID,
      NIMBLE_PROPERTY::WRITE |
          NIMBLE_PROPERTY::NOTIFY |
          NIMBLE_PROPERTY::INDICATE);

  BLECharacteristic *fitnessMachineStatus = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINESTATUS_UUID,
      NIMBLE_PROPERTY::READ |
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

  pHeartService->start();          //heart rate service
  pPowerMonitor->start();          //Power Meter Service
  pFitnessMachineService->start(); //Fitness Machine Service

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(HEARTSERVICE_UUID);
  pAdvertising->addServiceUUID(CYCLINGPOWERSERVICE_UUID);
  pAdvertising->addServiceUUID(FITNESSMACHINESERVICE_UUID);
  pAdvertising->setScanResponse(true);

  BLEDevice::startAdvertising();

  Serial.println("Bluetooth Characteristic defined!");
}

void BLENotify()
{ //!!!!!!!!!!!!!!!!High priority to spin this and the http loop both off as their own xtasks

  if (_BLEClientConnected)
  {
    //update the BLE information on the server
    heartRateMeasurement[1] = userConfig.getSimulatedHr();
    //cyclingPowerMeasurement[2] = userConfig.getSimulatedWatts();
    heartRateMeasurementCharacteristic->setValue(heartRateMeasurement, 5);
    heartRateMeasurementCharacteristic->notify();
    //vTaskDelay(10/portTICK_RATE_MS);
    //Set New Watts.
    int remainder, quotient;
    quotient = userConfig.getSimulatedWatts() / 256;
    remainder = userConfig.getSimulatedWatts() % 256;
    cyclingPowerMeasurement[2] = remainder;
    cyclingPowerMeasurement[3] = quotient;
    cyclingPowerMeasurementCharacteristic->setValue(cyclingPowerMeasurement, 13);
    Serial.println("CyclingPowerMeasurementCharacteristic notify value:");
    Serial.println((char *)cyclingPowerMeasurement);
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