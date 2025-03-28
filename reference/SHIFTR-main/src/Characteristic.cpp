#include <Arduino.h>
#include <Characteristic.h>
#include <Service.h>

Characteristic::Characteristic(const NimBLEUUID& uuid, uint32_t properties) {
  this->UUID = uuid;
  this->properties = properties;
}

void Characteristic::addSubscription(uint32_t clientID) {
  if (!this->isSubscribed(clientID)) {
    this->subscriptions.push_back(clientID);
    this->doCallbacks(false);
  }
}

void Characteristic::removeSubscription(uint32_t clientID) {
  if (!this->isSubscribed(clientID)) {
    return;
  }
  std::vector<uint32_t> filteredSubscriptions;
  for (uint32_t subscription : this->subscriptions) {
    if (subscription != clientID) {
      filteredSubscriptions.push_back(subscription);
    }
  }
  this->subscriptions = filteredSubscriptions;
  this->doCallbacks(true);
}

bool Characteristic::isSubscribed(uint32_t clientID) {
  for (uint32_t subscription : this->subscriptions) {
    if (subscription == clientID) {
      return true;
      break;
    }
  }
  return false;
}

std::vector<uint32_t> Characteristic::getSubscriptions() {
  return this->subscriptions;
}

void Characteristic::subscribeCallbacks(CharacteristicCallbacks* callbacks) {
  this->characteristicCallbacks.push_back(callbacks);
}

Service* Characteristic::getService() {
  return this->service;
}

void Characteristic::setProperties(uint32_t properties) {
  this->properties = properties;
}

uint32_t Characteristic::getProperties() {
  return this->properties;
}

void Characteristic::setService(Service* service) {
  this->service = service;
}

void Characteristic::doCallbacks(bool removed) {
  for (size_t callbackIndex = 0; callbackIndex < this->characteristicCallbacks.size(); callbackIndex++) {
    this->characteristicCallbacks.at(callbackIndex)->onSubscriptionChanged(this, removed);
  }
}
