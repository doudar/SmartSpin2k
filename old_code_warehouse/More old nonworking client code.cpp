//Testing server notify with heart rate data

void scanEndedCB(NimBLEScanResults results);



BLEScan *pBLEScan;
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  /*** Only a reference to the advertised device is passed now
    void onResult(BLEAdvertisedDevice advertisedDevice) { **/
  void onResult(BLEAdvertisedDevice *advertisedDevice)
  {
    /** Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str()); **/
    Serial.printf("Advertised Device: %s \n", advertisedDevice->toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (doConnect)
    {
      if ((advertisedDevice->haveName() && advertisedDevice->getName() == userConfig.getconnectedPowerMeter()) || advertisedDevice->getAddress().toString() == userConfig.getconnectedPowerMeter())
      {

        BLEDevice::getScan()->stop();
        //myDevice = new NimBLEAdvertisedDevice(*advertisedDevice);
        myDevice = advertisedDevice;
        //vTaskDelay(5000/portTICK_PERIOD_MS);
        connectToServer();
        //doConnect = false;
      }
    }
  }
};

class ClientCallbacks : public NimBLEClientCallbacks
{
  void onConnect(NimBLEClient *pClient)
  {
    Serial.println("Connected");
    /** After connection we should change the parameters if we don't need fast response times.
         *  These settings are 150ms interval, 0 latency, 450ms timout. 
         *  Timeout should be a multiple of the interval, minimum is 100ms.
         *  I find a multiple of 3-5 * the interval works best for quick response/reconnect.
         *  Min interval: 120 * 1.25ms = 150, Max interval: 120 * 1.25ms = 150, 0 latency, 60 * 10ms = 600ms timeout 
         */
    pClient->updateConnParams(120, 120, 0, 60);
  };

  void onDisconnect(NimBLEClient *pClient)
  {
    Serial.print(pClient->getPeerAddress().toString().c_str());
    Serial.println(" Disconnected - Starting scan");
    NimBLEDevice::getScan()->start(scanTime, scanEndedCB);
  };

  /** Called when the peripheral requests a change to the connection parameters.
     *  Return true to accept and apply them or false to reject and keep 
     *  the currently used parameters. Default will return true.
     */
  bool onConnParamsUpdateRequest(NimBLEClient *pClient, const ble_gap_upd_params *params)
  {
    Serial.println("onConnParamsUpdateRequest");
    if (params->itvl_min < 24)
    { /** 1.25ms units */
      return false;
    }
    else if (params->itvl_max > 40)
    { /** 1.25ms units */
      return false;
    }
    else if (params->latency > 2)
    { /** Number of intervals allowed to skip */
      return false;
    }
    else if (params->supervision_timeout > 100)
    { /** 10ms units */
      return false;
    }

    return true;
  };

  /********************* Security handled here **********************
    ****** Note: these are the same return values as defaults ********/
  uint32_t onPassKeyRequest()
  {
    Serial.println("Client Passkey Request");
    /** return the passkey to send to the server */
    return 123456;
  };

  bool onConfirmPIN(uint32_t pass_key)
  {
    Serial.print("The passkey YES/NO number: ");
    Serial.println(pass_key);
    /** Return false if passkeys don't match. */
    return true;
  };

  /** Pairing process complete, we can check the results in ble_gap_conn_desc */
  void onAuthenticationComplete(ble_gap_conn_desc *desc)
  {
    if (!desc->sec_state.encrypted)
    {
      Serial.println("Encrypt connection failed - disconnecting");
      /** Find the client with the connection handle provided in desc */
      NimBLEDevice::getClientByID(desc->conn_handle)->disconnect();
      return;
    }
  };
};

void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
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

/** Callback to process the results of the last scan or restart it */
void scanEndedCB(NimBLEScanResults results)
{
  Serial.println("Scan Ended");
}

/** Create a single global instance of the callback class to be used by all clients */
static ClientCallbacks clientCB;


void BLEServerScan(bool connectRequest)
{
  doConnect = connectRequest;
  Serial.println("Scanning...");

  //if(!BLEDevice::getInitialized){
  BLEDevice::init(BLEName.c_str());
  Serial.println("Initializing BLE Device in Client");
  //}

  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99); // less or equal setInterval value

  BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
  Serial.print("Devices found: ");
  Serial.println(foundDevices.getCount());
  Serial.println("Scan done!");
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
  Serial.println("Devices matching parameters:");
  Serial.println(output);
  userConfig.setFoundDevices(output);
  pBLEScan->clearResults(); // delete results fromBLEScan buffer to release memory
}

/*bool connectToServer()
  {
    BLEUUID serviceUUID;
    BLEUUID charUUID;
    Serial.print("Forming a connection to ");
    Serial.println(myDevice->getAddress().toString().c_str());

    BLEClient *pClient = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());
Serial.println(" - Set Client Callbacks");
    // Connect to the remove BLE Server.
    pClient->connect(myDevice->getAddress()); // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(" - Connected to server");

    // Obtain a reference to the service we are after in the remote BLE server.

    if (myDevice->getServiceUUID() == HEARTSERVICE_UUID)
    {
      serviceUUID = HEARTSERVICE_UUID;
      charUUID = HEARTCHARACTERISTIC_UUID;
    }
    if (myDevice->getServiceUUID() == CYCLINGPOWERSERVICE_UUID)
    {
      serviceUUID = CYCLINGPOWERSERVICE_UUID;
      charUUID = CYCLINGPOWERMEASUREMENT_UUID;
    }

    BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr)
    {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our service");

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pBLERemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pBLERemoteCharacteristic == nullptr)
    {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our characteristic");

    // Read the value of the characteristic.
    if (pBLERemoteCharacteristic->canRead())
    {
      std::string value = pBLERemoteCharacteristic->readValue();
      Serial.print("The characteristic value was: ");
      Serial.println(value.c_str());
      connected = true;
    }

    if (pBLERemoteCharacteristic->canNotify())
    {
      //pBLERemoteCharacteristic->registerForNotify(notifyCallback);
      pBLERemoteCharacteristic->subscribe(true, notifyCallback);

      connected = true;
      return true;
    }
    
    return false;
  
  }*/
/** Handles the provisioning of clients and connects / interfaces with the server */

bool connectToServer()
{
  NimBLEClient *pClient = nullptr;

  /** Check if we have a client we should reuse first **/
  if (NimBLEDevice::getClientListSize())
  {

    /** Special case when we already know this device, we send false as the 
         *  second argument in connect() to prevent refreshing the service database.
         *  This saves considerable time and power.
         */
    pClient = NimBLEDevice::getClientByPeerAddress(myDevice->getAddress());
    if (pClient)
    {
      Serial.println("Found previously paired device");
      if (!pClient->connect(myDevice, false))
      {
        Serial.println("Reconnect failed");
        return false;
      }
      Serial.println("Reconnected client");
    }
    /** We don't already have a client that knows this device,
         *  we will check for a client that is disconnected that we can use.
         */
    else
    {
      pClient = NimBLEDevice::getDisconnectedClient();
      Serial.println("pClient = NimBLEDevice::getDisconnectedClient();");
    }
  }

  /** No client to reuse? Create a new one. */
  if (!pClient)
  {
    if (NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS)
    {
      Serial.println("Max clients reached - no more connections available");
      return false;
    }

    pClient = NimBLEDevice::createClient();

    Serial.println("New client created");

    pClient->setClientCallbacks(&clientCB, false);
    /** Set initial connection parameters: These settings are 15ms interval, 0 latency, 120ms timout. 
         *  These settings are safe for 3 clients to connect reliably, can go faster if you have less 
         *  connections. Timeout should be a multiple of the interval, minimum is 100ms.
         *  Min interval: 12 * 1.25ms = 15, Max interval: 12 * 1.25ms = 15, 0 latency, 51 * 10ms = 510ms timeout 
         */
    Serial.println(" - Callbacks Set");
    //pClient->setConnectionParams(12, 12, 0, 51);
    Serial.println(" - Connection Parameters set");
    /** Set how long we are willing to wait for the connection to complete (seconds), default is 30. */
    //pClient->setConnectTimeout(5);
    Serial.println(" - Connection Timeout Set");

    pClient->connect(myDevice->getAddress());
    Serial.println("- made it past connection...");

    if (!pClient->connect(myDevice->getAddress()))
    {
      /** Created a client but failed to connect, don't need to keep it as it has no data */
      NimBLEDevice::deleteClient(pClient);
      Serial.println("Failed to connect, deleted client");
      return false;
    }
    Serial.println("BLE Client should be created and the data recieved was: ");
    Serial.println(pClient->getValue(HEARTSERVICE_UUID, HEARTCHARACTERISTIC_UUID).c_str());
  }

  if (!pClient->isConnected())
  {
    Serial.println("if(!pClient->isConnected())");
    if (!pClient->connect(myDevice))
    {
      Serial.println("Failed to connect");
      return false;
    }
  }

  Serial.println("Connected to: ");
  Serial.println(pClient->getPeerAddress().toString().c_str());
  Serial.println("RSSI: ");
  Serial.println(pClient->getRssi());

  /** Now we can read/write/subscribe the charateristics of the services we are interested in */

  NimBLERemoteService *pSvc = nullptr;
  NimBLERemoteCharacteristic *pChr = nullptr;
  //NimBLERemoteDescriptor* pDsc = nullptr;

  pSvc = pClient->getService(HEARTSERVICE_UUID);
  if (pSvc)
  { /** make sure it's not null */
    pChr = pSvc->getCharacteristic(HEARTCHARACTERISTIC_UUID);
    Serial.println("Retrieved Remote Heart Rate Characteristic UUID");
  }

  if (pChr)
  { /** make sure it's not null */
    if (pChr->canRead())
    {
      Serial.print(pChr->getUUID().toString().c_str());
      Serial.print(" Value: ");
      Serial.println(pChr->readValue().c_str());
    }

    /** registerForNotify() has been deprecated and replaced with subscribe() / unsubscribe().
         *  Subscribe parameter defaults are: notifications=true, notifyCallback=nullptr, response=false.
         *  Unsubscribe parameter defaults are: response=false. 
         */
    if (pChr->canNotify())
    {
      //if(!pChr->registerForNotify(notifyCB)) {
      if (!pChr->subscribe(true, notifyCallback))
      {
        /** Disconnect if subscribe failed */
        Serial.println("Heart Rate subscribe to notify failed");
        pClient->disconnect();
        return false;
      }
    }
  }

  else
  {
    Serial.println("Heart Rate Service Not Found");
  }

  pSvc = pClient->getService(CYCLINGPOWERSERVICE_UUID);
  if (pSvc)
  { /** make sure it's not null */
    pChr = pSvc->getCharacteristic(CYCLINGPOWERMEASUREMENT_UUID);
  }

  if (pChr)
  { /** make sure it's not null */
    if (pChr->canRead())
    {
      Serial.print(pChr->getUUID().toString().c_str());
      Serial.print(" Value: ");
      Serial.println(pChr->readValue().c_str());
    }

    /** registerForNotify() has been deprecated and replaced with subscribe() / unsubscribe().
         *  Subscribe parameter defaults are: notifications=true, notifyCallback=nullptr, response=false.
         *  Unsubscribe parameter defaults are: response=false. 
         */
    if (pChr->canNotify())
    {
      //if(!pChr->registerForNotify(notifyCB)) {
      if (!pChr->subscribe(true, notifyCallback))
      {
        /** Disconnect if subscribe failed */
        pClient->disconnect();
        return false;
      }
    }
  }

  else
  {
    Serial.println("Cycling Power Service Not Found");
  }

  Serial.println("Done with this device!");
  return true;
}

void bleClient()
{
  if (connected)
  {
    //String newValue = "Time since boot: " + String(millis() / 1000);
    //std::string serverValue;
    // Serial.println("Setting new characteristic value to \"" + newValue + "\"");
    //
    // Set the characteristic's value to be the array of bytes that is actually a string.
    Serial.println("Data Recieved from remote BLE Server:");
    Serial.println(pBLERemoteCharacteristic->readValue().c_str());
    //serverValue = pRemoteCharacteristic->readValue();

    //Serial.printf("Value from BLE Server Was: ");
    // for(const auto& text : serverValue) {   // Range-for!
    //     Serial.printf(" %d", text);
    //   }
    //    Serial.printf("\n");
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
  else
  {
    Serial.println("Device disconnected. Scanning Again.");
    //  BLEServerScan(true); // this is just eample to start scan after disconnect, most likely there is better way to do it in arduino
  }

  // delay(1000); // Delay a second between loops.
} // End of loop
