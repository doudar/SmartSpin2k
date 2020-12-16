// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

/* Assioma Pedal Information for later
BLE Advertised Device found: Name: ASSIOMA17287L, Address: e8:fe:6e:91:9f:16, appearance: 1156, manufacturer data: 640302018743, serviceUUID: 00001818-0000-1000-8000-00805f9b34fb
*/

#include "Main.h"
#include "SBBLE_Server.h"

#include <ArduinoJson.h>
#include <NimBLEDevice.h>

// macros to convert different types of bytes into int The naming here sucks and should be fixed.
#define bytes_to_s16(MSB, LSB) (((signed int)((signed char)MSB))) << 8 | (((signed char)LSB))
#define bytes_to_u16(MSB, LSB) (((signed int)((signed char)MSB))) << 8 | (((unsigned char)LSB))
#define bytes_to_int(MSB, LSB) (((int)((unsigned char)MSB))) << 8 | (((unsigned char)LSB))
// Potentially, something like this is a better way of doing this ^^  data.getUint16(1, true)

//BLE Server Settings
bool _BLEClientConnected = false;
int toggle = false;

//Cadence computation Variables
float crankRev[2] = {0, 0};
float crankEventTime[2] = {0, 0};
int noReadingIn = 0;

//Simulated Cadence Variables
int cscCumulativeCrankRev = 0;
int cscLastCrankEvtTime = 0;

//BLEAdvertisementData advertisementData = BLEAdvertisementData();
BLECharacteristic *heartRateMeasurementCharacteristic;
BLECharacteristic *cyclingPowerMeasurementCharacteristic;
BLECharacteristic *fitnessMachineFeature;
BLECharacteristic *fitnessMachineIndoorBikeData;

int reconnectTries = MAX_RECONNECT_TRIES;

//New Remote Charistics to get rid of notify callbacks
NimBLERemoteCharacteristic *remoteCyclingPowerCharacteristic;
NimBLERemoteCharacteristic *remoteHeartRateMearurementCharacteristic;

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
// 98765432109876543210 - bit placement helper :)
// 00001110000000001100
// 00000101000010000110
// 00000000100001010100
//               100000
byte heartRateMeasurement[5] = {0b00000, 60, 0, 0, 0};
byte cyclingPowerMeasurement[9] = {0b0000000100011, 0, 200, 0, 0, 0, 0, 0, 0};
byte cpsLocation[1] = {0b000};    //sensor location 5 == left crank
byte cpFeature[1] = {0b00100000}; //crank information present                                         // 3rd & 2nd byte is reported power

byte ftmsService[6] = {0x00, 0x00, 0x00, 0b01, 0b0100000, 0x00};
byte ftmsControlPoint[8] = {0, 0, 0, 0, 0, 0, 0, 0}; //0x08 we need to return a value of 1 for any sucessful change
byte ftmsMachineStatus[8] = {0, 0, 0, 0, 0, 0, 0, 0};

uint8_t ftmsFeature[8] = {0x86, 0x50, 0x00, 0x00, 0x0C, 0xE0, 0x00, 0x00};                            //101000010000110 1110000000001100
uint8_t ftmsIndoorBikeData[13] = {0x54, 0x08, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}; //00000000100001010100 ISpeed, ICAD, TDistance, IPower, ETime
uint8_t ftmsResistanceLevelRange[6] = {0x00, 0x00, 0x3A, 0x98, 0xC5, 0x68};                           //+-15000
uint8_t ftmsPowerRange[6] = {0x00, 0x00, 0xA0, 0x0F, 0x01, 0x00};                                     //1-4000

TaskHandle_t BLENotifyTask;
TaskHandle_t BLEClientTask;

//Creating Server Callbacks
class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    _BLEClientConnected = true;
    debugDirector("Bluetooth Client Connected!");
    // vTaskDelay(5000);
  };

  void onDisconnect(BLEServer *pServer)
  {
    _BLEClientConnected = false;
    debugDirector("Bluetooth Client Disconnected!");
    // vTaskDelay(5000);
  }
};

/***********************************BLE CLIENT. ****************************************/

static boolean doConnectPM = false;
static boolean doConnectHR = false;
static boolean connectedPM = false;
static boolean connectedHR = false;

static boolean doScan = false;
static BLERemoteCharacteristic *pRemoteCharacteristic;
static BLEAdvertisedDevice *myPowerMeter = nullptr;
static BLEAdvertisedDevice *myHeartMonitor = nullptr;

static void notifyCallback(
    BLERemoteCharacteristic *pBLERemoteCharacteristic,
    uint8_t *pData,
    size_t length,
    bool isNotify)
{

  for (int i = 0; i < length; i++)
  {
    debugDirector(String(pData[i], HEX) + " ", false);
  }
  debugDirector("<-- " + String(pBLERemoteCharacteristic->getUUID().toString().c_str()), false);

  if (pBLERemoteCharacteristic->getUUID() == HEARTCHARACTERISTIC_UUID)
  {
    userConfig.setSimulatedHr((int)pData[1]);
    debugDirector(" HRM: " + String(userConfig.getSimulatedHr()), false);
    debugDirector("");
  }

  if (pBLERemoteCharacteristic->getUUID() == CYCLINGPOWERMEASUREMENT_UUID)
  {

    userConfig.setSimulatedWatts(bytes_to_u16(pData[3], pData[2]));
    //This needs to be changed to read the bit field because this data could potentially shift positions in the characteristic
    //depending on what other fields are activated.

    if ((int)pData[0] == 35) //Don't let the hex to decimal confuse you.
    {                        //last crank time is present in power Measurement data, lets extract it
      crankRev[1] = crankRev[0];
      crankRev[0] = bytes_to_int(pData[6], pData[5]);
      crankEventTime[1] = crankEventTime[0];
      crankEventTime[0] = bytes_to_int(pData[8], pData[7]);

      if ((crankRev[0] > crankRev[1]) && (crankEventTime[0] - crankEventTime[1] != 0))
      {
        userConfig.setSimulatedCad(((abs(crankRev[0] - crankRev[1]) * 1024) / abs(crankEventTime[0] - crankEventTime[1])) * 60);
        noReadingIn = 0;
      }
      else //the crank rev probably didn't update
      {
        if (noReadingIn > 2) //Require three consecutive readings before setting 0 cadence
        {
          userConfig.setSimulatedCad(0);
        }
        noReadingIn ++;
      }

      debugDirector(" CAD: " + String(userConfig.getSimulatedCad()), false);
      debugDirector("");
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
    if ((pclient->getService(HEARTSERVICE_UUID)) && (!(pclient->isConnected())))
    {
      debugDirector("Detected HR Disconnect. Trying rapid reconnect");
      connectedHR = false;
      doConnectHR = true; //try rapid reconnect
      return;
    }
    if ((pclient->getService(CYCLINGPOWERSERVICE_UUID)) && (!(pclient->isConnected())))
    {

      debugDirector("Detected PM Disconnect. Trying rapid reconnect");
      connectedPM = false;
      doConnectPM = true; //try rapid reconnect
      return;
    }
  }

  /***************** New - Security handled here ********************
****** Note: these are the same return values as defaults ********/
  uint32_t onPassKeyRequest()
  {
    debugDirector("Client PassKeyRequest");
    return 123456;
  }
  bool onConfirmPIN(uint32_t pass_key)
  {
    debugDirector("The passkey YES/NO number: " + String(pass_key));
    return true;
  }

  void onAuthenticationComplete(ble_gap_conn_desc desc)
  {
    debugDirector("Starting BLE work!");
  }
  /*******************************************************************/
};

bool connectToServer()
{

  debugDirector("Initiating Server Connection");
  NimBLEUUID serviceUUID;
  NimBLEUUID charUUID;

  int sucessful = 0;
  BLEAdvertisedDevice *myDevice;
  if (doConnectPM)
  {
    myDevice = myPowerMeter;
    serviceUUID = CYCLINGPOWERSERVICE_UUID;
    charUUID = CYCLINGPOWERMEASUREMENT_UUID;
    debugDirector("trying to connect to PM");
  }
  else if (doConnectHR)
  {
    myDevice = myHeartMonitor;
    serviceUUID = HEARTSERVICE_UUID;
    charUUID = HEARTCHARACTERISTIC_UUID;
    debugDirector("Trying to connect to HRM");
  }
  else
  {
    debugDirector("no doConnect");
    return false;
  }

  NimBLEClient *pClient;

  // Check if we have a client we should reuse first
  if (NimBLEDevice::getClientListSize() > 1)
  {
    // Special case when we already know this device, we send false as the
    //     *  second argument in connect() to prevent refreshing the service database.
    //     *  This saves considerable time and power.
    //     *
    pClient = NimBLEDevice::getClientByPeerAddress(myDevice->getAddress());
    debugDirector("Reusing Client");
    if (pClient)
    {
      debugDirector("Client RSSI " + String(pClient->getRssi()));
      debugDirector("device RSSI " + String(myDevice->getRSSI()));
      if (myDevice->getRSSI() == 0)
      {
        debugDirector("no signal detected. abortng.");
        reconnectTries--;
        return false;
      }
      pClient->disconnect();
      vTaskDelay(100 / portTICK_PERIOD_MS);
      if (!pClient->connect(myDevice->getAddress(), true))
      {
        Serial.println("Reconnect failed ");
        reconnectTries--;
        debugDirector(String(reconnectTries) + " left.");
        if (reconnectTries < 1)
        {
          if (myDevice == myPowerMeter)
          {
            myPowerMeter = nullptr;
            doConnectPM = false;
            connectedPM = false;
          }
          if (myDevice == myHeartMonitor)
          {
            myHeartMonitor = nullptr;
            doConnectHR = false;
            connectedHR = false;
          }
        }
        return false;
      }
      Serial.println("Reconnected client");
      pClient->setClientCallbacks(new MyClientCallback(), true);
      BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
      pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
      if (pRemoteCharacteristic == nullptr)
      {
        debugDirector("Couldn't find Characteristic");
        reconnectTries--;
        return false;
      }
      if (pRemoteCharacteristic->canNotify())
      {

        pRemoteCharacteristic->subscribe(true, notifyCallback);
        if (pRemoteService->getUUID() == CYCLINGPOWERSERVICE_UUID)
        {
          debugDirector("Found PM on reconnect");
          connectedPM = true;
          doConnectPM = false;
          reconnectTries = MAX_RECONNECT_TRIES;
        }

        if (pRemoteService->getUUID() == HEARTSERVICE_UUID)
        {
          debugDirector("Found HRM on reconnect");
          connectedHR = true;
          doConnectHR = false;
          reconnectTries = MAX_RECONNECT_TRIES;
        }
        return true;
      }
      else
      {
        debugDirector("Unable to subscribe to notifications");
        return false;
      }
    }
    /** We don't already have a client that knows this device,
         *  we will check for a client that is disconnected that we can use.
         */

    else
    {
      debugDirector("Reconnect failed in a way I don't understand");
      //pClient = NimBLEDevice::getDisconnectedClient();
    }
  }

  debugDirector("Forming a connection to: " + String(myDevice->getAddress().toString().c_str()));
  pClient = NimBLEDevice::createClient();
  debugDirector(" - Created client", false);
  pClient->setClientCallbacks(new MyClientCallback(), true);
  // Connect to the remove BLE Server.
  pClient->setConnectionParams(24, 24, 0, 102);
  /** Set how long we are willing to wait for the connection to complete (seconds), default is 30. */
  pClient->setConnectTimeout(5);
  pClient->connect(myDevice->getAddress()); // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  debugDirector(" - Connected to server", true);
  debugDirector(" - RSSI " + pClient->getRssi(), true);

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr)
  {
    debugDirector("Failed to find service:" + String(serviceUUID.toString().c_str()));
  }
  else
  {
    debugDirector(" - Found service:" + String(pRemoteService->getUUID().toString().c_str()));
    sucessful++;

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr)
    {
      debugDirector("Failed to find our characteristic UUID: " + String(charUUID.toString().c_str()));
    }
    else
    {
      debugDirector(" - Found Characteristic:" + String(pRemoteCharacteristic->getUUID().toString().c_str()));
      sucessful++;
    }

    // Read the value of the characteristic.
    if (pRemoteCharacteristic->canRead())
    {
      std::string value = pRemoteCharacteristic->readValue();
      debugDirector("The characteristic value was: " + String(value.c_str()));
    }

    if (pRemoteCharacteristic->canNotify())
    {
      debugDirector("Subscribed to notifications");
      pRemoteCharacteristic->subscribe(true, notifyCallback);
    }
    else
    {
      debugDirector("Unable to subscribe to notifications");
    }
  }
  if (sucessful > 0)
  {

    if (pRemoteService->getUUID() == CYCLINGPOWERSERVICE_UUID)
    {
      connectedPM = true;
      doConnectPM = false;
      debugDirector("Sucessful PM");
    }

    if (pRemoteService->getUUID() == HEARTSERVICE_UUID)
    {
      connectedHR = true;
      doConnectHR = false;
      debugDirector("Sucessful HRM");
    }
    debugDirector("Returning True");
    reconnectTries = MAX_RECONNECT_TRIES;
    return true;
  }
  debugDirector("disconnecting Client");
  pClient->disconnect();
  return false;
}

/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{

  void onResult(BLEAdvertisedDevice *advertisedDevice)
  {
    debugDirector("BLE Advertised Device found: " + String(advertisedDevice->toString().c_str()));

    if ((advertisedDevice->haveServiceUUID()) && (advertisedDevice->isAdvertisingService(CYCLINGPOWERSERVICE_UUID)))
    {
      if ((advertisedDevice->getName() == userConfig.getconnectedPowerMeter()) || (advertisedDevice->getAddress().toString().c_str() == userConfig.getconnectedPowerMeter()) || (String(userConfig.getconnectedPowerMeter()) == ("any")))
      {
        myPowerMeter = advertisedDevice;
        doConnectPM = true;
        doScan = true;
      }
    }
    if ((advertisedDevice->haveServiceUUID()) && (advertisedDevice->isAdvertisingService(HEARTSERVICE_UUID)))
    {
      if ((advertisedDevice->getName() == userConfig.getconnectedHeartMonitor()) || (advertisedDevice->getAddress().toString().c_str() == userConfig.getconnectedHeartMonitor()) || (String(userConfig.getconnectedHeartMonitor()) == ("any")))
      {
        myHeartMonitor = advertisedDevice;
        doConnectHR = true;
        doScan = true;
      }
    }
  }
};

void setupBLE() //Common BLE setup for both Client and Server
{
  debugDirector("Starting Arduino BLE Client application...");
  BLEDevice::init(userConfig.getDeviceName());

  xTaskCreatePinnedToCore(
      bleClient,       /* Task function. */
      "BLEClientTask", /* name of task. */
      3500,            /* Stack size of task */
      NULL,            /* parameter of the task */
      18,              /* priority of the task  - 29 worked  at 1 I get stuttering */
      &BLEClientTask,  /* Task handle to keep track of created task */
      1);

} // End of setup.

// This is the Arduino main loop function.
void bleClient(void *pvParameters)
{
  for (;;)
  {

    if ((doConnectPM == true) || (doConnectHR == true))
    {
      if (connectToServer())
      {
        debugDirector("We are now connected to the BLE Server.");
      }
      else
      {
      
      }
    }

    if ((connectedPM) || (connectedHR))
    {
    }
    else if (doScan)
    {
    }

    vTaskDelay(BLE_CLIENT_DELAY / portTICK_PERIOD_MS); // Delay a second between loops.
    //debugDirector("BLEclient High Water Mark: " + String(uxTaskGetStackHighWaterMark(BLEClientTask)));
  }
}

void BLEServerScan(bool connectRequest)
{
  //doConnect = connectRequest;
  debugDirector("Scanning for BLE servers and putting them into a list...");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  //pBLEScan->setInterval(1349);
  //pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  BLEScanResults foundDevices = pBLEScan->start(10, false);

  // Load the scan into a Json String
  int count = foundDevices.getCount();

  StaticJsonDocument<500> devices;

  String device = "";

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
  debugDirector("Bluetooth Client Found Devices: " + output);
  userConfig.setFoundDevices(output);

  // if (doConnect)
  // {
  //   connectToServer("0x1818");
  // }
}

/**************BLE Server Callbacks *************************/
class MyCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    std::string rxValue = pCharacteristic->getValue();

    if (rxValue.length() > 1)
    {
      for (const auto &text : rxValue)
      { // Range-for!
        debugDirector(String(text, HEX) + " ", false);
      }
      debugDirector("<-- From APP ", false);
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
        debugDirector(" Target Incline: " + String((userConfig.getIncline() / 100)), false);
      }
      debugDirector("");
      /* 5 means FTMS Watts Control Mode (aka ERG mode) */

      if ((int)rxValue[0] == 5)
      {
        int targetWatts = bytes_to_int(rxValue[2], rxValue[1]);
        if (!userConfig.getERGMode())
        {
          userConfig.setERGMode(true);
        }
        computeERG(userConfig.getSimulatedWatts(), targetWatts);
        //userConfig.setIncline(computePID(userConfig.getSimulatedWatts(), targetWatts)); //Updating incline via PID...This should be interesting.....
        debugDirector("ERG MODE", false);
        debugDirector(" Target: " + String(targetWatts), false);
        debugDirector(" Current: " + String(userConfig.getSimulatedWatts()), false); //not displaying numbers less than 256 correctly but they do get sent to Zwift correctly.
        debugDirector(" Incline: " + String(userConfig.getIncline() / 100), false);
        debugDirector("");
      }
    }
  }
};

void startBLEServer()
{

  //Server Setup
  debugDirector("Starting BLE Server");
  //if(!BLEDevice::getInitialized())
  //{
  //  BLEDevice::init(BLEName.c_str());
  //  debugDirector("Device previously initialized");
  //}
  debugDirector(" - Initializing BLE Device in Server");
  NimBLEServer *pServer = BLEDevice::createServer();
  debugDirector(" - Server Created");

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

  BLECharacteristic *cyclingPowerFeatureCharacteristic = pPowerMonitor->createCharacteristic(
      CYCLINGPOWERFEATURE_UUID,
      NIMBLE_PROPERTY::READ);

  BLECharacteristic *sensorLocationCharacteristic = pPowerMonitor->createCharacteristic(
      SENSORLOCATION_UUID,
      NIMBLE_PROPERTY::READ);

  //Fitness Machine service setup
  BLEService *pFitnessMachineService = pServer->createService(FITNESSMACHINESERVICE_UUID);

  fitnessMachineFeature = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINEFEATURE_UUID,
      NIMBLE_PROPERTY::READ |
          NIMBLE_PROPERTY::WRITE |
          NIMBLE_PROPERTY::NOTIFY |
          NIMBLE_PROPERTY::INDICATE);

  BLECharacteristic *fitnessMachineControlPoint = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINECONTROLPOINT_UUID,
      NIMBLE_PROPERTY::WRITE |
          NIMBLE_PROPERTY::NOTIFY |
          NIMBLE_PROPERTY::INDICATE);

  BLECharacteristic *fitnessMachineStatus = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINESTATUS_UUID,
      NIMBLE_PROPERTY::READ |
          NIMBLE_PROPERTY::WRITE |
          NIMBLE_PROPERTY::NOTIFY);

  fitnessMachineIndoorBikeData = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINEINDOORBIKEDATA_UUID,
      NIMBLE_PROPERTY::READ |
          NIMBLE_PROPERTY::NOTIFY);

  BLECharacteristic *fitnessMachineResistanceLevelRange = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINERESISTANCELEVELRANGE_UUID,
      NIMBLE_PROPERTY::READ);

  BLECharacteristic *fitnessMachinePowerRange = pFitnessMachineService->createCharacteristic(
      FITNESSMACHINEPOWERRANGE_UUID,
      NIMBLE_PROPERTY::READ);

  pServer->setCallbacks(new MyServerCallbacks());
  //Creating Characteristics

  //Bluetooth Server Setup
  debugDirector("Starting BLE work!");

  //Create BLE Server

  heartRateMeasurementCharacteristic->setValue(heartRateMeasurement, 5);

  cyclingPowerMeasurementCharacteristic->setValue(cyclingPowerMeasurement, 9);
  cyclingPowerFeatureCharacteristic->setValue(cpFeature, 1);
  sensorLocationCharacteristic->setValue(cpsLocation, 1);

  fitnessMachineFeature->setValue(ftmsFeature, 8);
  fitnessMachineControlPoint->setValue(ftmsControlPoint, 8);
  fitnessMachineIndoorBikeData->setValue(ftmsIndoorBikeData, 13); //Maybe enable this later. Now just exposing the char and basically saying get it from the power service.
  fitnessMachineStatus->setValue(ftmsMachineStatus, 8);
  fitnessMachineResistanceLevelRange->setValue(ftmsResistanceLevelRange, 6);
  fitnessMachinePowerRange->setValue(ftmsPowerRange, 6);

  fitnessMachineControlPoint->setCallbacks(new MyCallbacks());

  pHeartService->start();          //heart rate service
  pPowerMonitor->start();          //Power Meter Service
  pFitnessMachineService->start(); //Fitness Machine Service

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(CYCLINGPOWERSERVICE_UUID);
  pAdvertising->addServiceUUID(FITNESSMACHINESERVICE_UUID);
  pAdvertising->addServiceUUID(HEARTSERVICE_UUID);

  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  debugDirector("Bluetooth Characteristic defined!");
  xTaskCreatePinnedToCore(
      BLENotify,       /* Task function. */
      "BLENotifyTask", /* name of task. */
      3000,            /* Stack size of task*/
      NULL,            /* parameter of the task */
      1,               /* priority of the task*/
      &BLENotifyTask,  /* Task handle to keep track of created task */
      1);              /* pin task to core 0 */

  debugDirector("BLE Notify Task Started");
}

void BLENotify(void *pvParameters)
{
  for (;;)
  {
    if (_BLEClientConnected)
    {
      //update the BLE information on the server
      heartRateMeasurement[1] = userConfig.getSimulatedHr();
      heartRateMeasurementCharacteristic->setValue(heartRateMeasurement, 5);
      computeCSC();
      updateIndoorBikeDataChar();
      updateCyclingPowerMesurementChar();
      cyclingPowerMeasurementCharacteristic->notify();
      fitnessMachineFeature->notify();
      fitnessMachineIndoorBikeData->notify();
      heartRateMeasurementCharacteristic->notify();
      GlobalBLEClientConnected = true;
    }
    else
    {
      GlobalBLEClientConnected = false;
    }
    if (!_BLEClientConnected)
    {
      digitalWrite(LED_PIN, LOW); //blink if no client connected
    }
    vTaskDelay((BLE_NOTIFY_DELAY / 2) / portTICK_PERIOD_MS);
    digitalWrite(LED_PIN, HIGH);
    vTaskDelay((BLE_NOTIFY_DELAY / 2) / portTICK_PERIOD_MS);
    //debugDirector("BLENotify High Water Mark: " + String(uxTaskGetStackHighWaterMark(BLENotifyTask)));
  }
}

void computeERG(int currentWatts, int setPoint)
{
  //cooldownTimer--;

  float incline = userConfig.getIncline();
  int cad = userConfig.getSimulatedCad();
  int newIncline = incline;
  int amountToChangeIncline = 0;

  if (cad > 20)
  {
    if (abs(currentWatts - setPoint) < 50)
    {
      amountToChangeIncline = (currentWatts - setPoint) * .5;
    }
    if (abs(currentWatts - setPoint) > 50)
    {
      amountToChangeIncline = amountToChangeIncline + ((currentWatts - setPoint)) * 1;
    }
    amountToChangeIncline = amountToChangeIncline / ((currentWatts / 100) + 1);
  }

  if (abs(amountToChangeIncline) > 2000)
  {
    if (amountToChangeIncline > 0)
    {
      amountToChangeIncline = 200;
    }
    if (amountToChangeIncline < 0)
    {
      amountToChangeIncline = -200;
    }
  }

  newIncline = incline - amountToChangeIncline; //  }
  userConfig.setIncline(newIncline);
}

void computeCSC() //What was SIG smoking when they came up with the Cycling Speed and Cadence Characteristic?
{
  if (userConfig.getSimulatedCad() > 0)
  {
    float crankRevPeriod = (60 * 1024) / userConfig.getSimulatedCad();
    cscCumulativeCrankRev++;
    cscLastCrankEvtTime += crankRevPeriod;
    int remainder, quotient;
    quotient = cscCumulativeCrankRev / 256;
    remainder = cscCumulativeCrankRev % 256;
    cyclingPowerMeasurement[5] = remainder;
    cyclingPowerMeasurement[6] = quotient;
    quotient = cscLastCrankEvtTime / 256;
    remainder = cscLastCrankEvtTime % 256;
    cyclingPowerMeasurement[7] = remainder;
    cyclingPowerMeasurement[8] = quotient;
  }
}

void updateIndoorBikeDataChar()
{
  int cad = userConfig.getSimulatedCad();
  int watts = userConfig.getSimulatedWatts();
  float gearRatio = 1;
  int speed = ((cad * 2.75 * 2.08 * 60 * gearRatio) / 10);
  ftmsIndoorBikeData[2] = (uint8_t)(speed & 0xff);
  ftmsIndoorBikeData[3] = (uint8_t)(speed >> 8);
  ftmsIndoorBikeData[4] = (uint8_t)((cad * 2) & 0xff);
  ftmsIndoorBikeData[5] = (uint8_t)((cad * 2) >> 8); // cadence value
  ftmsIndoorBikeData[6] = 0;                         //distance <
  ftmsIndoorBikeData[7] = 0;                         //distance <-- uint24 with 1m resolution
  ftmsIndoorBikeData[8] = 0;                         //distance <
  ftmsIndoorBikeData[9] = (uint8_t)((watts)&0xff);
  ftmsIndoorBikeData[10] = (uint8_t)((watts) >> 8); // power value, constrained to avoid negative values, although the specification allows for a sint16
  ftmsIndoorBikeData[11] = 0;                       // Elapsed Time uint16 in seconds
  ftmsIndoorBikeData[12] = 0;                       // Elapsed Time
  fitnessMachineIndoorBikeData->setValue(ftmsIndoorBikeData, 13);
}

void updateCyclingPowerMesurementChar()
{
  int remainder, quotient;
  quotient = userConfig.getSimulatedWatts() / 256;
  remainder = userConfig.getSimulatedWatts() % 256;
  cyclingPowerMeasurement[2] = remainder;
  cyclingPowerMeasurement[3] = quotient;
  cyclingPowerMeasurementCharacteristic->setValue(cyclingPowerMeasurement, 9);
  debugDirector("");
  for (const auto &text : cyclingPowerMeasurement)
  { // Range-for!
    debugDirector(String(text, HEX) + " ", false);
  }

  debugDirector("<-- CPMC sent ", false);
  debugDirector("");
}