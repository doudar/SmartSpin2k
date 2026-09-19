/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>
#include "ThermalSafety.h"
#include "Stepper.h"
#include "test.h"

namespace {
// No DRV_STATUS or write API: connectivity must not depend on motor state or
// enable the outputs. Emulate the library refreshing CRCerror on each read.
struct IdentityDriver {
  uint32_t response     = 0x21000001;  // TMC2209 identity with ENN high.
  bool responseCrcError = false;
  bool CRCerror         = true;
  int reads             = 0;

  uint32_t IOIN() {
    ++reads;
    CRCerror = responseCrcError;
    return response;
  }
};
}  // namespace

void TestThermalSafety::test_tmc_uart_probe_while_disabled() {
  IdentityDriver driver;
  auto probe = TmcUart::probe(driver);
  TEST_ASSERT_TRUE(probe.valid());
  TEST_ASSERT_EQUAL_HEX32(0x21000001, probe.ioin);
  TEST_ASSERT_EQUAL(1, driver.reads);
  // GPIO inputs can change without changing UART availability.
  driver.response = 0x210003FF;
  TEST_ASSERT_TRUE(TmcUart::probe(driver).valid());
  driver.response = 0x21000000;
  TEST_ASSERT_TRUE(TmcUart::probe(driver).valid());
}

void TestThermalSafety::test_tmc_uart_probe_rejects_invalid_responses() {
  IdentityDriver driver;
  driver.responseCrcError = true;
  TEST_ASSERT_FALSE(TmcUart::probe(driver).valid());
  // A subsequent valid response recovers without recreating the driver.
  driver.responseCrcError = false;
  TEST_ASSERT_TRUE(TmcUart::probe(driver).valid());
  const uint32_t invalidResponses[] = {0, 0xFFFFFFFF, 0x20000001};
  for (uint32_t invalid : invalidResponses) {
    driver.response = invalid;
    TEST_ASSERT_FALSE(TmcUart::probe(driver).valid());
  }
}

void TestThermalSafety::test_tmc_cooldown_and_recovery() {
  ThermalSafety::TmcProtection protection;
  protection.update(true, true, false, 10000);
  TEST_ASSERT_EQUAL(50, protection.percent());
  TEST_ASSERT_FALSE(protection.disabled());
  protection.update(true, true, false, 39999);
  TEST_ASSERT_FALSE(protection.disabled());
  protection.update(true, true, false, 40000);
  TEST_ASSERT_TRUE(protection.disabled());
  protection.update(false, false, false, 50000);
  TEST_ASSERT_TRUE(protection.disabled());
  protection.update(true, false, false, 60000);
  TEST_ASSERT_FALSE(protection.disabled());
  TEST_ASSERT_EQUAL(100, protection.percent());
  // A fresh overheat must receive a fresh 30 second cooldown window.
  protection.update(true, true, false, 100000);
  TEST_ASSERT_FALSE(protection.disabled());
  protection.update(true, false, true, 100001);
  TEST_ASSERT_TRUE(protection.disabled());
}

void TestThermalSafety::test_tmc_missing_samples_and_timer_wrap() {
  ThermalSafety::TmcProtection protection;
  // Lost telemetry alone must never stop a previously cool driver.
  protection.update(true, false, false, 0);
  for (uint32_t now = 10000; now <= 60000; now += 10000) {
    protection.update(false, false, false, now);
    TEST_ASSERT_FALSE(protection.disabled());
    TEST_ASSERT_EQUAL(100, protection.percent());
  }
  const uint32_t start = UINT32_MAX - 5000;
  protection.update(true, true, false, start);
  protection.update(false, false, false, uint32_t(start + 20000));
  TEST_ASSERT_FALSE(protection.disabled());
  TEST_ASSERT_EQUAL(50, protection.percent());
  protection.update(false, false, false, uint32_t(start + 30000));
  TEST_ASSERT_TRUE(protection.disabled());
  protection.update(true, false, false, uint32_t(start + 40000));
  TEST_ASSERT_FALSE(protection.disabled());
}

void TestThermalSafety::test_s3_thresholds_and_hysteresis() {
  ThermalSafety::S3Protection protection;
  TEST_ASSERT_TRUE(protection.disabled());  // Await the first valid sample.
  protection.update(69.9f);
  TEST_ASSERT_FALSE(protection.disabled());
  TEST_ASSERT_FALSE(protection.radiosReduced);
  TEST_ASSERT_EQUAL(100, protection.motorPercent);
  protection.update(70.0f);
  TEST_ASSERT_TRUE(protection.radiosReduced);
  TEST_ASSERT_EQUAL(100, protection.motorPercent);
  protection.update(75.0f);
  TEST_ASSERT_EQUAL(75, protection.motorPercent);
  protection.update(80.0f);
  TEST_ASSERT_EQUAL(50, protection.motorPercent);
  TEST_ASSERT_FALSE(protection.disabled());
  protection.update(80.1f);
  TEST_ASSERT_TRUE(protection.disabled());
  protection.update(79.0f);
  TEST_ASSERT_TRUE(protection.disabled());
  protection.update(78.0f);
  TEST_ASSERT_FALSE(protection.disabled());
  TEST_ASSERT_EQUAL(60, protection.motorPercent);
  protection.update(68.0f);
  TEST_ASSERT_TRUE(protection.radiosReduced);
  protection.update(67.9f);
  TEST_ASSERT_FALSE(protection.radiosReduced);
  TEST_ASSERT_EQUAL(100, protection.motorPercent);
}

void TestThermalSafety::test_s3_failed_sensor_preserves_protection() {
  ThermalSafety::S3Protection protection;
  protection.update(90.0f);
  protection.update(NAN);
  TEST_ASSERT_TRUE(protection.disabled());
  TEST_ASSERT_TRUE(protection.radiosReduced);
  TEST_ASSERT_EQUAL(50, protection.motorPercent);
  protection.update(79.0f);
  TEST_ASSERT_TRUE(protection.disabled());
  protection.update(65.0f);
  TEST_ASSERT_FALSE(protection.disabled());
  protection.update(INFINITY);
  TEST_ASSERT_TRUE(protection.disabled());
}

void TestThermalSafety::test_combined_limits_and_setting_changes() {
  // Limits always use the requested current, never a previously reduced value.
  for (int i = 0; i < 10; ++i) TEST_ASSERT_EQUAL(450, ThermalSafety::limitedCurrent(900, 50, 75));
  TEST_ASSERT_EQUAL(600, ThermalSafety::limitedCurrent(1200, 50, 75));
  TEST_ASSERT_EQUAL(300, ThermalSafety::limitedCurrent(600, 50, 75));
  TEST_ASSERT_EQUAL(675, ThermalSafety::limitedCurrent(900, 100, 75));
  TEST_ASSERT_EQUAL(900, ThermalSafety::limitedCurrent(900, 100, 100));
  TEST_ASSERT_EQUAL(0, ThermalSafety::limitedCurrent(0, 50, 50));
  TEST_ASSERT_EQUAL(0, ThermalSafety::limitedCurrent(-1, 50, 50));
}
