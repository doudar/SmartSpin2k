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
#include "BLE_Common.h"

#include <memory>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>

int reconnectTries = MAX_RECONNECT_TRIES;
int scanRetries = MAX_SCAN_RETRIES;

TaskHandle_t BLEClientTask;

SpinBLEClient spinBLEClient;

void SpinBLEClient::start()
{
    //Create the task for the BLE Client loop
    xTaskCreatePinnedToCore(
        bleClientTask,   /* Task function. */
        "BLEClientTask", /* name of task. */
        4000,            /* Stack size of task */
        NULL,            /* parameter of the task */
        1,               /* priority of the task  */
        &BLEClientTask,  /* Task handle to keep track of created task */
        1);
}

// BLE Client loop task
void bleClientTask(void *pvParameters)
{
    for (;;)
    {
        if (spinBLEClient.doScan && (scanRetries > 0))
        {
            scanRetries--;
            spinBLEClient.scanProcess();
        }

        vTaskDelay(BLE_CLIENT_DELAY / portTICK_PERIOD_MS); // Delay a second between loops.
#ifdef DEBUG_STACK
        debugDirector("BLE_client High Water Mark: " + String(uxTaskGetStackHighWaterMark(BLEClientTask)));
#endif
        for (size_t x = 0; x < NUM_BLE_DEVICES; x++)
        {
            if (spinBLEClient.myBLEDevices[x].doConnect == true)
            {
                if (spinBLEClient.connectToServer())
                {
                    debugDirector("We are now connected to the BLE Server.");
                }
                else
                {
                }
            }
        }
    }
}

bool SpinBLEClient::connectToServer()
{
    debugDirector("Initiating Server Connection");
    NimBLEUUID serviceUUID;
    NimBLEUUID charUUID;

    int sucessful = 0;
    BLEAdvertisedDevice *myDevice = nullptr;
    int device_number = -1;
    for (size_t i = 0; i < NUM_BLE_DEVICES; i++)
    {
        if (spinBLEClient.myBLEDevices[i].doConnect == true) //Client wants to be connected
        {
            if (spinBLEClient.myBLEDevices[i].advertisedDevice) //Client is assigned
            {
                myDevice = spinBLEClient.myBLEDevices[i].advertisedDevice;
                device_number = i;
                break;
            }
            else
            {
                debugDirector("doConnect and client out of alignment. Resetting device slot");
                spinBLEClient.myBLEDevices[i].reset();
                spinBLEClient.doScan = true;
                return false;
            }
        }
    }
    if (myDevice == nullptr)
    {
        debugDirector("No Device Found to Connect");
        return false;
    }
    //FUTURE - Iterate through an array of UUID's we support instead of all the if checks.
    if (myDevice->haveServiceUUID())
    {
        if (myDevice->isAdvertisingService(FLYWHEEL_UART_SERVICE_UUID))
        {
            serviceUUID = FLYWHEEL_UART_SERVICE_UUID;
            charUUID = FLYWHEEL_UART_TX_UUID;
            debugDirector("trying to connect to Flywheel Bike");
        }
        else if (myDevice->isAdvertisingService(CYCLINGPOWERSERVICE_UUID))
        {
            serviceUUID = CYCLINGPOWERSERVICE_UUID;
            charUUID = CYCLINGPOWERMEASUREMENT_UUID;
            debugDirector("trying to connect to PM");
        }
        else if (myDevice->isAdvertisingService(FITNESSMACHINESERVICE_UUID))
        {
            serviceUUID = FITNESSMACHINESERVICE_UUID;
            charUUID = FITNESSMACHINEINDOORBIKEDATA_UUID;
            debugDirector("trying to connect to Fitness machine service");
        }
        else if (myDevice->isAdvertisingService(HEARTSERVICE_UUID))
        {
            serviceUUID = HEARTSERVICE_UUID;
            charUUID = HEARTCHARACTERISTIC_UUID;
            debugDirector("Trying to connect to HRM");
        }
        else
        {
            debugDirector("Error: No advertised UUID found");
            spinBLEClient.myBLEDevices[device_number].reset();
            return false;     
        }
    }
    else
    {
        debugDirector("Error: Device has no Service UUID");
        spinBLEClient.myBLEDevices[device_number].reset();
        spinBLEClient.doScan = true;
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
                    spinBLEClient.myBLEDevices[device_number].reset();
                    spinBLEClient.myBLEDevices[device_number].doConnect = false;
                    connectedPM = false;
                    doScan = true;
                }
                return false;
            }
            Serial.println("Reconnecting client");
            BLERemoteService *pRemoteService = pClient->getService(serviceUUID);

            if (pRemoteService == nullptr)
            {
                debugDirector("Couldn't find Service");
                reconnectTries--;
                return false;
            }

            pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);

            if (pRemoteCharacteristic == nullptr)
            {
                debugDirector("Couldn't find Characteristic");
                reconnectTries--;
                return false;
            }

            if (pRemoteCharacteristic->canNotify())
            {

                pRemoteCharacteristic->subscribe(true, nullptr, true);
                debugDirector("Found " + String(pRemoteCharacteristic->getUUID().toString().c_str()) + " on reconnect.");
                reconnectTries = MAX_RECONNECT_TRIES;
                spinBLEClient.myBLEDevices[device_number].set(myDevice, pClient->getConnId(), serviceUUID, charUUID);
                spinBLEClient.myBLEDevices[device_number].doConnect = false;

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
            debugDirector("No Previous client found");
            //pClient = NimBLEDevice::getDisconnectedClient();
        }
    }
    String t_name = "";
    if (myDevice->haveName())
    {
        String t_name = myDevice->getName().c_str();
    }
    debugDirector("Forming a connection to: " + t_name + " " + String(myDevice->getAddress().toString().c_str()));
    pClient = NimBLEDevice::createClient();
    debugDirector(" - Created client", false);
    pClient->setClientCallbacks(new MyClientCallback(), true);
    // Connect to the remove BLE Server.
    pClient->setConnectionParams(80, 80, 0, 200);
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
        { //need to iterate through these for all UUID's
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
            pRemoteCharacteristic->subscribe(true, nullptr, true);
            reconnectTries = MAX_RECONNECT_TRIES;
            scanRetries = MAX_SCAN_RETRIES;
        }
        else
        {
            debugDirector("Unable to subscribe to notifications");
        }
    }
    if (sucessful > 0)
    {
        debugDirector("Sucessful " + String(pRemoteCharacteristic->getUUID().toString().c_str()) + " subscription.");
        spinBLEClient.myBLEDevices[device_number].doConnect = false;
        reconnectTries = MAX_RECONNECT_TRIES;
        spinBLEClient.myBLEDevices[device_number].set(myDevice, pClient->getConnId(), serviceUUID, charUUID);
        vTaskDelay(100 / portTICK_PERIOD_MS); //Give time for connection to finalize.
        removeDuplicates(pClient);
        return true;
    }
    reconnectTries--;
    debugDirector("disconnecting Client");
    if (pClient->isConnected())
    {
        pClient->disconnect();
    }
    if (reconnectTries < 1)
    {
        spinBLEClient.myBLEDevices[device_number].reset(); //Give up on this device
    }
    return false;
}

/**  None of these are required as they will be handled by the library with defaults. **
 **                       Remove as you see fit for your needs                        */

void SpinBLEClient::MyClientCallback::onConnect(BLEClient *pclient)
{

    auto addr = pclient->getPeerAddress();
    for (size_t i = 0; i < NUM_BLE_DEVICES; i++)
    {
        if (addr == spinBLEClient.myBLEDevices[i].peerAddress)
        {
            spinBLEClient.myBLEDevices[i].doConnect = true;
            if ((spinBLEClient.myBLEDevices[i].charUUID == CYCLINGPOWERMEASUREMENT_UUID) || (spinBLEClient.myBLEDevices[i].charUUID == FITNESSMACHINEINDOORBIKEDATA_UUID) || (spinBLEClient.myBLEDevices[i].charUUID == FLYWHEEL_UART_RX_UUID))
            {
                spinBLEClient.connectedPM = true;
                debugDirector("Registered PM on Connect");
                //spinBLEClient.removeDuplicates(pclient);
                return;
            }
            if ((spinBLEClient.myBLEDevices[i].charUUID == HEARTCHARACTERISTIC_UUID))
            {
                spinBLEClient.connectedHR = true;
                debugDirector("Registered HRM on Connect");
                return;
            }
        }
    }
}

void SpinBLEClient::MyClientCallback::onDisconnect(BLEClient *pclient)
{

    if (spinBLEClient.intentionalDisconnect)
    {
        debugDirector("Intentional Disconnect");
        spinBLEClient.intentionalDisconnect = false;
        return;
    }
    if (!pclient->isConnected())
    {
        auto addr = pclient->getPeerAddress();
        //auto addr = BLEDevice::getDisconnectedClient()->getPeerAddress();
        debugDirector("This disconnected client Address " + String(addr.toString().c_str()));
        for (size_t i = 0; i < NUM_BLE_DEVICES; i++)
        {

            if (addr == spinBLEClient.myBLEDevices[i].peerAddress)
            {
                //spinBLEClient.myBLEDevices[i].connectedClientID = BLE_HS_CONN_HANDLE_NONE;
                debugDirector("Detected " + String(spinBLEClient.myBLEDevices[i].serviceUUID.toString().c_str()) + " Disconnect");
                spinBLEClient.myBLEDevices[i].doConnect = true;
                if ((spinBLEClient.myBLEDevices[i].charUUID == CYCLINGPOWERMEASUREMENT_UUID) || (spinBLEClient.myBLEDevices[i].charUUID == FITNESSMACHINEINDOORBIKEDATA_UUID) || (spinBLEClient.myBLEDevices[i].charUUID == FLYWHEEL_UART_RX_UUID))
                {
                    spinBLEClient.connectedPM = false;
                    break;
                }
                if ((spinBLEClient.myBLEDevices[i].charUUID == HEARTCHARACTERISTIC_UUID))
                {
                    spinBLEClient.connectedHR = false;
                    break;
                }
            }
        }
        return;
    }
}

/***************** New - Security handled here ********************
****** Note: these are the same return values as defaults ********/
uint32_t SpinBLEClient::MyClientCallback::onPassKeyRequest()
{
    debugDirector("Client PassKeyRequest");
    return 123456;
}
bool SpinBLEClient::MyClientCallback::onConfirmPIN(uint32_t pass_key)
{
    debugDirector("The passkey YES/NO number: " + String(pass_key));
    return true;
}

void SpinBLEClient::MyClientCallback::onAuthenticationComplete(ble_gap_conn_desc desc)
{
    debugDirector("Starting BLE work!");
}
/*******************************************************************/

/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */

void SpinBLEClient::MyAdvertisedDeviceCallback::onResult(BLEAdvertisedDevice *advertisedDevice)
{
    debugDirector("BLE Advertised Device found: " + String(advertisedDevice->toString().c_str()));
    String aDevName;
    if (advertisedDevice->haveName())
    {
        aDevName = String(advertisedDevice->getName().c_str());
    }
    else
    {
        aDevName = "";
    }
    if ((advertisedDevice->haveServiceUUID()) && (advertisedDevice->isAdvertisingService(CYCLINGPOWERSERVICE_UUID) || advertisedDevice->isAdvertisingService(FLYWHEEL_UART_SERVICE_UUID) || advertisedDevice->isAdvertisingService(FITNESSMACHINESERVICE_UUID) || advertisedDevice->isAdvertisingService(HEARTSERVICE_UUID)))
    {
        //if ((aDevName == c_PM) || (advertisedDevice->getAddress().toString().c_str() == c_PM) || (aDevName == c_HR) || (advertisedDevice->getAddress().toString().c_str() == c_HR) || (String(c_PM) == ("any")) || (String(c_HR) == ("any")))
        //{ //notice the subtle difference vv getServiceUUID(int) returns the index of the service in the list or the 0 slot if not specified.
        debugDirector("Matching Device Name: " + aDevName);
        if (advertisedDevice->getServiceUUID() == HEARTSERVICE_UUID)
        {
            if (String(userConfig.getconnectedHeartMonitor()) == "any")
            {
                debugDirector("HR String Matched Any");
                //continue
            }
            else if (aDevName != String(userConfig.getconnectedHeartMonitor()) || (String(userConfig.getconnectedHeartMonitor()) == "none"))
            {
                debugDirector("Skipping non-selected HRM |" + aDevName + "|" + String(userConfig.getconnectedHeartMonitor()));
                return;
            }else if (aDevName == String(userConfig.getconnectedHeartMonitor()))
            {
                debugDirector("HR String Matched " + aDevName);
            }
        }
        else // Already tested -->((advertisedDevice->getServiceUUID()(CYCLINGPOWERSERVICE_UUID) || advertisedDevice->getServiceUUID()(FLYWHEEL_UART_SERVICE_UUID) || advertisedDevice->getServiceUUID()(FITNESSMACHINESERVICE_UUID)))
        {
            if (String(userConfig.getconnectedPowerMeter()) == "any")
            {
                debugDirector("PM String Matched Any");
                //continue
            }
            else if (aDevName != String(userConfig.getconnectedPowerMeter()) || (String(userConfig.getconnectedPowerMeter()) == "none"))
            {
                debugDirector("Skipping non-selected PM |" + aDevName + "|" + String(userConfig.getconnectedPowerMeter()));
                return;
            }else if (aDevName == String(userConfig.getconnectedPowerMeter()))
            {
                debugDirector("PM String Matched " + aDevName);
            }
        }
        for (size_t i = 0; i < NUM_BLE_DEVICES; i++)
        {
            if ((spinBLEClient.myBLEDevices[i].advertisedDevice == nullptr) || (advertisedDevice->getAddress() == spinBLEClient.myBLEDevices[i].peerAddress)) //found empty device slot
            {
                spinBLEClient.myBLEDevices[i].set(advertisedDevice);
                spinBLEClient.myBLEDevices[i].doConnect = true;
                debugDirector("doConnect set on device: " + String(i));
                spinBLEClient.doScan = false;
                return;
            }
            debugDirector("Checking Slot " + String(i));
        }
        return;
        //}
    }
}

void SpinBLEClient::scanProcess()
{
    //doConnect = connectRequest;
    debugDirector("Scanning for BLE servers and putting them into a list...");

    // Retrieve a Scanner and set the callback we want to use to be informed when we
    // have detected a new device.  Specify that we want active scanning and start the
    // scan to run for 5 seconds.
    BLEScan *pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallback());
    pBLEScan->setInterval(550);
    pBLEScan->setWindow(500);
    pBLEScan->setActiveScan(true);
    BLEScanResults foundDevices = pBLEScan->start(10, false);
    // Load the scan into a Json String
    int count = foundDevices.getCount();

    StaticJsonDocument<1000> devices;

    String device = "";

    for (int i = 0; i < count; i++)
    {
        BLEAdvertisedDevice d = foundDevices.getDevice(i);
        if (d.isAdvertisingService(CYCLINGPOWERSERVICE_UUID) || d.isAdvertisingService(HEARTSERVICE_UUID) || d.isAdvertisingService(FLYWHEEL_UART_SERVICE_UUID) || d.isAdvertisingService(FITNESSMACHINESERVICE_UUID))
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
    debugDirector("Bluetooth Client Found Devices: " + output, true, true);
    userConfig.setFoundDevices(output);
}

void SpinBLEClient::serverScan(bool connectRequest)
{
    if (connectRequest)
    {
        scanRetries = MAX_SCAN_RETRIES;
    }
    spinBLEClient.doScan = true;
}

void SpinBLEClient::disconnect()
{
    scanRetries = 0;
    reconnectTries = 0;
    intentionalDisconnect = true;
    debugDirector("Shutting Down all BLE services");
    if (NimBLEDevice::getInitialized())
    {
        NimBLEDevice::deinit();
        vTaskDelay(100 / portTICK_RATE_MS);
    }
}

void SpinBLEClient::removeDuplicates(BLEClient *pClient)
{
    //BLEAddress thisAddress = pClient->getPeerAddress();
    myAdvertisedBLEDevice tBLEd;
    myAdvertisedBLEDevice oldBLEd;
    for (size_t i = 0; i < NUM_BLE_DEVICES; i++) //Disconnect oldest PM to avoid two connected.
    {
        tBLEd = spinBLEClient.myBLEDevices[i];
        if (tBLEd.peerAddress == pClient->getPeerAddress())
        {
            break;
        }
    }

    for (size_t i = 0; i < NUM_BLE_DEVICES; i++) //Disconnect oldest PM to avoid two connected.
    {
        oldBLEd = spinBLEClient.myBLEDevices[i];
        if (oldBLEd.advertisedDevice)
        {
            if ((tBLEd.serviceUUID == oldBLEd.serviceUUID) && (tBLEd.peerAddress != oldBLEd.peerAddress))
            {
                if (BLEDevice::getClientByPeerAddress(oldBLEd.peerAddress))
                {
                    if (BLEDevice::getClientByPeerAddress(oldBLEd.peerAddress)->isConnected())
                    {
                        debugDirector(String(tBLEd.peerAddress.toString().c_str()) + " Matched another service.  Disconnecting: " + String(oldBLEd.peerAddress.toString().c_str()));
                        BLEDevice::getClientByPeerAddress(oldBLEd.peerAddress)->disconnect();
                        oldBLEd.reset();
                        spinBLEClient.intentionalDisconnect = true;
                        return;
                    }
                }
            }
        }
    }
}