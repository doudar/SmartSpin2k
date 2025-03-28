#ifndef SERVICE_H
#define SERVICE_H

#include <Characteristic.h>
#include <CharacteristicCallbacks.h>
#include <NimBLEUUID.h>

#include <vector>

class ServiceManager;

class Service {
 public:
  friend class Characteristic;
  friend class ServiceCharacteristicCallbacks;
  friend class ServiceManager;
  Service(const NimBLEUUID& uuid);
  Service(const NimBLEUUID& uuid, bool advertise, bool internal);
  ~Service();
  void addCharacteristic(Characteristic* characteristic);
  std::vector<Characteristic*> getCharacteristics();
  bool isAdvertised();
  bool isInternal();
  Characteristic* getCharacteristic(const NimBLEUUID& characteristicUUID);
  ServiceManager* getServiceManager();
  void subscribeCallbacks(CharacteristicCallbacks* callbacks);
  NimBLEUUID UUID;
  bool Advertise = false;
  bool Internal = false;

 private:
  std::vector<Characteristic*> characteristics;
  ServiceManager* serviceManager;
  std::vector<CharacteristicCallbacks*> characteristicCallbacks;
};

#endif
