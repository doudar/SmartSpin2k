/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <cstring>

#include <esp_app_format.h>
#include <sdkconfig.h>

enum class FirmwareImageHeaderResult {
  Valid,
  NeedMoreData,
  InvalidMagic,
  InvalidSegmentCount,
  WrongChip,
};

struct FirmwareImageHeaderValidation {
  FirmwareImageHeaderResult result;
  esp_chip_id_t imageChipId;
};

inline FirmwareImageHeaderValidation validateFirmwareImageHeader(const uint8_t* data, size_t length) {
  if (data == nullptr || length < sizeof(esp_image_header_t)) {
    return {FirmwareImageHeaderResult::NeedMoreData, ESP_CHIP_ID_INVALID};
  }

  esp_image_header_t header;
  memcpy(&header, data, sizeof(header));

  if (header.magic != ESP_IMAGE_HEADER_MAGIC) {
    return {FirmwareImageHeaderResult::InvalidMagic, header.chip_id};
  }
  if (header.segment_count == 0 || header.segment_count > ESP_IMAGE_MAX_SEGMENTS) {
    return {FirmwareImageHeaderResult::InvalidSegmentCount, header.chip_id};
  }
  if (header.chip_id != static_cast<esp_chip_id_t>(CONFIG_IDF_FIRMWARE_CHIP_ID)) {
    return {FirmwareImageHeaderResult::WrongChip, header.chip_id};
  }

  return {FirmwareImageHeaderResult::Valid, header.chip_id};
}

inline const char* firmwareImageHeaderResultName(FirmwareImageHeaderResult result) {
  switch (result) {
    case FirmwareImageHeaderResult::Valid:
      return "valid";
    case FirmwareImageHeaderResult::NeedMoreData:
      return "header is incomplete";
    case FirmwareImageHeaderResult::InvalidMagic:
      return "invalid image magic";
    case FirmwareImageHeaderResult::InvalidSegmentCount:
      return "invalid segment count";
    case FirmwareImageHeaderResult::WrongChip:
      return "firmware targets a different chip";
  }
  return "unknown validation error";
}
