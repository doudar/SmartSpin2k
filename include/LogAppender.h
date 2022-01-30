/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

class ILogAppender {
 public:
  virtual void Initialize() = 0;
  virtual void Log(const char* message) = 0;
};