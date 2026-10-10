/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <atomic>

// Only one transport may modify flash/update state at a time. Successful
// updates retain ownership until reboot; failures release after cleanup.
namespace FirmwareUpdateOwner {
enum class Owner { None, Web, Ble };
inline std::atomic<Owner> current{Owner::None};
inline bool acquire(Owner owner) {
  auto expected = Owner::None;
  return current.compare_exchange_strong(expected, owner);
}
inline void release(Owner owner) { current.compare_exchange_strong(owner, Owner::None); }
}
