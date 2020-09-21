#ifndef SBBLE_SERVER_H
#define SBBLE_SERVER_H

#include <Main.h>
/*espidf stuff
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sdkconfig.h"
*/
void startBLEServer();
void BLENotify();
bool connectToServer();
void BLEServerScan(bool connectRequest);
void bleClient();
void startBleClient();
double computePID(double, double);
void setupBLE();

#endif