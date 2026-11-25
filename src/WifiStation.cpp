/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "WifiStation.h"
#include "Main.h"
#include "DirConManager.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
// Arduino 3.3.11 asks for both interfaces in wifiLowLevelInit, even in STA
// mode. ESP-IDF omits the AP factory in station-only builds. Arduino accepts
// an empty AP slot and initializes the station interface independently.
extern "C" esp_netif_t* esp_netif_create_default_wifi_ap() { return nullptr; }
#endif

namespace {
constexpr char TAG[] = "WiFi";
constexpr uint32_t RETRY_MS = 15000;
constexpr uint32_t BACKOFF_MS = 60000;
constexpr uint32_t SERVICES_RETRY_MS = 5000;
constexpr uint8_t ATTEMPTS_PER_BURST = 3;

bool configured = false;
bool mdnsStarted = false;
bool dirconStarted = false;
uint32_t lastAttempt = 0;
uint32_t lastServiceAttempt = 0;
uint8_t attempts = 0;
IPAddress address;

void stopServices() {
  DirConManager::stop();
  dirconStarted = false;
  if (mdnsStarted) MDNS.end();
  mdnsStarted = false;
}
}  // namespace

void startWifi() {
  // LittleFS owns the station credentials. Avoid a second persistent copy.
  WiFi.persistent(false);
  const char* ssid = userConfig->getSsid();
  configured = ssid[0] != '\0' && strcmp(ssid, DEVICE_NAME) != 0;
  if (!configured) {
    WiFi.mode(WIFI_OFF);
    return;
  }
  WiFi.setHostname(userConfig->getDeviceName());
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  WiFi.begin(ssid, userConfig->getPassword());
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  lastAttempt = millis();
  attempts = 1;
}

bool resetWifiSettings() {
  // Older firmware stored credentials in the IDF WiFi namespace as well as
  // LittleFS. The driver must be initialized even on an unconfigured unit.
  return WiFi.mode(WIFI_STA) && esp_wifi_set_storage(WIFI_STORAGE_FLASH) == ESP_OK && esp_wifi_restore() == ESP_OK;
}

void updateWifi() {
  if (!configured) return;
  const uint32_t now = millis();
  const IPAddress current = WiFi.status() == WL_CONNECTED ? WiFi.localIP() : IPAddress();
  const bool changed = current != address;
  if (changed) {
    stopServices();
    address = current;
    refreshBLEAdvertisementIp();
    attempts = 0;
    lastAttempt = now - RETRY_MS;
    if (current != IPAddress()) {
      SS2K_LOG(TAG, "Station connected at %s", current.toString().c_str());
      configTime(0, 0, "pool.ntp.org");
    }
  }
  if (current == IPAddress()) {
    if (now - lastAttempt >= (attempts >= ATTEMPTS_PER_BURST ? BACKOFF_MS : RETRY_MS)) {
      if (attempts >= ATTEMPTS_PER_BURST) attempts = 0;
      ++attempts;
      lastAttempt = now;
      WiFi.reconnect();
    }
    return;
  }
  if (!dirconStarted && (changed || now - lastServiceAttempt >= SERVICES_RETRY_MS)) {
    lastServiceAttempt = now;
    if (!mdnsStarted) mdnsStarted = MDNS.begin(userConfig->getDeviceName());
    if (mdnsStarted) dirconStarted = DirConManager::start();
  }
}
