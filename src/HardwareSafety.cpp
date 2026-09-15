/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "ThermalSafety.h"

#if defined(SMARTSPIN2K_S3)
#include "driver/temperature_sensor.h"
#include "esp_wifi.h"
#include "esp_bt.h"

namespace {
ThermalSafety::S3Protection s3Protection;
temperature_sensor_handle_t temperatureSensor = nullptr;

float readS3Temperature() {
  if (!temperatureSensor) {
    // Cover both thermal thresholds; Arduino temperatureRead() requests 10-50 C.
    temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
    esp_err_t result                   = temperature_sensor_install(&config, &temperatureSensor);
    if (result == ESP_OK) result = temperature_sensor_enable(temperatureSensor);
    if (result != ESP_OK) {
      SS2K_LOGE(MAIN_LOG_TAG, "S3 temperature sensor setup failed: %s; motor inhibited, retry in 10s", esp_err_to_name(result));
      if (temperatureSensor) temperature_sensor_uninstall(temperatureSensor);
      temperatureSensor = nullptr;
      return NAN;
    }
  }
  float temperature = NAN;
  esp_err_t result  = temperature_sensor_get_celsius(temperatureSensor, &temperature);
  if (result != ESP_OK) {
    SS2K_LOGE(MAIN_LOG_TAG, "S3 temperature read failed: %s; motor inhibited, retry in 10s", esp_err_to_name(result));
    return NAN;
  }
  return temperature;
}

void updateRadioCooling(bool reduce) {
  constexpr int8_t WIFI_COOLING_POWER = 34;  // 8.5 dBm, in quarter-dBm units.
  static int8_t savedWifiPower        = 0;
  static bool wifiPowerSaved          = false;
  static wifi_ps_type_t savedSleep    = WIFI_PS_NONE;
  static bool wifiSleepSaved          = false;
  int8_t power;
  wifi_ps_type_t sleep;
  if (reduce) {
    if (esp_wifi_get_max_tx_power(&power) == ESP_OK) {
      if (!wifiPowerSaved) {
        savedWifiPower = power;
        wifiPowerSaved = true;
      }
      if (power > WIFI_COOLING_POWER && esp_wifi_set_max_tx_power(WIFI_COOLING_POWER) != ESP_OK)
        SS2K_LOGE(MAIN_LOG_TAG, "S3 cooling: WiFi TX power reduction failed; retry in 10s");
    }
    if (esp_wifi_get_ps(&sleep) == ESP_OK) {
      if (!wifiSleepSaved) {
        savedSleep     = sleep;
        wifiSleepSaved = true;
      }
      if (sleep == WIFI_PS_NONE && esp_wifi_set_ps(WIFI_PS_MIN_MODEM) != ESP_OK) SS2K_LOGE(MAIN_LOG_TAG, "S3 cooling: WiFi modem sleep failed; retry in 10s");
    }
  } else {
    if (wifiPowerSaved && esp_wifi_set_max_tx_power(savedWifiPower) == ESP_OK) wifiPowerSaved = false;
    if (wifiSleepSaved && esp_wifi_set_ps(savedSleep) == ESP_OK) wifiSleepSaved = false;
    if (wifiPowerSaved || wifiSleepSaved) SS2K_LOGE(MAIN_LOG_TAG, "S3 cooling: WiFi restore pending; retry in 10s");
  }

  // NimBLEDevice::setPower(All) only changes ADV/SCAN/DEFAULT, so include
  // established links explicitly. Polling also catches new connections while hot.
  static esp_power_level_t savedBlePower[ESP_BLE_PWR_TYPE_NUM];
  static bool blePowerSaved[ESP_BLE_PWR_TYPE_NUM] = {};
  static esp_power_level_t normalBleDefault       = ESP_PWR_LVL_INVALID;
  if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) return;
  if (reduce && normalBleDefault == ESP_PWR_LVL_INVALID) normalBleDefault = esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_DEFAULT);
  for (int i = 0; i < ESP_BLE_PWR_TYPE_NUM; ++i) {
    auto type = static_cast<esp_ble_power_type_t>(i);
    if (i <= ESP_BLE_PWR_TYPE_CONN_HDL8) {
      ble_gap_conn_desc connection;
      if (ble_gap_conn_find(i, &connection) != 0) {
        blePowerSaved[i] = false;
        continue;
      }
    }
    if (reduce) {
      esp_power_level_t current = esp_ble_tx_power_get(type);
      if (current == ESP_PWR_LVL_INVALID) continue;
      if (!blePowerSaved[i]) {
        // A connection created during cooling inherits the reduced default.
        // Restore such links to the original default once the chip cools.
        savedBlePower[i] = (i <= ESP_BLE_PWR_TYPE_CONN_HDL8 && current == ESP_PWR_LVL_N24 && normalBleDefault != ESP_PWR_LVL_INVALID) ? normalBleDefault : current;
        blePowerSaved[i] = true;
      }
      if (current > ESP_PWR_LVL_N24 && esp_ble_tx_power_set(type, ESP_PWR_LVL_N24) != ESP_OK)
        SS2K_LOGE(MAIN_LOG_TAG, "S3 cooling: BLE TX power reduction failed for type %d; retry in 10s", i);
    } else if (blePowerSaved[i]) {
      if (esp_ble_tx_power_set(type, savedBlePower[i]) == ESP_OK)
        blePowerSaved[i] = false;
      else
        SS2K_LOGE(MAIN_LOG_TAG, "S3 cooling: BLE TX power restore failed for type %d; retry in 10s", i);
    }
  }
  if (!reduce) {
    bool restorePending = false;
    for (bool saved : blePowerSaved) restorePending |= saved;
    if (!restorePending) normalBleDefault = ESP_PWR_LVL_INVALID;
  }
}
}  // namespace
#endif

void SS2K::updateHardwareSafety() {
#if defined(SMARTSPIN2K_S3)
  const auto previous = s3Protection;
  float temperature   = readS3Temperature();
  s3Protection.update(temperature);
  if (s3Protection.sensorValid) SS2K_LOG(MAIN_LOG_TAG, "T=%dC", static_cast<int>(lroundf(temperature)));
  if (s3Protection.radiosReduced != previous.radiosReduced) {
    SS2K_LOG(MAIN_LOG_TAG, "S3 cooling: %s",
             s3Protection.radiosReduced ? "70C reached; limiting WiFi to 8.5 dBm with modem sleep and BLE to minimum TX power (-24 dBm), BLE remains on"
                                        : "below 68C; restoring WiFi and BLE power settings");
  }
  if (s3Protection.motorPercent != previous.motorPercent) SS2K_LOG(MAIN_LOG_TAG, "S3 cooling: motor current limit %d%% at %.1fC", s3Protection.motorPercent, temperature);
  if (s3Protection.motorStopped != previous.motorStopped)
    SS2K_LOG(MAIN_LOG_TAG, "S3 cooling: %s",
             s3Protection.motorStopped ? "above 80C; stopping motor and holding EN high until at or below 78C"
                                       : "at or below 78C; temperature permits motor operation with current limit");
  if (s3Protection.sensorValid && !previous.sensorValid) SS2K_LOG(MAIN_LOG_TAG, "S3 temperature monitoring available");
  // Apply the motor interlock before radio calls, which may fail independently.
  updateDriverSafety(s3Protection.motorPercent, s3Protection.disabled());
  updateRadioCooling(s3Protection.radiosReduced);
#else
  updateDriverSafety(100, false);
#endif
}
