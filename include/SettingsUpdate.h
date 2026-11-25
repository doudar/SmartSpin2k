/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

// Invoked exclusively by the authenticated channel on the maintenance task.
namespace SettingsUpdate {
bool owns(uint16_t peer);
std::vector<uint8_t> command(uint16_t peer, const uint8_t* packet, size_t size);
void disconnect(uint16_t peer);
void tick();
}  // namespace SettingsUpdate
