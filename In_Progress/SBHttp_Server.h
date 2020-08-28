#ifndef SBHTTP_SERVER_H
#define SBHTTP_SERVER_H
//Webserver structures and classes
#include <Arduino.h>
#include <smartbike_parameters.h>
//function Prototypes

/*espidf stuff
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sdkconfig.h"
*/
void startHttpServer();
String processor(const String& var);

#endif