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

bool HeartRateData::hasResistance() { return false; }

int HeartRateData::getHeartRate() { return this->heartrate; }

float HeartRateData::getCadence() { return nanf(""); }

int HeartRateData::getPower() { return INT_MIN; }

float HeartRateData::getSpeed() { return nanf(""); }

int HeartRateData::getResistance() { return INT_MIN; }

void HeartRateData::decode(uint8_t *data, size_t length) { this->heartrate = static_cast<int>(data[1]); }
