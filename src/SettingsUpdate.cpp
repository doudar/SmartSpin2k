/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "SettingsUpdate.h"

#include <algorithm>
#include <cstring>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include "ByteUtils.h"
#include "FirmwareImageValidation.h"
#include "Main.h"
#include "ss2k_secure.h"

namespace {
struct Transfer {
  const esp_partition_t* partition = nullptr;
  esp_ota_handle_t handle          = 0;
  mbedtls_sha256_context hash{};
  uint8_t expected[32]{};
  uint8_t imageHeader[sizeof(esp_image_header_t)]{};
  uint32_t size = 0, offset = 0, activity = 0;
  uint16_t owner = UINT16_MAX;
  bool writing   = false;
} transfer;

void stop() {
  if (transfer.writing) esp_ota_abort(transfer.handle);
  mbedtls_sha256_free(&transfer.hash);
  transfer         = {};
  ss2k->isUpdating = false;
}

bool start(uint16_t peer, const uint8_t* p, size_t n) {
  if (n != 39 || transfer.partition || ss2k->rebootFlag || ss2k->resetDefaultsFlag) return false;
  auto partition     = esp_ota_get_next_update_partition(nullptr);
  uint32_t imageSize = get_le32(p + 3);
  if (!partition || imageSize < sizeof(esp_image_header_t) || imageSize > partition->size) return false;
  transfer.partition = partition;
  transfer.owner     = peer;
  transfer.size      = imageSize;
  std::memcpy(transfer.expected, p + 7, sizeof(transfer.expected));
  mbedtls_sha256_init(&transfer.hash);
  // Sequential erasure spreads flash latency across the stream.
  if (mbedtls_sha256_starts(&transfer.hash, 0) || esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &transfer.handle) != ESP_OK) {
    stop();
    return false;
  }
  transfer.writing = true;
  ss2k->isUpdating = true;
  return true;
}

bool append(const uint8_t* p, size_t n) {
  if (n <= 7 || get_le32(p + 3) != transfer.offset || n - 7 > transfer.size - transfer.offset) return false;
  const uint8_t* data = p + 7;
  size_t count = n - 7, consumed = 0;
  if (transfer.offset < sizeof(transfer.imageHeader)) {
    consumed = std::min<size_t>(count, sizeof(transfer.imageHeader) - transfer.offset);
    std::memcpy(transfer.imageHeader + transfer.offset, data, consumed);
    if (consumed + transfer.offset == sizeof(transfer.imageHeader) &&
        (validateFirmwareImageHeader(transfer.imageHeader, sizeof(transfer.imageHeader)).result != FirmwareImageHeaderResult::Valid ||
         esp_ota_write(transfer.handle, transfer.imageHeader, sizeof(transfer.imageHeader)) != ESP_OK)) {
      stop();
      return false;
    }
  }
  if ((count > consumed && esp_ota_write(transfer.handle, data + consumed, count - consumed) != ESP_OK) || mbedtls_sha256_update(&transfer.hash, data, count)) {
    stop();
    return false;
  }
  transfer.offset += count;
  return true;
}

bool finish() {
  uint8_t digest[32];
  if (transfer.offset != transfer.size || mbedtls_sha256_finish(&transfer.hash, digest) || memcmp(transfer.expected, digest, sizeof(digest))) {
    stop();
    return false;
  }
  transfer.writing = false;  // esp_ota_end consumes the handle, including on failure.
  bool ok          = esp_ota_end(transfer.handle) == ESP_OK && esp_ota_set_boot_partition(transfer.partition) == ESP_OK;
  stop();
  if (ok) ss2k->rebootFlag = true;
  return ok;
}
}  // namespace

bool SettingsUpdate::owns(uint16_t peer) { return transfer.partition && transfer.owner == peer; }

std::vector<uint8_t> SettingsUpdate::command(uint16_t peer, const uint8_t* p, size_t n) {
  uint8_t op      = n >= 3 ? p[2] : 0xff;
  bool ok         = false;
  uint32_t offset = transfer.owner == peer ? transfer.offset : 0;
  if (n >= 3 && p[0] == SS2K_WRITE) {
    if (op == SS2K_OTA_BEGIN)
      ok = start(peer, p, n);
    else if (transfer.partition && transfer.owner == peer) {
      if (op == SS2K_OTA_CHUNK)
        ok = append(p, n);
      else if (n == 3 && op == SS2K_OTA_FINISH)
        ok = finish();
      else if (n == 3 && op == SS2K_OTA_ABORT) {
        stop();
        offset = 0;
        ok     = true;
      }
    }
  }
  if (transfer.partition && transfer.owner == peer) {
    offset = transfer.offset;
    if (ok) transfer.activity = millis();
  }
  std::vector<uint8_t> reply = {static_cast<uint8_t>(ok ? SS2K_SUCCESS : SS2K_ERROR), SS2K_ID_FIRMWARE_STREAM, op, 0, 0, 0, 0};
  put_le32(reply.data() + 3, offset);
  return reply;
}

void SettingsUpdate::disconnect(uint16_t peer) {
  if (transfer.partition && transfer.owner == peer) stop();
}

void SettingsUpdate::tick() {
  if (transfer.partition && uint32_t(millis() - transfer.activity) >= 30000) stop();
}
