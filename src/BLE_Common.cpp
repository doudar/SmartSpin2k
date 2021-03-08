// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

#include "Main.h"
#include <math.h>
#include "BLE_Common.h"
//#include <queue>

int bleConnDesc = 1;
bool _BLEClientConnected = false;
bool updateConnParametersFlag = false;
TaskHandle_t BLECommunicationTask;

void BLECommunications(void *pvParameters)
{
    for (;;)
    {
        //**********************************Client***************************************/
        for (size_t x = 0; x < NUM_BLE_DEVICES; x++) //loop through discovered devices
        {
            if (spinBLEClient.myBLEDevices[x].advertisedDevice) //is device registered?
            {
                myAdvertisedBLEDevice myAdvertisedDevice = spinBLEClient.myBLEDevices[x];
                if ((myAdvertisedDevice.connectedClientID != BLE_HS_CONN_HANDLE_NONE) && (myAdvertisedDevice.doConnect == false)) //client must not be in connection process
                {
                    if (NimBLEDevice::getClientByPeerAddress(myAdvertisedDevice.peerAddress)) //nullptr check
                    {
                        BLEClient *pClient = NimBLEDevice::getClientByPeerAddress(myAdvertisedDevice.peerAddress);
                        if ((myAdvertisedDevice.serviceUUID != BLEUUID((uint16_t)0x0000)) && (pClient->isConnected())) //Client connected with a valid UUID registered
                        {
                            //Write the recieved data to the Debug Director
                            BLERemoteCharacteristic *pRemoteBLECharacteristic = pClient->getService(myAdvertisedDevice.serviceUUID)->getCharacteristic(myAdvertisedDevice.charUUID); //get the registered services
                            std::string pData = pRemoteBLECharacteristic->getValue(); //read the data
                            int length = pData.length();
                            String debugOutput = "";
                            for (int i = 0; i < length; i++) //loop and print data
                            {
                                debugOutput += String(pData[i], HEX) + " ";
                            }
                            debugDirector(debugOutput + "<-" + String(myAdvertisedDevice.serviceUUID.toString().c_str()) + " | " + String(myAdvertisedDevice.charUUID.toString().c_str()), true, true);
                            
                            if (pRemoteBLECharacteristic->getUUID() == CYCLINGPOWERMEASUREMENT_UUID)
                            {
                                BLE_CPSDecode(pRemoteBLECharacteristic);
                                if (!spinBLEClient.connectedPM)
                                {
                                    spinBLEClient.connectedPM = true;
                                }
                            }
                            if ((pRemoteBLECharacteristic->getUUID() == FITNESSMACHINEINDOORBIKEDATA_UUID) || (pRemoteBLECharacteristic->getUUID() == FLYWHEEL_UART_SERVICE_UUID) || (pRemoteBLECharacteristic->getUUID() == HEARTCHARACTERISTIC_UUID))
                            {
                                BLE_FTMSDecode(pRemoteBLECharacteristic);
                                if ((!spinBLEClient.connectedPM) &&(pRemoteBLECharacteristic->getUUID()!= HEARTCHARACTERISTIC_UUID)) //PM flag for HR-->PWR
                                {
                                    spinBLEClient.connectedPM = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        //***********************************SERVER**************************************/
        if ((spinBLEClient.connectedHR && !spinBLEClient.connectedPM) && (userConfig.getSimulatedHr() > 0) && userPWC.hr2Pwr)
        {
            calculateInstPwrFromHR();
        }
        if (!spinBLEClient.connectedPM && !userPWC.hr2Pwr)
        {
            userConfig.setSimulatedCad(0);
            userConfig.setSimulatedWatts(0);
        }
        if (!spinBLEClient.connectedHR)
        {
            userConfig.setSimulatedHr(0);
        }

        if (_BLEClientConnected)
        {
            //update the BLE information on the server
            computeCSC();
            updateIndoorBikeDataChar();
            updateCyclingPowerMesurementChar();
            updateHeartRateMeasurementChar();
            GlobalBLEClientConnected = true;

            if (updateConnParametersFlag)
            {
                vTaskDelay(100 / portTICK_PERIOD_MS);
                BLEDevice::getServer()->updateConnParams(bleConnDesc, 40, 50, 0, 100);
                updateConnParametersFlag = false;
            }
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
#ifdef DEBUG_STACK
        Serial.printf("BLEComm: %d \n", uxTaskGetStackHighWaterMark(BLECommunicationTask));
#endif
    }
}
