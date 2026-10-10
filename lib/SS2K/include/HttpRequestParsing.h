/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <string>
#include <vector>
#include <utility>

namespace HttpRequestParsing {
inline bool decode(const std::string& input, std::string& output) {
  output.clear();
  auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
  for (size_t i = 0; i < input.size(); ++i) {
    char c = input[i];
    if (c == '%') {
      if (i + 2 >= input.size()) return false;
      int hi = hex(input[i + 1]), lo = hex(input[i + 2]);
      if (hi < 0 || lo < 0) return false;
      c = static_cast<char>(hi * 16 + lo); i += 2;
    } else if (c == '+') c = ' ';
    if (c == '\0' || c == '\r' || c == '\n') return false;
    output += c;
  }
  return true;
}
inline bool query(const std::string& input, std::vector<std::pair<std::string, std::string>>& out) {
  if (input.size() > 2048) return false;
  size_t pos = 0;
  while (pos < input.size()) {
    if (out.size() >= 40) return false;
    auto end = input.find('&', pos); if (end == std::string::npos) end = input.size();
    auto equal = input.find('=', pos); if (equal == std::string::npos || equal > end) equal = end;
    std::string key, value;
    if (!decode(input.substr(pos, equal - pos), key) || !decode(equal < end ? input.substr(equal + 1, end - equal - 1) : "", value)) return false;
    out.emplace_back(std::move(key), std::move(value)); pos = end + 1;
  }
  return true;
}
// Extract a quoted multipart parameter; reject header injection/path components.
inline bool filename(const std::string& headers, std::string& result) {
  const auto start = headers.find("filename=\"");
  if (start == std::string::npos) return false;
  const auto end = headers.find('"', start + 10);
  if (end == std::string::npos) return false;
  result = headers.substr(start + 10, end - start - 10);
  return !result.empty() && result.size() <= 80 && result.find_first_of("/\\\r\n") == std::string::npos;
}
}
