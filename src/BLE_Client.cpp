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

#include <ArduinoJson.h>
#include <NimBLEDevice.h>

int reconnectTries = MAX_RECONNECT_TRIES;
int scanRetries = MAX_SCAN_RETRIES;

bool intentionalDisconnect = false;

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
        if ((spinBLEClient.doConnectPM == true) || (spinBLEClient.doConnectHR == true))
        {
            if (spinBLEClient.connectToServer())
            {
                debugDirector("We are now connected to the BLE Server.");
            }
            else
            {
            }
        }

        if (spinBLEClient.doScan && (scanRetries > 0))
        {
            scanRetries--;
            spinBLEClient.scanProcess();
        }

        vTaskDelay(BLE_CLIENT_DELAY / portTICK_PERIOD_MS); // Delay a second between loops.
        //debugDirector("BLEclient High Water Mark: " + String(uxTaskGetStackHighWaterMark(BLEClientTask)));
    }
}

static void notifyCallback(
    BLERemoteCharacteristic *pBLERemoteCharacteristic,
    uint8_t *pData,
    size_t length,
    bool isNotify)
{
    //Write the recieved data to the Debug Director
    String debugOutput = "";
    for (int i = 0; i < length; i++)
    {
        debugOutput += String(pData[i], HEX) + " ";
    }
    debugDirector(debugOutput + "<-- " + String(pBLERemoteCharacteristic->getUUID().toString().c_str()), false, true);

    //Set HR Data
    if (pBLERemoteCharacteristic->getUUID() == HEARTCHARACTERISTIC_UUID)
    {
        userConfig.setSimulatedHr((int)pData[1]);
        debugDirector(" HRM: " + String(userConfig.getSimulatedHr()), false);
        debugDirector("");
    }

    //Set data from Flywheel Bike
    if (pBLERemoteCharacteristic->getUUID() == FLYWHEEL_UART_SERVICE_UUID)
    {
        debugDirector("Flywheel Data: ", false);
        if (pData[0] == 0xFF)
        {
            userConfig.setSimulatedWatts(bytes_to_u16(pData[4], pData[3]));
            userConfig.setSimulatedCad(pData[12]);
            debugDirector(" Flywheel Data: " + String(userConfig.getSimulatedWatts()) + "W " + String(userConfig.getSimulatedCad()) + " CAD");

            //May need to update the connection params here as below but I'll see if Flywheel likes our current settings first
            // static bool firstloop = true;
            // if(firstloop){
            // pBLERemoteCharacteristic->getRemoteService()->getClient()->updateConnParams(16,60,0,400);
            // firstloop = false;
            // }
        }
    }

    //Set data from Fitness Machine Service
    if (pBLERemoteCharacteristic->getUUID() == FITNESSMACHINEINDOORBIKEDATA_UUID)
    {
        byte flags = bytes_to_u16(pData[1], pData[0]); //two bytes in this bit field
        int dPos = 4;                                  //lowest position any data could be
                                        //IC4 Bit field is probably 1001000100
                                                        //98765432109876543210 - bit placement helper :)
        if (bitRead(flags, 0)) //Instantaneous Speed field
        {
            dPos += 2;
        }
        if (bitRead(flags, 1)) //Average Speed present
        {
            dPos += 2;
        }
        if (bitRead(flags, 2)) //Instantaneous Cadence
        {                      //IC4 Offset was 4
            userConfig.setSimulatedCad((bytes_to_u16(pData[dPos + 1], pData[dPos])) / 2);
            dPos += 2;      
        }
        if (bitRead(flags, 3)) //Average Cadence present
        {
            dPos += 2;
        }
        if (bitRead(flags, 4)) //Total Distance Present
        {
            dPos += 3;
        }
        if (bitRead(flags, 5)) //Resistance Level Present
        {
            dPos += 2;
        }
        if (bitRead(flags, 6)) //Instantaneous Power Present
        {                      //IC4 offsett was 6
            userConfig.setSimulatedWatts(bytes_to_u16(pData[dPos + 1], pData[dPos]));
            dPos += 2;
        }
        if (bitRead(flags, 7)) //Average Power Present
        {
            dPos += 2;
        }
        if (bitRead(flags, 8)) //Expended Energy Present
        {
            dPos += 5;
        }
        if (bitRead(flags, 9)) //Heart Rate Present
        {
            if (pData[8] > 0) //Throwing out zero's
            {
                userConfig.setSimulatedHr(pData[8]);
            }
            dPos += 1;
        }

        if (bitRead(flags, 10)) //Metabolic Equivalent Present
        {
            dPos += 1;
        }
        if (bitRead(flags, 11)) //Elapsed Time Present
        {
            dPos += 2;
        }
        if (bitRead(flags, 12)) //Remaining Time Present
        {
            dPos += 2;
        }
        //debugDirector(" Indoor Bike Data: " + String(userConfig.getSimulatedWatts()) + "W " + String(userConfig.getSimulatedCad()) + " CAD");
    }

    //Calculate Cadence and power from Cycling Power Measurement
    if (pBLERemoteCharacteristic->getUUID() == CYCLINGPOWERMEASUREMENT_UUID)
    {
        if (pBLERemoteCharacteristic->getRemoteService()->getClient()->getConnId() == spinBLEClient.lastConnectedPMID) //disregarding other pm's that may still be connected
        {
            //first calculate which fields are present. Power is always 2 & 3, cadence can move depending on the flags.
            byte flags = pData[0];
            int cPos = 4; //lowest position cadence could ever be
            if (bitRead(flags, 0))
            {
                //pedal balance field present
                cPos++;
            }
            if (bitRead(flags, 1))
            {
                //pedal power balance reference
                //no field associated with this.
            }
            if (bitRead(flags, 2))
            {
                //accumulated torque field present
                cPos += 2;
            }
            if (bitRead(flags, 3))
            {
                //accumulated torque field source
                //no field associated with this.
            }
            if (bitRead(flags, 4))
            {
                //Wheel Revolution field PAIR Data present. 32-bits for wheel revs, 16 bits for wheel event time.
                //Why is that so hard to find in the specs?
                cPos += 6;
            }
            if (bitRead(flags, 5))
            {
                //Crank Revolution data present, lets process it.
                spinBLEClient.crankRev[1] = spinBLEClient.crankRev[0];
                spinBLEClient.crankRev[0] = bytes_to_int(pData[cPos + 1], pData[cPos]);
                spinBLEClient.crankEventTime[1] = spinBLEClient.crankEventTime[0];
                spinBLEClient.crankEventTime[0] = bytes_to_int(pData[cPos + 3], pData[cPos + 2]);
                if ((spinBLEClient.crankRev[0] > spinBLEClient.crankRev[1]) && (spinBLEClient.crankEventTime[0] - spinBLEClient.crankEventTime[1] != 0))
                {
                    int tCAD = (((abs(spinBLEClient.crankRev[0] - spinBLEClient.crankRev[1]) * 1024) / abs(spinBLEClient.crankEventTime[0] - spinBLEClient.crankEventTime[1])) * 60);
                    if (tCAD > 1)
                    {
                        if (tCAD > 200) //Cadence Error
                        {
                            tCAD = 0;
                        }
                        userConfig.setSimulatedCad(tCAD);
                        spinBLEClient.noReadingIn = 0;
                    }
                    else
                    {
                        spinBLEClient.noReadingIn++;
                    }
                }
                else //the crank rev probably didn't update
                {
                    if (spinBLEClient.noReadingIn > 2) //Require three consecutive readings before setting 0 cadence
                    {
                        userConfig.setSimulatedCad(0);
                    }
                    spinBLEClient.noReadingIn++;
                }

                debugDirector(" CAD: " + String(userConfig.getSimulatedCad()), false);
                debugDirector("");
            }

            //Watts are so much easier......
            userConfig.setSimulatedWatts(bytes_to_u16(pData[3], pData[2]));
            if (userConfig.getDoublePower())
            {
                userConfig.setSimulatedWatts(userConfig.getSimulatedWatts() * 2);
            }
        }

        else
        {
            debugDirector("Disconnecting secondary PM");
            intentionalDisconnect = true;
            pBLERemoteCharacteristic->getRemoteService()->getClient()->disconnect();
            //NimBLEDevice::deleteClient(pBLERemoteCharacteristic->getRemoteService()->getClient()); //this was an old client, disconnect it.
        }
    }
}

bool SpinBLEClient::connectToServer()
{
    debugDirector("Initiating Server Connection");
    NimBLEUUID serviceUUID;
    NimBLEUUID charUUID;

    int sucessful = 0;
    BLEAdvertisedDevice *myDevice;
    if (doConnectPM)
    {
        myDevice = myPowerMeter;
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
                        doScan = true;
                    }
                    if (myDevice == myHeartMonitor)
                    {
                        myHeartMonitor = nullptr;
                        doConnectHR = false;
                        connectedHR = false;
                        doScan = true;
                    }
                }
                return false;
            }
            Serial.println("Reconnecting client");
            // pClient->setClientCallbacks(new MyClientCallback(), true); commented out per @h2zero suggestion
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

                pRemoteCharacteristic->subscribe(true, notifyCallback);
                if (myDevice == myPowerMeter)
                {
                    debugDirector("Found PM on reconnect");
                    connectedPM = true;
                    doConnectPM = false;
                    reconnectTries = MAX_RECONNECT_TRIES;
                    lastConnectedPMID = pClient->getConnId();
                }

                if (myDevice == myHeartMonitor)
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
            debugDirector("No Previous client found");
            pClient = NimBLEDevice::getDisconnectedClient();
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

        if (myDevice == myPowerMeter)
        {
            connectedPM = true;
            doConnectPM = false;
            debugDirector("Sucessful PM");
            lastConnectedPMID = pClient->getConnId();
        }

        if (myDevice == myHeartMonitor)
        {
            connectedHR = true;
            doConnectHR = false;
            debugDirector("Sucessful HRM");
        }
        reconnectTries = MAX_RECONNECT_TRIES;
        return true;
    }
    debugDirector("disconnecting Client");
    pClient->disconnect();
    return false;
}

/**  None of these are required as they will be handled by the library with defaults. **
 **                       Remove as you see fit for your needs                        */

void SpinBLEClient::MyClientCallback::onConnect(BLEClient *pclient)
{
}
void SpinBLEClient::MyClientCallback::onDisconnect(BLEClient *pclient)
{
    if (intentionalDisconnect)
    {
        intentionalDisconnect = false;
        return;
    }
    if ((pclient->getService(HEARTSERVICE_UUID)) && (!(pclient->isConnected())))
    {
        debugDirector("Detected HR Disconnect. Trying rapid reconnect");
        spinBLEClient.doConnectHR = true; //try rapid reconnect
        return;
    }
    if ((pclient->getService(CYCLINGPOWERSERVICE_UUID) || pclient->getService(FLYWHEEL_UART_SERVICE_UUID) || pclient->getService(FITNESSMACHINESERVICE_UUID)) && (!(pclient->isConnected())))
    {

        debugDirector("Detected PM Disconnect. Trying rapid reconnect");
        spinBLEClient.doConnectPM = true; //try rapid reconnect
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
    const char *c_PM = userConfig.getconnectedPowerMeter();
    const char *c_HR = userConfig.getconnectedHeartMonitor();
    if ((advertisedDevice->haveServiceUUID()) && (advertisedDevice->isAdvertisingService(CYCLINGPOWERSERVICE_UUID) || advertisedDevice->isAdvertisingService(FLYWHEEL_UART_SERVICE_UUID) || advertisedDevice->isAdvertisingService(FITNESSMACHINESERVICE_UUID)))
    {
        if ((advertisedDevice->getName() == c_PM) || (advertisedDevice->getAddress().toString().c_str() == c_PM) || (String(c_PM) == ("any")))
        {
            spinBLEClient.myPowerMeter = advertisedDevice;
            spinBLEClient.doConnectPM = true;
            spinBLEClient.doScan = false;
            return;
        }
    }
    if ((advertisedDevice->haveServiceUUID()) && (advertisedDevice->isAdvertisingService(HEARTSERVICE_UUID)))
    {
        if ((advertisedDevice->getName() == c_HR) || (advertisedDevice->getAddress().toString().c_str() == c_HR) || (String(c_HR) == ("any")))
        {
            spinBLEClient.myHeartMonitor = advertisedDevice;
            spinBLEClient.doConnectHR = true;
            spinBLEClient.doScan = false;
            return;
        }
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