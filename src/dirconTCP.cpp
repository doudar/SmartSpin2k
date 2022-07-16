/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "dirconTCP.h"

WiFiServer dServer(dirconPort);
WiFiClient remoteClient;
DirconServer dirconServer;

void DirconServer::start(){
    Serial.println("Dircon Server Starting");
    dServer.begin();
} 

void DirconServer::checkForConnections(){
    if(dServer.hasClient()){
        if(remoteClient.connected()){
            Serial.println("Connection Rejected");
            dServer.available().stop();
        } else{
            Serial.println("Connection accepted");
            remoteClient = dServer.available();
        }
    }
}

void DirconServer::sendValue(){
    if(remoteClient.connected()){
        remoteClient.write("This is a test message");
    }
}


