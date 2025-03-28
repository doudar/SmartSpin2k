#ifndef SERVICEMANAGER_H
#define SERVICEMANAGER_H

#include <Arduino.h>
#include <Service.h>
#include <ServiceManagerCallbacks.h>

class ServiceManager {
 public:
  friend class BTDeviceManager;
  friend class ServiceManagerCharacteristicCallbacks;
  friend class DirConServiceManagerCallbacks;
  ServiceManager();
  std::vector<Service*> getServices();
  void addService(Service* service);
  Service* getService(const NimBLEUUID& serviceUUID);
  Service* getServiceByCharacteristic(const NimBLEUUID& characteristicUUID);
  Characteristic* getCharacteristic(const NimBLEUUID& characteristicUUID);
  void subscribeCallbacks(ServiceManagerCallbacks* callbacks);
  String getStatusMessage();

 private:
  std::vector<Service*> services;
  std::vector<ServiceManagerCallbacks*> serviceManagerCallbacks;
  String statusMessage;
  void updateStatusMessage();
};

#endif
