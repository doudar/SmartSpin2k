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

#define HOME_TIMEOUT                  30000
#define HOMING_SG_SAMPLE_COUNT        24
#define HOMING_SG_MIN_SAMPLE_MARGIN   10
#define HOMING_SG_MAX_THRESHOLD_DRIFT 30
#define HOMING_TAP_MAX_ATTEMPTS       7
#define HOMING_TAP_REQUIRED_STABLE    3
#define HOMING_TAP_TOLERANCE          150
#define HOMING_RECOVERY_BACKOFF_MULT  3
#define HOMING_MAX_SENSITIVITY        100

struct HomingSgBaseline {
  int threshold;
  int sensitivity;
};

extern HardwareSerial stepperSerial;
extern TMC2209Stepper driver;
extern FastAccelStepperEngine engine;
extern FastAccelStepper* stepper;
