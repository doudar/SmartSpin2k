#ifndef CHARACTERISTICCALLBACKS_H
#define CHARACTERISTICCALLBACKS_H

#include <Characteristic.h>

class Characteristic;

class CharacteristicCallbacks {
 public:
  virtual ~CharacteristicCallbacks() {}
  virtual void onSubscriptionChanged(Characteristic* characteristic, bool removed) {};
};

#endif