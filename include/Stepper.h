/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <stdint.h>

namespace TmcUart {
struct Probe {
  uint32_t ioin;
  bool crcError;

  // TMC2209 IOIN.VERSION is 0x21, regardless of ENN or temperature.
  bool valid() const { return !crcError && (ioin >> 24) == 0x21; }
};

template <typename Driver>
Probe probe(Driver& driver) {
  uint32_t ioin = driver.IOIN();
  return {ioin, driver.CRCerror};
}
}  // namespace TmcUart

// Native tests exercise the connection check without ESP32 peripherals.
#ifndef PLATFORMIO_ENV_NATIVE
#include <Arduino.h>
#include <HardwareSerial.h>
#include <TMCStepper.h>
#include "FastAccelStepper.h"

constexpr int LOG_INTERVAL = 1000;

struct HomingSgBaseline {
  int threshold;
  int sensitivity;
};

extern HardwareSerial stepperSerial;
void initializeStepperSerial(bool restart = false);
extern FastAccelStepperEngine engine;
extern FastAccelStepper* stepper;
#endif
