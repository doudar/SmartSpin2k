/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

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
