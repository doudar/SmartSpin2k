/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// MCPWM/PCNT timing adapted from FastAccelStepper 2.0.1, commit f0fac8d6.
// Copyright (c) 2020 J.Kiemes; Copyright (C) 2026 Anthony Doud & Joel Baranick
// The upstream timing code is licensed under the MIT License (see LICENSE.timing).

// Only ESP32/S3, one motor, ESP-IDF 5.5 are supported here.
#include "PulseQueue.h"
#include <driver/gpio.h>
#include <esp_idf_version.h>
#include <esp_rom_gpio.h>
#include <hal/gpio_ll.h>
#include <soc/gpio_struct.h>
#include <soc/pcnt_struct.h>
#include <soc/pcnt_reg.h>
#include <soc/periph_defs.h>

#if ESP_IDF_VERSION_MAJOR != 5 || ESP_IDF_VERSION_MINOR != 5
#error "Review MCPWM/PCNT register and handle layout before changing ESP-IDF"
#endif
#if !CONFIG_IDF_TARGET_ESP32 && !CONFIG_IDF_TARGET_ESP32S3
#error "SS2KStepper supports classic ESP32 and ESP32-S3 only"
#endif

#if CONFIG_IDF_TARGET_ESP32S3
#define COUNT_LIMIT cnt_h_lim_un
#define COUNT_VALUE pulse_cnt_un
#define HIGH_EVENT thr_h_lim_en_un
#define LOW_EVENT thr_l_lim_en_un
#else
#define COUNT_LIMIT cnt_h_lim
#define COUNT_VALUE cnt_val
#define HIGH_EVENT thr_h_lim_en
#define LOW_EVENT thr_l_lim_en
#endif

namespace ss2k_motor {
namespace {
constexpr uint32_t timerCompareInterrupt = 1u << 15;
// ESP-IDF does not expose the allocated PCNT unit number. This prefix is shared
// by IDF 5.5 classic/S3; the version guard above makes SDK upgrades explicit.
struct CounterPrefix { void* group; portMUX_TYPE spinlock; int unit_id; };

inline void IRAM_ATTR clearCounter(uint8_t id) {
  REG_SET_BIT(PCNT_CTRL_REG, 1u << (2 * id));
  REG_CLR_BIT(PCNT_CTRL_REG, 1u << (2 * id));
}

void IRAM_ATTR halt(PulseQueue* q) {
  MCPWM0.timer[0].timer_cfg1.timer_start = 0; // Stop at zero, with output low.
  MCPWM0.int_ena.val &= ~timerCompareInterrupt;
  MCPWM0.operators[0].generator[0].gen_utea = 1;
  q->running = false;
}

void IRAM_ATTR prepare(PulseQueue* q, const Pulse& next) {
  if (next.steps) PCNT.conf_unit[q->counterId].conf2.COUNT_LIMIT = next.steps;
}

void IRAM_ATTR apply(PulseQueue* q, const Pulse& pulse) {
  if (pulse.toggle_dir && q->dirPin != 255) {
    gpio_ll_set_level(&GPIO, q->dirPin, gpio_ll_get_level(&GPIO, q->dirPin) ^ 1);
  }
  MCPWM0.timer[0].timer_cfg0.timer_period_upmethod = MCPWM0.timer[0].timer_status.timer_value <= 1 ? 0 : 1;
  MCPWM0.timer[0].timer_cfg0.timer_period = pulse.ticks;
  if (!pulse.steps) {
    MCPWM0.operators[0].generator[0].gen_utea = 1;
    MCPWM0.int_clr.val = timerCompareInterrupt;
    MCPWM0.int_ena.val |= timerCompareInterrupt;
  } else {
    bool disableCompare = true;
    if (PCNT.conf_unit[q->counterId].conf2.COUNT_LIMIT != pulse.steps) {
      const uint16_t before = pulse.steps - PCNT.cnt_unit[q->counterId].COUNT_VALUE;
      MCPWM0.int_clr.val = timerCompareInterrupt;
      const uint16_t after = pulse.steps - PCNT.cnt_unit[q->counterId].COUNT_VALUE;
      if (before != after) MCPWM0.int_clr.val = timerCompareInterrupt;
      PCNT.conf_unit[q->counterId].conf2.COUNT_LIMIT = after;
      clearCounter(q->counterId);
      if ((MCPWM0.int_raw.val & timerCompareInterrupt) && PCNT.cnt_unit[q->counterId].COUNT_VALUE == 0) {
        PCNT.conf_unit[q->counterId].conf2.COUNT_LIMIT = after - 1;
        clearCounter(q->counterId);
        disableCompare = after > 0;
      }
    }
    if (disableCompare) MCPWM0.int_ena.val &= ~timerCompareInterrupt;
    MCPWM0.operators[0].generator[0].gen_utea = 2;
  }
}

void IRAM_ATTR advance(PulseQueue* q) {
  const bool prepared = q->prepared;
  q->prepared = false;
  if (q->read != q->write) {
    ++q->read;
    if (q->read != q->write) {
      const Pulse& current = q->entries[q->read % PulseQueue::capacity];
      if (!prepared) { prepare(q, current); clearCounter(q->counterId); }
      apply(q, current);
      const uint8_t next = q->read + 1;
      if (next != q->write && current.steps) {
        q->prepared = true;
        prepare(q, q->entries[next % PulseQueue::capacity]);
      }
      return;
    }
  }
  halt(q);
}

void IRAM_ATTR counterInterrupt(void* arg) {
  auto* q = static_cast<PulseQueue*>(arg);
  portENTER_CRITICAL_ISR(&q->mux);
  const uint32_t bit = 1u << q->counterId;
  if (PCNT.int_st.val & bit) {
    PCNT.int_clr.val = bit;
    if (q->running) advance(q);
  }
  portEXIT_CRITICAL_ISR(&q->mux);
}

bool IRAM_ATTR compareInterrupt(mcpwm_cmpr_handle_t, const mcpwm_compare_event_data_t*, void* arg) {
  auto* q = static_cast<PulseQueue*>(arg);
  portENTER_CRITICAL_ISR(&q->mux);
  if (q->running) advance(q);
  portEXIT_CRITICAL_ISR(&q->mux);
  return false;
}

// Caller holds mux. Full 32-bit queue positions avoid position reconstruction.
int32_t currentPosition(PulseQueue* q) {
  if (q->read == q->write) return q->tailPosition;
  const Pulse& current = q->entries[q->read % PulseQueue::capacity];
  if (!current.steps) return current.start;
  const uint32_t bit = 1u << q->counterId;
  uint32_t count, limit;
  // mux blocks the ISR, not PCNT: a high-limit edge can reset the counter
  // between register reads. Retry that crossing so pre-reset pulses are never
  // added to a completed batch. The flag stays latched while mux is held.
  do {
    limit = PCNT.int_st.val & bit;
    count = PCNT.cnt_unit[q->counterId].COUNT_VALUE;
  } while (limit != (PCNT.int_st.val & bit));
  if (limit) count += current.steps;
  return static_cast<int32_t>(static_cast<int64_t>(current.start) + (current.forward ? static_cast<int64_t>(count) : -static_cast<int64_t>(count)));
}
}  // namespace

bool PulseQueue::begin(uint8_t pin) {
  if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) return false;
  // All handles start null, so every allocation failure can unwind safely.
  auto setup = [&]() -> esp_err_t {
#define TRY(call) do { esp_err_t result = (call); if (result != ESP_OK) return result; } while (0)
    pcnt_unit_config_t pc = {};
    pc.low_limit = -32768; pc.high_limit = 32767; pc.intr_priority = 1;
    TRY(pcnt_new_unit(&pc, &counter));
    counterId = static_cast<uint8_t>(reinterpret_cast<CounterPrefix*>(counter)->unit_id);
    pcnt_chan_config_t chan = {};
    chan.edge_gpio_num = pin; chan.level_gpio_num = -1;
    TRY(pcnt_new_channel(counter, &chan, &channel));
    TRY(pcnt_channel_set_edge_action(channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD));
    PCNT.conf_unit[counterId].conf2.COUNT_LIMIT = 1;
    PCNT.conf_unit[counterId].conf0.HIGH_EVENT = 1;
    PCNT.conf_unit[counterId].conf0.LOW_EVENT = 0;
    PCNT.int_clr.val = 1u << counterId;
    TRY(esp_intr_alloc(ETS_PCNT_INTR_SOURCE, ESP_INTR_FLAG_SHARED | ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL1, counterInterrupt, this, &interrupt));
    PCNT.int_ena.val |= 1u << counterId;
    TRY(pcnt_unit_clear_count(counter));
    TRY(pcnt_unit_enable(counter)); counterEnabled = true;
    TRY(pcnt_unit_start(counter));

    mcpwm_timer_config_t tc = {};
    tc.group_id = 0; tc.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT;
    tc.resolution_hz = 16000000; tc.count_mode = MCPWM_TIMER_COUNT_MODE_UP_DOWN;
    tc.period_ticks = 400; tc.intr_priority = 1;
    TRY(mcpwm_new_timer(&tc, &timer));
    mcpwm_operator_config_t oc = {};
    oc.group_id = 0; oc.intr_priority = 1;
    oc.flags.update_gen_action_on_tez = 1; oc.flags.update_gen_action_on_tep = 1;
    TRY(mcpwm_new_operator(&oc, &oper));
    TRY(mcpwm_operator_connect_timer(oper, timer));
    mcpwm_comparator_config_t cc = {}; cc.intr_priority = 1;
    TRY(mcpwm_new_comparator(oper, &cc, &comparator));
    TRY(mcpwm_comparator_set_compare_value(comparator, 1));
    mcpwm_generator_config_t gc = {}; gc.gen_gpio_num = pin; gc.flags.io_loop_back = 1;
    TRY(mcpwm_new_generator(oper, &gc, &generator));
    mcpwm_comparator_event_callbacks_t cb = {}; cb.on_reach = compareInterrupt;
    TRY(mcpwm_comparator_register_event_callbacks(comparator, &cb, this));
    // The controller exclusively owns group 0's first timer/operator. Up/down
    // mode at 32 MHz gives 16 MHz pulse intervals, on both classic and S3.
    MCPWM0.operators[0].generator[0].val = 0;
    MCPWM0.operators[0].generator[1].val = 0;
    MCPWM0.operators[0].generator[0].gen_dtep = 1;
    MCPWM0.operators[0].gen_stmp_cfg.gen_a_upmethod = 0;
    MCPWM0.operators[0].dt_cfg.val = 0;
    MCPWM0.operators[0].carrier_cfg.val = 0;
    MCPWM0.operators[0].gen_force.val = 0;
    MCPWM0.clk_cfg.clk_prescale = 4;
    MCPWM0.timer[0].timer_cfg0.timer_prescale = 0;
    MCPWM0.operator_timersel.operator0_timersel = 0;
    TRY(mcpwm_timer_enable(timer)); timerEnabled = true;
#undef TRY
    return ESP_OK;
  };
  if (setup() == ESP_OK) return true;
  release();
  return false;
}

void PulseQueue::release() {
  if (counter) { PCNT.int_ena.val &= ~(1u << counterId); PCNT.int_clr.val = 1u << counterId; }
  if (interrupt) { esp_intr_free(interrupt); interrupt = nullptr; }
  if (timerEnabled) { mcpwm_timer_disable(timer); timerEnabled = false; }
  if (generator) { mcpwm_del_generator(generator); generator = nullptr; }
  if (comparator) { mcpwm_del_comparator(comparator); comparator = nullptr; }
  if (oper) { mcpwm_del_operator(oper); oper = nullptr; }
  if (timer) { mcpwm_del_timer(timer); timer = nullptr; }
  if (counterEnabled) { pcnt_unit_stop(counter); pcnt_unit_disable(counter); counterEnabled = false; }
  if (channel) { pcnt_del_channel(channel); channel = nullptr; }
  if (counter) { pcnt_del_unit(counter); counter = nullptr; }
}

bool PulseQueue::push(uint16_t ticks, uint8_t steps, bool forward, bool toggle) {
  if (!ticks) return false;
  portENTER_CRITICAL(&mux);
  const bool room = static_cast<uint8_t>(write - read) < capacity;
  if (room) {
    entries[write % capacity] = {tailPosition, ticks, steps, forward, toggle};
    tailPosition += forward ? steps : -static_cast<int32_t>(steps);
    tailForward = forward;
    ++write;
  }
  portEXIT_CRITICAL(&mux);
  return room;
}

bool PulseQueue::start() {
  portENTER_CRITICAL(&mux);
  if (running || read == write || MCPWM0.timer[0].timer_status.timer_value > 1) {
    portEXIT_CRITICAL(&mux);
    return false;
  }
  clearCounter(counterId);
  PCNT.int_clr.val = 1u << counterId;
  running = true; prepared = false;
  MCPWM0.operators[0].gen_cfg0.gen_cfg_upmethod = 0;
  apply(this, entries[read % capacity]);
  MCPWM0.operators[0].gen_cfg0.gen_cfg_upmethod = 3;
  mcpwm_generator_set_force_level(generator, -1, true);
  mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP);
  portEXIT_CRITICAL(&mux);
  return true;
}

int32_t PulseQueue::stop() {
  if (!counter) return tailPosition;
  portENTER_CRITICAL(&mux);
  mcpwm_generator_set_force_level(generator, 0, true);
  const int32_t actual = currentPosition(this);
  halt(this);
  PCNT.int_clr.val = 1u << counterId;
  read = write; prepared = false; tailPosition = actual;
  // The queued tail may be after a direction change that was just cancelled.
  if (dirPin != 255) tailForward = gpio_ll_get_level(&GPIO, dirPin) == directionHigh;
  portEXIT_CRITICAL(&mux);
  return actual;
}

int32_t PulseQueue::position() {
  if (!counter) return tailPosition;
  portENTER_CRITICAL(&mux);
  const int32_t actual = currentPosition(this);
  portEXIT_CRITICAL(&mux);
  return actual;
}
void PulseQueue::rebase(int32_t value) { portENTER_CRITICAL(&mux); tailPosition = value; portEXIT_CRITICAL(&mux); }
bool PulseQueue::busy() { portENTER_CRITICAL(&mux); bool value = running || read != write; portEXIT_CRITICAL(&mux); return value; }
uint32_t PulseQueue::queuedTicks() {
  portENTER_CRITICAL(&mux);
  uint32_t ticks = 0;
  for (uint8_t index = read; index != write; ++index) {
    const Pulse& p = entries[index % capacity];
    ticks += p.ticks * (p.steps ? p.steps : 1u);
  }
  portEXIT_CRITICAL(&mux);
  return ticks;
}
void PulseQueue::directionPin(uint8_t pin, bool highCountsUp) {
  if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) return;
  dirPin = pin; directionHigh = highCountsUp;
  gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_INPUT_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(pin), tailForward == highCountsUp);
}
}  // namespace ss2k_motor
