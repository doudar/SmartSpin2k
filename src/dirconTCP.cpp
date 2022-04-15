/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "main.h"
#include "dirconTCP.h"

WiFiServer dirconServer(dirconPort);
WiFiClient remoteDirconClient;
DirconServer dirconServer;

DirconServer::checkForConnections(){ 
    if dirconServer.hasClient(){
        if(remoteDirconClient.hasClient()){
            Serial.println("Connection Rejected");
            dirconServer.available().stop();
        } else{
            Serial.println("Connection accepted");
            remoteDirconClient = dirconServer.available();
        }
    }
}

void DirconServer::sendValue(){
    if(remoteDirconClient.connected()){
        remoteDirconClient.write("This is a test message");
    }
}