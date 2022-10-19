/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <NimBLEDevice.h>
#include <Arduino.h>
#include <Main.h>

#pragma once


void collectAndSet(NimBLEUUID charUUID, NimBLEUUID serviceUUID, NimBLEAddress address, uint8_t *pData, size_t length);