/*
ESP32 Web Server - AP Mode
modified on 25 MAy 2019
by Mohammadreza Akbari @ Electropeak
https://electropeak.com/learn
*/
#ifndef HTTP_SERVER_BASIC_H
#define HTTP_SERVER_BASIC_H

#include <Main.h>
#include <smartbike_parameters.h>



void startHttpServer();
void stopHttpServer();
void webClientUpdate(void * pvParameters);
void handleIndexFile();
void handleStyleCss();
void handleHrSlider();


//wifi Function
void stopWifi();
void startWifi();



#endif
