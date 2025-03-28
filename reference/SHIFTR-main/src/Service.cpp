#include <Arduino.h>
#include <Service.h>

class ServiceCharacteristicCallbacks : public CharacteristicCallbacks {
  void onSubscriptionChanged(Characteristic* characteristic, bool removed) {
    Service* service = characteristic->getService();
    if (service != nullptr) {
      for (size_t callbackIndex = 0; callbackIndex < service->characteristicCallbacks.size(); callbackIndex++) {
        service->characteristicCallbacks.at(callbackIndex)->onSubscriptionChanged(characteristic, removed);
      }
    }
  };
};

Service::Service(const NimBLEUUID& uuid) {
  this->UUID = uuid;
}

Service::Service(const NimBLEUUID& uuid, bool advertise, bool internal) {
  this->UUID = uuid;
  this->Advertise = advertise;
  this->Internal = internal;
}

Service::~Service() {}

void Service::addCharacteristic(Characteristic* characteristic) {
  characteristic->setService(this);
  characteristic->subscribeCallbacks(new ServiceCharacteristicCallbacks);
  this->characteristics.push_back(characteristic);
}

bool Service::isAdvertised() {
  return this->Advertise;
}

bool Service::isInternal() {
  return this->Internal;
}

Characteristic* Service::getCharacteristic(const NimBLEUUID& characteristicUUID) {
  for (size_t characteristicIndex = 0; characteristicIndex < this->characteristics.size(); characteristicIndex++) {
    if (this->characteristics.at(characteristicIndex)->UUID.equals(characteristicUUID)) {
      return this->characteristics.at(characteristicIndex);
      break;
    }
  }
  return nullptr;
}

std::vector<Characteristic*> Service::getCharacteristics() {
  return this->characteristics;
}

ServiceManager* Service::getServiceManager() {
  return this->serviceManager;
}

void Service::subscribeCallbacks(CharacteristicCallbacks* callbacks) {
  this->characteristicCallbacks.push_back(callbacks);
}