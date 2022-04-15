/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <WiFi.h>
#include <WiFi



class DirconServer {
    private:
    const uint dirconPort = 36866; 
    public:
    void checkForConnections();
    void sendValue();
    




}