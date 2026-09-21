/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <NimBLEDevice.h>

#include <cstddef>
#include <cstdint>
#include <string>

void collectAndSet(const NimBLEUUID& charUUID, const NimBLEUUID& serviceUUID, const std::string& uniqueName, uint8_t* pData, size_t length);
