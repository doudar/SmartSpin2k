#ifndef BTADVERTISEDDEVICECALLBACKS_H
#define BTADVERTISEDDEVICECALLBACKS_H

#include <NimBLEDevice.h>

class BTAdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* advertisedDevice);
};

#endif
