#ifndef BTDEVICEMANAGER_H
#define BTDEVICEMANAGER_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ServiceManager.h>
#include <arduino-timer.h>

class BTDeviceManager {
 public:
  static bool start();
  static void stop();
  static void update();
  static void setLocalDeviceName(const std::string localDeviceName);
  static void setRemoteDeviceNameFilter(const std::string remoteDeviceNameFilter);
  static void setServiceManager(ServiceManager* serviceManager);
  static void addRemoteDeviceFilter(const NimBLEUUID& serviceUUID);
  static bool isConnected();
  static bool isStarted();
  static std::string getConnecedDeviceName();
  static bool writeBLECharacteristic(const NimBLEUUID& serviceUUID, const NimBLEUUID& characteristicUUID, std::vector<uint8_t>* data);
  static std::vector<uint8_t> readBLECharacteristic(const NimBLEUUID& serviceUUID, const NimBLEUUID& characteristicUUID);
  static bool writeFECTargetPower(uint16_t targetPower);
  static bool writeFECTrackResistance(uint16_t grade, uint8_t rollingResistance = 0xFF);
  static bool writeFECBasicResistance(uint8_t totalResistance);
  static bool writeFECUserConfiguration(uint16_t bicycleWeight, uint16_t userWeight, uint8_t wheelDiameter, uint8_t gearRatio);
  static bool writeFECCapabilitiesRequest();
  static std::vector<NimBLEAdvertisedDevice>* getScannedDevices();
  static String getStatusMessage();

 private:
  friend class BTAdvertisedDeviceCallbacks;
  friend class BTClientCallbacks;
  friend class BTDeviceServiceManagerCallbacks;
  static std::string deviceName;
  static std::string remoteDeviceName;
  static std::string connectedDeviceName;
  static bool connected;
  static bool started;
  static ServiceManager* serviceManager;
  static NimBLEClient* nimBLEClient;
  static std::vector<NimBLEUUID> remoteDeviceFilterUUIDs;
  static std::vector<NimBLEAdvertisedDevice> scannedDevices;
  static Timer<> scanTimer;
  static Timer<> connectTimer;
  static NimBLEAdvertisedDevice* getRemoteDevice();
  static bool connectRemoteDevice(NimBLEAdvertisedDevice* remoteDevice);
  static void onScanEnd(NimBLEScanResults scanResults);
  static void startScan();
  static void stopScan();
  static bool doScan(void* argument);
  static bool doConnect(void* argument);
  static uint32_t getProperties(NimBLERemoteCharacteristic* remoteCharacteristic);
  static void onBLENotify(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify);
  static bool changeBLENotify(Characteristic* characteristic, bool remove);
  static uint8_t getFECChecksum(std::vector<uint8_t>* fecData);
  static String statusMessage;
};

#endif