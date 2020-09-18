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
  for (int i = 0; i < length; i++)
  {
    Serial.printf("%x ,", pData[i]);
  }
  Serial.println("");
  if (pBLERemoteCharacteristic->getUUID().toString() == HEARTCHARACTERISTIC_UUID.toString())
  {
    userConfig.setSimulatedHr((int)pData[1]);
  }
  if (pBLERemoteCharacteristic->getUUID().toString() == CYCLINGPOWERMEASUREMENT_UUID.toString())
  {
    userConfig.setSimulatedWatts(bytes_to_u16(pData[3], pData[2]));
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
    pRemoteCharacteristic->subscribe(true, notifyCallback(), false);

  connected = true;
  return true;
}

 //Scan for BLE servers and find the first one that advertises the service we are looking for.
 
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
 
   // Called for each advertising BLE server.
  
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
  userConfig.setfoundDevices(output);

  if (doConnect)
  { //Works but inhibits the BLE Server Scan. Too late at night to fix. another day.
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
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
  else if (doScan)
  {
    Serial.println("Device disconnected. Scanning Again.");
    BLEDevice::getScan()->start(0); // this is just eample to start scan after disconnect, most likely there is better way to do it in arduino
  }

  // delay(1000); // Delay a second between loops.
} // End of loop
