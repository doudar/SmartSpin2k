/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

inline char foldBleIdentifierAscii(char value) {
  return (value >= 'A' && value <= 'Z') ? static_cast<char>(value + ('a' - 'A')) : value;
}

inline bool bleDeviceIdentifierEquals(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) return left == right;

  while (*left != '\0' && *right != '\0') {
    if (foldBleIdentifierAscii(*left) != foldBleIdentifierAscii(*right)) return false;
    ++left;
    ++right;
  }

  return *left == *right;
}
