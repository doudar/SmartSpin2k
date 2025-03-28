#ifndef CHARACTERISTIC_H
#define CHARACTERISTIC_H

#include <CharacteristicCallbacks.h>
#include <NimBLEUUID.h>
#include <Service.h>

#include <vector>

class Service;

class Characteristic {
 public:
  friend class Service;
  Characteristic(const NimBLEUUID& uuid, uint32_t properties);
  NimBLEUUID UUID;
  void addSubscription(uint32_t clientID);
  void removeSubscription(uint32_t clientID);
  bool isSubscribed(uint32_t clientID);
  std::vector<uint32_t> getSubscriptions();
  void subscribeCallbacks(CharacteristicCallbacks* callbacks);
  Service* getService();
  void setProperties(uint32_t properties);
  uint32_t getProperties();

 private:
  uint32_t properties;
  std::vector<uint32_t> subscriptions;
  std::vector<CharacteristicCallbacks*> characteristicCallbacks;
  Service* service;
  void doCallbacks(bool removed);
  void setService(Service* service);
};

#endif
