// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

#include "Main.h"
#include <math.h>
#include "BLE_Common.h"
#include "sensors/SensorData.h"
#include "sensors/SensorDataFactory.h"
//#include <queue>

int bleConnDesc = 1;
bool _BLEClientConnected = false;
bool updateConnParametersFlag = false;
TaskHandle_t BLECommunicationTask;
SensorDataFactory sensorDataFactory;

void BLECommunications(void *pvParameters)
{
    for (;;)
    {
        //**********************************Client***************************************/
        for (size_t x = 0; x < NUM_BLE_DEVICES; x++) //loop through discovered devices
        {
            if (spinBLEClient.myBLEDevices[x].connectedClientID != BLE_HS_CONN_HANDLE_NONE)
            {
                //spinBLEClient.myBLEDevices[x].print();
                if (spinBLEClient.myBLEDevices[x].advertisedDevice) //is device registered?
                {
                   // debugDirector("1",false);
                    SpinBLEAdvertisedDevice myAdvertisedDevice = spinBLEClient.myBLEDevices[x];
                    if ((myAdvertisedDevice.connectedClientID != BLE_HS_CONN_HANDLE_NONE) && (myAdvertisedDevice.doConnect == false)) //client must not be in connection process
                    {
                     //   debugDirector("2",false);
                        if (NimBLEDevice::getClientByPeerAddress(myAdvertisedDevice.peerAddress)) //nullptr check
                        {
                       //     debugDirector("3",false);
                            BLEClient *pClient = NimBLEDevice::getClientByPeerAddress(myAdvertisedDevice.peerAddress);
                            if ((myAdvertisedDevice.serviceUUID != BLEUUID((uint16_t)0x0000)) && (pClient->isConnected())) //Client connected with a valid UUID registered
                            {
                         //       debugDirector("4");
                                //Write the recieved data to the Debug Director

                                BLERemoteCharacteristic *pRemoteBLECharacteristic = pClient->getService(myAdvertisedDevice.serviceUUID)->getCharacteristic(myAdvertisedDevice.charUUID);
                                std::string data = pRemoteBLECharacteristic->getValue();
                                uint8_t *pData = reinterpret_cast<uint8_t *>(&data[0]);
                                int length = data.length();

                                // 250 == Data(60), Spaces(Data/2), Arrow(4), SvrUUID(37), Sep(3), ChrUUID(37), Sep(3),
                                //        Name(10), Prefix(2), HR(8), SEP(1), CD(10), SEP(1), PW(8), SEP(1), SP(7), Suffix(2), Nul(1) - 225 rounded up
                                char logBuf[250];
                                char *logBufP = logBuf;
                                for (int i = 0; i < length; i++)

                                {
                                    logBufP += sprintf(logBufP, "%02x ", pData[i]);
                                }
                                logBufP += sprintf(logBufP, "<- %s | %s", myAdvertisedDevice.serviceUUID.toString().c_str(), myAdvertisedDevice.charUUID.toString().c_str());

                                std::shared_ptr<SensorData> sensorData = sensorDataFactory.getSensorData(pRemoteBLECharacteristic, pData, length);

                                logBufP += sprintf(logBufP, " | %s:[", sensorData->getId().c_str());
                                if (sensorData->hasHeartRate())
                                {
                                    int heartRate = sensorData->getHeartRate();
                                    userConfig.setSimulatedHr(heartRate);
                                    spinBLEClient.connectedHR |= true;
                                    logBufP += sprintf(logBufP, " HR(%d)", heartRate % 1000);
                                }
                                if (sensorData->hasCadence())
                                {
                                    float cadence = sensorData->getCadence();
                                    userConfig.setSimulatedCad(cadence);
                                    spinBLEClient.connectedCD |= true;
                                    logBufP += sprintf(logBufP, " CD(%.2f)", fmodf(cadence, 1000.0));
                                }
                                if (sensorData->hasPower())
                                {
                                    int power = sensorData->getPower();
                                    userConfig.setSimulatedWatts(power);
                                    spinBLEClient.connectedPM |= true;
                                    logBufP += sprintf(logBufP, " PW(%d)", power % 10000);
                                }
                                if (sensorData->hasSpeed())
                                {
                                    float speed = sensorData->getSpeed();
                                    userConfig.setSimulatedSpeed(speed);
                                    logBufP += sprintf(logBufP, " SD(%.2f)", fmodf(speed, 1000.0));
                                }
                                strcat(logBufP, " ]");
                                debugDirector(String(logBuf), true, true);
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

