#include <Arduino.h>
#include <Service.h>
#include <ServiceManager.h>

class ServiceManagerCharacteristicCallbacks : public CharacteristicCallbacks {
  void onSubscriptionChanged(Characteristic* characteristic, bool removed) {
    ServiceManager* serviceManager = characteristic->getService()->getServiceManager();
    for (size_t callbackIndex = 0; callbackIndex < serviceManager->serviceManagerCallbacks.size(); callbackIndex++) {
      serviceManager->serviceManagerCallbacks.at(callbackIndex)->onCharacteristicSubscriptionChanged(characteristic, removed);
    }
  };
};

ServiceManager::ServiceManager() {}

std::vector<Service*> ServiceManager::getServices() {
  return this->services;
}

void ServiceManager::addService(Service* service) {
  service->serviceManager = this;
  service->subscribeCallbacks(new ServiceManagerCharacteristicCallbacks);
  this->services.push_back(service);
  for (size_t callbackIndex = 0; callbackIndex < this->serviceManagerCallbacks.size(); callbackIndex++) {
    this->serviceManagerCallbacks.at(callbackIndex)->onServiceAdded(service);
  }
  this->updateStatusMessage();
}

Service* ServiceManager::getService(const NimBLEUUID& serviceUUID) {
  for (size_t serviceIndex = 0; serviceIndex < this->services.size(); serviceIndex++) {
    if (this->services.at(serviceIndex)->UUID.equals(serviceUUID)) {
      return this->services.at(serviceIndex);
      break;
    }
  }
  return nullptr;
}

Service* ServiceManager::getServiceByCharacteristic(const NimBLEUUID& characteristicUUID) {
  for (size_t serviceIndex = 0; serviceIndex < this->services.size(); serviceIndex++) {
    if (this->services.at(serviceIndex)->getCharacteristic(characteristicUUID) != nullptr) {
      return this->services.at(serviceIndex);
      break;
    }
  }
  return nullptr;
}

Characteristic* ServiceManager::getCharacteristic(const NimBLEUUID& characteristicUUID) {
  for (size_t serviceIndex = 0; serviceIndex < this->services.size(); serviceIndex++) {
    if (this->services.at(serviceIndex)->getCharacteristic(characteristicUUID) != nullptr) {
      return this->services.at(serviceIndex)->getCharacteristic(characteristicUUID);
      break;
    }
  }
  return nullptr;
}

void ServiceManager::subscribeCallbacks(ServiceManagerCallbacks* callbacks) {
  this->serviceManagerCallbacks.push_back(callbacks);
}

String ServiceManager::getStatusMessage() {
  return this->statusMessage;
}

void ServiceManager::updateStatusMessage() {
  this->statusMessage = this->services.size();
  this->statusMessage += " service(s) registered";
}