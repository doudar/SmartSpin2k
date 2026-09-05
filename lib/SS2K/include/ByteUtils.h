/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <stdint.h>
#include <os/endian.h>

// The underlying endian API deliberately exposes unsigned bit patterns. These
// wrappers make two's-complement signed protocol fields explicit at call sites.
inline void put_le16s(void* buffer, int16_t value) { put_le16(buffer, static_cast<uint16_t>(value)); }
inline void put_le32s(void* buffer, int32_t value) { put_le32(buffer, static_cast<uint32_t>(value)); }
inline int16_t get_le16s(const void* buffer) { return static_cast<int16_t>(get_le16(buffer)); }
inline int32_t get_le32s(const void* buffer) { return static_cast<int32_t>(get_le32(buffer)); }

