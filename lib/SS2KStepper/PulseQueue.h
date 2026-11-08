// Copyright (C) 2026 Anthony Doud & Joel Baranick
// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <driver/mcpwm_prelude.h>
#include <driver/pulse_cnt.h>
#include <freertos/FreeRTOS.h>
#include <soc/mcpwm_struct.h>
#include <esp_intr_alloc.h>

namespace ss2k_motor {
struct Pulse {
  int32_t start;
  uint16_t ticks;
  uint8_t steps;
  bool forward;
  bool toggle_dir;
};

// One motor owns MCPWM group 0/timer 0 and one PCNT unit. No Arduino calls.
// Task and interrupt accesses share a spinlock; planner arithmetic stays outside it.
class PulseQueue {
 public:
  static constexpr unsigned capacity = 32;
  bool begin(uint8_t pin);
  void release();
  bool push(uint16_t ticks, uint8_t steps, bool forward, bool toggle = false);
  bool start();
  int32_t stop();
  int32_t position();
  void rebase(int32_t value);
  bool busy();
  uint32_t queuedTicks();
  bool direction() const { return tailForward; } // Caller holds controller mutex.
  void directionPin(uint8_t pin, bool highCountsUp);

  // The ISR helpers below only access these fields with mux held.
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  Pulse entries[capacity] = {};
  uint8_t read = 0, write = 0;
  bool running = false, prepared = false;
  int32_t tailPosition = 0;
  bool tailForward = true;
  uint8_t dirPin = 255;
  bool directionHigh = true;
  mcpwm_timer_handle_t timer = nullptr;
  mcpwm_oper_handle_t oper = nullptr;
  mcpwm_cmpr_handle_t comparator = nullptr;
  mcpwm_gen_handle_t generator = nullptr;
  pcnt_unit_handle_t counter = nullptr;
  pcnt_channel_handle_t channel = nullptr;
  intr_handle_t interrupt = nullptr;
  uint8_t counterId = 0;
  bool counterEnabled = false, timerEnabled = false;
};
}  // namespace ss2k_motor
