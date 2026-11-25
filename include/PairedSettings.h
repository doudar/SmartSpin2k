/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

namespace PairedSettings {
using Bytes                    = std::vector<uint8_t>;
constexpr uint16_t DIRCON_BASE = 0x8000;
void receive(uint16_t peer, uint64_t generation, size_t capacity, const uint8_t* data, size_t size, Bytes* immediate);
void disconnect(uint16_t peer);
void tick();
bool buttons(bool up, bool down);
bool resetPending();
bool beginReset();
bool finishReset();
bool hasRecipients();
void publish(Bytes plaintext);
using Emit     = bool (*)(uint16_t, uint64_t, const uint8_t*, size_t);
using Alive    = bool (*)(uint16_t, uint64_t);
using Dispatch = Bytes (*)(const std::string&);
void initialize(Emit emit, Alive alive, Dispatch dispatch);
}  // namespace PairedSettings
