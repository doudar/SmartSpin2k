/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "sensors/HeartRateData.h"

bool HeartRateData::hasHeartRate() { return this->heartrate != INT_MIN; }

bool HeartRateData::hasCadence() { return false; }

bool HeartRateData::hasPower() { return false; }

bool HeartRateData::hasSpeed() { return false; }

int HeartRateData::getHeartRate() { return this->heartrate; }

float HeartRateData::getCadence() { return NAN; }

int HeartRateData::getPower() { return INT_MIN; }

float HeartRateData::getSpeed() { return NAN; }

void HeartRateData::decode(uint8_t *data, size_t length) { this->heartrate = static_cast<int>(data[1]); }
