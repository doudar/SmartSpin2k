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
void BLEserverScan();
void bleClient();
double computePID(double, double);

#endif