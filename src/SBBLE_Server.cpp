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
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <ArduinoJson.h>

//BLE Server Settings
String BLEName = "SmartBike2K";
bool _BLEClientConnected = false;
int toggle = false;
//int numDevices = 0;
//String foundDevices[];

byte heartRateMeasurement[5] = {0b00000, 60, 0, 0, 0};
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
#define bytes_to_u16(MSB, LSB) (((unsigned int)((unsigned char)MSB))) << 8 | (((unsigned char)LSB))

//Creating Characteristics
BLECharacteristic heartRateMeasurementCharacteristic(
    HEARTCHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_NOTIFY);

BLECharacteristic cyclingPowerMeasurementCharacteristic(
    CYCLINGPOWERMEASUREMENT_UUID,
    BLECharacteristic::PROPERTY_NOTIFY);

BLECharacteristic fitnessMachineFeature(
    FITNESSMACHINEFEATURE_UUID,
    BLECharacteristic::PROPERTY_READ);

BLECharacteristic fitnessMachineControlPoint(
    FITNESSMACHINECONTROLPOINT_UUID,
    BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY |
        BLECharacteristic::PROPERTY_INDICATE);

BLECharacteristic fitnessMachineStatus(
    FITNESSMACHINESTATUS_UUID,
    BLECharacteristic::PROPERTY_NOTIFY);

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
/***********************************BLE CLIENT. EXPERIMENTAL TESTING *******************/ 
// The remote service we wish to connect to.
static BLEUUID heartServiceUUID((uint16_t)0x180D); //Could eventually set these to the defines we have at the top of the file.
static BLEUUID powerServiceUUID((uint16_t)0x1818);
// The characteristic of the remote service we are interested in.

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic *pRemoteCharacteristic;
static BLEAdvertisedDevice *myDevice;

//Testing server notify with heart rate data
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
  //Serial.println((char *)pData);
for(int i = 0; i<length; i++){
   Serial.printf("%x ,", pData[i]);
}
Serial.println("");
  if(pBLERemoteCharacteristic->getUUID().toString() == HEARTCHARACTERISTIC_UUID.toString())
  {
  userConfig.setSimulatedHr((int)pData[1]);
  }
  if(pBLERemoteCharacteristic->getUUID().toString() == CYCLINGPOWERMEASUREMENT_UUID.toString())
  {
  userConfig.setSimulatedWatts(bytes_to_u16(pData[2], pData[3]));
  }
}

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
};

bool connectToServer()
{
  Serial.print("Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());

  BLEClient *pClient = BLEDevice::createClient();
  Serial.println(" - Created client");

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remote BLE Server.
  pClient->connect(myDevice); // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  Serial.println(" - Connected to server");

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService *pRemoteService = pClient->getService(CYCLINGPOWERSERVICE_UUID);
  if (pRemoteService == nullptr)
  {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(CYCLINGPOWERSERVICE_UUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our service");

  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(CYCLINGPOWERMEASUREMENT_UUID);
  if (pRemoteCharacteristic == nullptr)
  {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(CYCLINGPOWERMEASUREMENT_UUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");

  // Read the value of the characteristic.
  if (pRemoteCharacteristic->canRead())
  {
    std::string value = pRemoteCharacteristic->readValue();
    Serial.print("The characteristic value was: ");
    Serial.println(value.c_str());
  }

  if (pRemoteCharacteristic->canNotify())
    pRemoteCharacteristic->registerForNotify(notifyCallback);

  connected = true;
  return true;
}
/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if ((advertisedDevice.haveName() && advertisedDevice.getName() == userConfig.getConnectedDevices()) || advertisedDevice.getAddress().toString() == userConfig.getConnectedDevices())
    {

      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;

    } // Found our server
  }   // onResult
};    // MyAdvertisedDeviceCallbacks

void BLEserverScan()
{ 
  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  BLEScanResults foundDevices = pBLEScan->start(5, false);

  //********************** Load the scan into a Json String
    int count = foundDevices.getCount();

  StaticJsonDocument<500> devices;
  
  String device = "";
  //JsonObject root = devices.createNestedObject();
  //JsonArray server = devices.createNestedObject("server");
  for (int i = 0; i < count; i++)
  {
    BLEAdvertisedDevice d = foundDevices.getDevice(i);
    if(d.isAdvertisingService(CYCLINGPOWERSERVICE_UUID) || d.isAdvertisingService(HEARTSERVICE_UUID)){
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
  userConfig.setfoundDevices(output);

  if(doConnect){  //Works but inhibits the BLE Server Scan. Too late at night to fix. another day. 
    connectToServer();
  }
}
// End of setup.

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
    //String newValue = "Time since boot: " + String(millis() / 1000);
    //std::string serverValue;
    // Serial.println("Setting new characteristic value to \"" + newValue + "\"");
    //
    // Set the characteristic's value to be the array of bytes that is actually a string.
    //pRemoteCharacteristic->writeValue(newValue.c_str(), newValue.length());
    //serverValue = pRemoteCharacteristic->readValue();

    //Serial.printf("Value from BLE Server Was: ");
    // for(const auto& text : serverValue) {   // Range-for!
    //     Serial.printf(" %d", text);
    //   }
    //    Serial.printf("\n");
    vTaskDelay(100/portTICK_PERIOD_MS);
  }
  else if (doScan)
  {
    Serial.println("Device disconnected. Scanning Again.");
    BLEDevice::getScan()->start(0); // this is just eample to start scan after disconnect, most likely there is better way to do it in arduino
  }

  // delay(1000); // Delay a second between loops.
} // End of loop

/**********************************END OF EXPERIMENTAL BLE CLIENT TESTING******************/



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

        int port = bytes_to_s16(buf[1], buf[0]);
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
        userConfig.setSimulatedWatts(bytes_to_s16(rxValue[2], rxValue[1]));
        if (!userConfig.getERGMode())
        {
          userConfig.setERGMode(true);
        }
        Serial.print("   Target Watts: ");
        Serial.print(userConfig.getSimulatedWatts()); //not displaying numbers less than 256 correctly but they do get sent to Zwift correctly.
        Serial.println("*********");
      }
    }
  }
};

void startBLEServer()
{
  //Bluetooth Server Setup
  Serial.println("Starting BLE work!");

  //Create BLE Server
  BLEDevice::init(BLEName.c_str());
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  //HEART RATE MONITOR SERVICE SETUP
  BLEService *pService = pServer->createService(HEARTSERVICE_UUID);
  pService->addCharacteristic(&heartRateMeasurementCharacteristic);
  heartRateMeasurementCharacteristic.addDescriptor(new BLE2902());
  heartRateMeasurementCharacteristic.setValue(heartRateMeasurement, 5);

  //Power Meter MONITOR SERVICE SETUP
  BLEService *pPowerMonitor = pServer->createService(CYCLINGPOWERSERVICE_UUID);
  pPowerMonitor->addCharacteristic(&cyclingPowerMeasurementCharacteristic);
  cyclingPowerMeasurementCharacteristic.addDescriptor(new BLE2902());
  cyclingPowerMeasurementCharacteristic.setValue(cyclingPowerMeasurement, 19);

  //Fitness Machine service setup
  BLEService *pFitnessMachineService = pServer->createService(FITNESSMACHINESERVICE_UUID);
  pFitnessMachineService->addCharacteristic(&fitnessMachineFeature);
  fitnessMachineFeature.addDescriptor(new BLE2902());
  fitnessMachineFeature.setValue(ftmsFeature, 32);

  pFitnessMachineService->addCharacteristic(&fitnessMachineControlPoint);
  fitnessMachineControlPoint.addDescriptor(new BLE2902());
  fitnessMachineControlPoint.setValue(ftmsControlPoint, 8);
  fitnessMachineControlPoint.setCallbacks(new MyCallbacks());

  pFitnessMachineService->addCharacteristic(&fitnessMachineStatus);
  fitnessMachineStatus.addDescriptor(new BLE2902());
  fitnessMachineStatus.setValue(ftmsMachineStatus, 8);

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
    heartRateMeasurementCharacteristic.setValue(heartRateMeasurement, 5);
    heartRateMeasurementCharacteristic.notify();
    //vTaskDelay(10/portTICK_RATE_MS);
    //Set New Watts.
    int remainder, quotient;
    quotient = userConfig.getSimulatedWatts() / 256;
    remainder = userConfig.getSimulatedWatts() % 256;
    cyclingPowerMeasurement[2] = remainder;
    cyclingPowerMeasurement[3] = quotient;
    cyclingPowerMeasurementCharacteristic.setValue(cyclingPowerMeasurement, 19);
    cyclingPowerMeasurementCharacteristic.notify();
    //vTaskDelay(10/portTICK_RATE_MS);
    fitnessMachineFeature.notify();
    //vTaskDelay(2000/portTICK_RATE_MS);
    //pfitnessMachineControlPoint->notify();
  }
}
