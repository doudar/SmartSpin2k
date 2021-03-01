    


    #include "BLE_Common.h"
    #include "Main.h"
    
    void BLE_FTMSDecode(NimBLERemoteCharacteristic *pBLERemoteCharacteristic)
    {
        std::string data = pBLERemoteCharacteristic->getValue();
        const uint8_t *pData = reinterpret_cast<const uint8_t*>(&data[0]);
        std::unique_ptr<SensorData> sensorData = SensorDataFactory::getSensorData(pBLERemoteCharacteristic, pData, data.length());
        debugDirector(" SensorData(" + sensorData->getId() + "):[", false);
        if (sensorData->hasHeartRate())
        {
            int heartRate = sensorData->getHeartRate();
            userConfig.setSimulatedHr(heartRate);
            debugDirector(" HR(" + String(heartRate) + ")", false);
            if(!spinBLEClient.connectedHR)
            {
            spinBLEClient.connectedHR = true;
            debugDirector("Registered HRM in FTMS");
            }
        }
        if (sensorData->hasCadence())
        {
            float cadence = sensorData->getCadence();
            userConfig.setSimulatedCad(cadence);
            debugDirector(" CD(" + String(cadence) + ")", false);
        }
        if (sensorData->hasPower())
        {
            int power = sensorData->getPower();
            userConfig.setSimulatedWatts(power);
            debugDirector(" PW(" + String(power) + ")", false);
        }
        debugDirector(" ]");
    }