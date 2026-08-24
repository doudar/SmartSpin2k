/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

using String = std::string;

inline unsigned long millis() {
  using namespace std::chrono;
  return static_cast<unsigned long>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
