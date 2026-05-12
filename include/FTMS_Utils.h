/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <stdint.h>

inline bool ftmsTargetPowerRequestsFreeRide(uint16_t targetPower) { return targetPower == 0; }
