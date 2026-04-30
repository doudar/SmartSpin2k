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

extern HardwareSerial stepperSerial;
extern TMC2209Stepper driver;
extern FastAccelStepperEngine engine;
extern FastAccelStepper* stepper;
