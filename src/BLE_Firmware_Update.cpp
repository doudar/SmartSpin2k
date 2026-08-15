/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "BLE_Firmware_Update.h"

#include <algorithm>
#include <cstring>

#include <Arduino.h>
#include <Constants.h>
#include <NimBLEDevice.h>
#include <esp_err.h>
#include <esp_ota_ops.h>
#include <esp_rom_crc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "FirmwareImageValidation.h"
#include "Main.h"
#include "SS2KLog.h"

#define BLE_OTA_LOG_TAG "BLE_OTA"

namespace {

constexpr uint32_t PROGRESS_NOTIFICATION_INTERVAL = 64U * 1024U;
constexpr uint32_t PROGRESS_LOG_INTERVAL          = 512U * 1024U;
constexpr uint32_t OTA_PREPARE_DELAY_MS           = 300;

NimBLECharacteristic* statusCharacteristic = nullptr;

struct TransferContext {
  esp_ota_handle_t otaHandle             = 0;
  const esp_partition_t* updatePartition = nullptr;
  uint16_t connectionHandle              = BLE_HS_CONN_HANDLE_NONE;
  uint32_t imageSize                     = 0;
  uint32_t expectedCrc32                 = 0;
  uint32_t receivedBytes                 = 0;
  uint32_t runningCrc32                  = 0;
  uint32_t nextProgressNotification      = PROGRESS_NOTIFICATION_INTERVAL;
  uint32_t nextProgressLog               = PROGRESS_LOG_INTERVAL;
  uint32_t startedMs                     = 0;
  uint32_t lastActivityMs                = 0;
  size_t headerBytes                     = 0;
  uint8_t header[sizeof(esp_image_header_t)]{};
  BleFirmwareUpdate::State state = BleFirmwareUpdate::State::Waiting;
  BleFirmwareUpdate::Error error = BleFirmwareUpdate::Error::None;
  bool active                   = false;
  bool otaBegun                 = false;
  bool headerValidated          = false;
};

TransferContext transfer;
StaticSemaphore_t transferMutexStorage;
SemaphoreHandle_t transferMutex = nullptr;

const char* stateName(BleFirmwareUpdate::State state) {
  switch (state) {
    case BleFirmwareUpdate::State::Waiting:
      return "Waiting";
    case BleFirmwareUpdate::State::Preparing:
      return "Preparing";
    case BleFirmwareUpdate::State::Updating:
      return "Updating";
    case BleFirmwareUpdate::State::Flashing:
      return "Flashing";
    case BleFirmwareUpdate::State::Verifying:
      return "Verifying";
    case BleFirmwareUpdate::State::Rebooting:
      return "Rebooting";
    case BleFirmwareUpdate::State::Error:
      return "Error";
  }
  return "Unknown";
}

const char* errorName(BleFirmwareUpdate::Error error) {
  switch (error) {
    case BleFirmwareUpdate::Error::None:
      return "None";
    case BleFirmwareUpdate::Error::InvalidCommand:
      return "InvalidCommand";
    case BleFirmwareUpdate::Error::UnsupportedVersion:
      return "UnsupportedVersion";
    case BleFirmwareUpdate::Error::InvalidStartPacket:
      return "InvalidStartPacket";
    case BleFirmwareUpdate::Error::Busy:
      return "Busy";
    case BleFirmwareUpdate::Error::InvalidImageSize:
      return "InvalidImageSize";
    case BleFirmwareUpdate::Error::NoUpdatePartition:
      return "NoUpdatePartition";
    case BleFirmwareUpdate::Error::WrongConnection:
      return "WrongConnection";
    case BleFirmwareUpdate::Error::NotStarted:
      return "NotStarted";
    case BleFirmwareUpdate::Error::EmptyData:
      return "EmptyData";
    case BleFirmwareUpdate::Error::TooMuchData:
      return "TooMuchData";
    case BleFirmwareUpdate::Error::InvalidImageHeader:
      return "InvalidImageHeader";
    case BleFirmwareUpdate::Error::OtaBeginFailed:
      return "OtaBeginFailed";
    case BleFirmwareUpdate::Error::OtaWriteFailed:
      return "OtaWriteFailed";
    case BleFirmwareUpdate::Error::IncompleteImage:
      return "IncompleteImage";
    case BleFirmwareUpdate::Error::ChecksumMismatch:
      return "ChecksumMismatch";
    case BleFirmwareUpdate::Error::ImageVerifyFailed:
      return "ImageVerifyFailed";
    case BleFirmwareUpdate::Error::SetBootFailed:
      return "SetBootFailed";
    case BleFirmwareUpdate::Error::TransferTimedOut:
      return "TransferTimedOut";
    case BleFirmwareUpdate::Error::Aborted:
      return "Aborted";
    case BleFirmwareUpdate::Error::ConnectionLost:
      return "ConnectionLost";
  }
  return "Unknown";
}

class TransferGuard {
 public:
  explicit TransferGuard(TickType_t waitTicks) : locked(transferMutex != nullptr && xSemaphoreTake(transferMutex, waitTicks) == pdTRUE) {}
  ~TransferGuard() {
    if (locked) xSemaphoreGive(transferMutex);
  }
  explicit operator bool() const { return locked; }

 private:
  bool locked;
};

void publishStatus(uint16_t connectionHandle = BLE_HS_CONN_HANDLE_NONE, bool notify = true) {
  const auto packet = BleFirmwareUpdate::makeStatusPacket(transfer.state, transfer.error, transfer.receivedBytes, transfer.imageSize);
  statusCharacteristic->setValue(packet.data(), packet.size());
  if (notify && connectionHandle != BLE_HS_CONN_HANDLE_NONE) {
    statusCharacteristic->notify(packet.data(), packet.size(), connectionHandle);
  }
}

void publishTransientError(uint16_t connectionHandle, BleFirmwareUpdate::Error error) {
  SS2K_LOGW(BLE_OTA_LOG_TAG, "Rejected OTA request: conn=%u state=%s error=%s(%u) bytes=%lu/%lu", connectionHandle, stateName(transfer.state), errorName(error),
            static_cast<unsigned>(error), static_cast<unsigned long>(transfer.receivedBytes), static_cast<unsigned long>(transfer.imageSize));
  const auto packet = BleFirmwareUpdate::makeStatusPacket(BleFirmwareUpdate::State::Error, error, transfer.receivedBytes, transfer.imageSize);
  statusCharacteristic->notify(packet.data(), packet.size(), connectionHandle);
  // A control write temporarily replaces the characteristic's readable value
  // with the command bytes. Restore the persistent transfer status after
  // sending this connection-specific error.
  publishStatus(BLE_HS_CONN_HANDLE_NONE, false);
}

void abortOtaHandle() {
  if (transfer.otaBegun) {
    esp_ota_abort(transfer.otaHandle);
    transfer.otaBegun  = false;
    transfer.otaHandle = 0;
  }
}

void resetTransfer(BleFirmwareUpdate::State state = BleFirmwareUpdate::State::Waiting) {
  abortOtaHandle();
  transfer       = TransferContext{};
  transfer.state = state;
  if (ss2k != nullptr) ss2k->isUpdating = false;
}

void failTransfer(BleFirmwareUpdate::Error error, const char* message, esp_err_t espError = ESP_OK, bool notify = true) {
  const BleFirmwareUpdate::State failedState = transfer.state;
  const uint32_t elapsedMs                    = transfer.startedMs == 0 ? 0 : millis() - transfer.startedMs;
  if (espError == ESP_OK) {
    SS2K_LOGE(BLE_OTA_LOG_TAG, "FAILED: conn=%u state=%s error=%s(%u) bytes=%lu/%lu elapsed=%lums detail=%s", transfer.connectionHandle, stateName(failedState),
              errorName(error), static_cast<unsigned>(error), static_cast<unsigned long>(transfer.receivedBytes), static_cast<unsigned long>(transfer.imageSize),
              static_cast<unsigned long>(elapsedMs), message);
  } else {
    SS2K_LOGE(BLE_OTA_LOG_TAG, "FAILED: conn=%u state=%s error=%s(%u) bytes=%lu/%lu elapsed=%lums detail=%s esp=%s(%d)", transfer.connectionHandle,
              stateName(failedState), errorName(error), static_cast<unsigned>(error), static_cast<unsigned long>(transfer.receivedBytes),
              static_cast<unsigned long>(transfer.imageSize), static_cast<unsigned long>(elapsedMs), message, esp_err_to_name(espError), espError);
  }

  abortOtaHandle();
  transfer.active = false;
  transfer.state  = BleFirmwareUpdate::State::Error;
  transfer.error  = error;
  if (ss2k != nullptr) ss2k->isUpdating = false;
  publishStatus(notify ? transfer.connectionHandle : BLE_HS_CONN_HANDLE_NONE, notify);
  if (ss2k != nullptr) ss2k->rebootFlag = true;
}

bool ownsTransfer(uint16_t connectionHandle) {
  return transfer.active && transfer.connectionHandle == connectionHandle;
}

bool validateAndWriteHeader() {
  const FirmwareImageHeaderValidation validation = validateFirmwareImageHeader(transfer.header, transfer.headerBytes);
  if (validation.result != FirmwareImageHeaderResult::Valid) {
    failTransfer(BleFirmwareUpdate::Error::InvalidImageHeader, firmwareImageHeaderResultName(validation.result));
    return false;
  }

  SS2K_LOG(BLE_OTA_LOG_TAG, "Image header valid: conn=%u chip=0x%04x expected=0x%04x header=%u bytes", transfer.connectionHandle,
           static_cast<unsigned>(validation.imageChipId), static_cast<unsigned>(CONFIG_IDF_FIRMWARE_CHIP_ID), static_cast<unsigned>(transfer.headerBytes));
  const esp_err_t writeResult = esp_ota_write(transfer.otaHandle, transfer.header, transfer.headerBytes);
  if (writeResult != ESP_OK) {
    failTransfer(BleFirmwareUpdate::Error::OtaWriteFailed, "Unable to write firmware image header", writeResult);
    return false;
  }
  transfer.headerValidated = true;
  return true;
}

bool prepareOtaPartition() {
  const uint32_t prepareStartedMs = millis();
  SS2K_LOG(BLE_OTA_LOG_TAG, "Preparing flash: conn=%u partition=%s size=%lu", transfer.connectionHandle, transfer.updatePartition->label,
           static_cast<unsigned long>(transfer.imageSize));
  const esp_err_t result = esp_ota_begin(transfer.updatePartition, transfer.imageSize, &transfer.otaHandle);
  if (result != ESP_OK) {
    failTransfer(BleFirmwareUpdate::Error::OtaBeginFailed, "Unable to begin BLE firmware update", result);
    return false;
  }

  transfer.otaBegun       = true;
  transfer.state          = BleFirmwareUpdate::State::Updating;
  transfer.error          = BleFirmwareUpdate::Error::None;
  transfer.lastActivityMs = millis();
  SS2K_LOG(BLE_OTA_LOG_TAG, "Flash ready: conn=%u partition=%s prepare=%lums; accepting firmware data", transfer.connectionHandle, transfer.updatePartition->label,
           static_cast<unsigned long>(millis() - prepareStartedMs));
  // OTA normally pauses log draining. Flush preparation diagnostics while the
  // app is still waiting for Updating, before firmware data starts competing
  // for BLE airtime.
  logHandler.writeLogs();
  publishStatus(transfer.connectionHandle);
  return true;
}

bool writeFirmwareBytes(const uint8_t* data, size_t length) {
  if (length == 0) return true;
  const esp_err_t result = esp_ota_write(transfer.otaHandle, data, length);
  if (result != ESP_OK) {
    failTransfer(BleFirmwareUpdate::Error::OtaWriteFailed, "Unable to write firmware data", result);
    return false;
  }
  return true;
}

void startTransfer(const uint8_t* data, size_t length, NimBLEConnInfo& connInfo) {
  BleFirmwareUpdate::StartRequest request{};
  if (length != BleFirmwareUpdate::START_PACKET_SIZE) {
    publishTransientError(connInfo.getConnHandle(), BleFirmwareUpdate::Error::InvalidStartPacket);
    return;
  }
  if (data[1] != BleFirmwareUpdate::PROTOCOL_VERSION) {
    publishTransientError(connInfo.getConnHandle(), BleFirmwareUpdate::Error::UnsupportedVersion);
    return;
  }
  if (!BleFirmwareUpdate::parseStartRequest(data, length, request)) {
    publishTransientError(connInfo.getConnHandle(), BleFirmwareUpdate::Error::InvalidStartPacket);
    return;
  }
  if (transfer.active || (ss2k != nullptr && ss2k->rebootFlag)) {
    publishTransientError(connInfo.getConnHandle(), BleFirmwareUpdate::Error::Busy);
    return;
  }

  resetTransfer();
  transfer.updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (transfer.updatePartition == nullptr) {
    transfer.connectionHandle = connInfo.getConnHandle();
    failTransfer(BleFirmwareUpdate::Error::NoUpdatePartition, "No OTA update partition is available");
    return;
  }
  if (request.imageSize < sizeof(esp_image_header_t) || request.imageSize > transfer.updatePartition->size) {
    transfer.connectionHandle = connInfo.getConnHandle();
    transfer.imageSize        = request.imageSize;
    failTransfer(BleFirmwareUpdate::Error::InvalidImageSize, "Firmware image size does not fit the OTA partition");
    return;
  }

  transfer.connectionHandle = connInfo.getConnHandle();
  transfer.imageSize        = request.imageSize;
  transfer.expectedCrc32    = request.imageCrc32;
  transfer.active           = true;
  transfer.state            = BleFirmwareUpdate::State::Preparing;
  transfer.error            = BleFirmwareUpdate::Error::None;
  transfer.startedMs        = millis();
  transfer.lastActivityMs   = millis();
  if (ss2k != nullptr) ss2k->isUpdating = true;

  // Retry the best-effort exchange here in case the request made from the
  // connection callback was too early for the peer. EALREADY is harmless.
  BLERequestMtuExchange(connInfo.getConnHandle());
  NimBLEDevice::getServer()->updateConnParams(connInfo.getConnHandle(), 12, 12, 0, 1000);
  SS2K_LOG(BLE_OTA_LOG_TAG, "START: conn=%u peer=%s mtu=%u size=%lu crc32=0x%08lx partition=%s", transfer.connectionHandle,
           connInfo.getAddress().toString().c_str(), connInfo.getMTU(), static_cast<unsigned long>(transfer.imageSize), static_cast<unsigned long>(transfer.expectedCrc32),
           transfer.updatePartition->label);
  publishStatus(transfer.connectionHandle);
}

void finishTransfer(uint16_t connectionHandle) {
  if (!ownsTransfer(connectionHandle)) {
    publishTransientError(connectionHandle, transfer.active ? BleFirmwareUpdate::Error::WrongConnection : BleFirmwareUpdate::Error::NotStarted);
    return;
  }
  SS2K_LOG(BLE_OTA_LOG_TAG, "FINISH: conn=%u bytes=%lu/%lu crc32=0x%08lx expected=0x%08lx", connectionHandle, static_cast<unsigned long>(transfer.receivedBytes),
           static_cast<unsigned long>(transfer.imageSize), static_cast<unsigned long>(transfer.runningCrc32), static_cast<unsigned long>(transfer.expectedCrc32));
  if (!transfer.otaBegun || transfer.receivedBytes != transfer.imageSize) {
    failTransfer(BleFirmwareUpdate::Error::IncompleteImage, "Firmware transfer ended before the declared image size was received");
    return;
  }
  if (transfer.runningCrc32 != transfer.expectedCrc32) {
    failTransfer(BleFirmwareUpdate::Error::ChecksumMismatch, "Firmware CRC-32 does not match the declared checksum");
    return;
  }

  transfer.state = BleFirmwareUpdate::State::Verifying;
  SS2K_LOG(BLE_OTA_LOG_TAG, "Verifying image: conn=%u partition=%s", connectionHandle, transfer.updatePartition->label);
  publishStatus(transfer.connectionHandle);

  const esp_ota_handle_t completedHandle = transfer.otaHandle;
  transfer.otaBegun                      = false;
  transfer.otaHandle                     = 0;
  esp_err_t result                       = esp_ota_end(completedHandle);
  if (result != ESP_OK) {
    failTransfer(BleFirmwareUpdate::Error::ImageVerifyFailed, "Firmware image verification failed", result);
    return;
  }

  result = esp_ota_set_boot_partition(transfer.updatePartition);
  if (result != ESP_OK) {
    failTransfer(BleFirmwareUpdate::Error::SetBootFailed, "Unable to select the new firmware boot partition", result);
    return;
  }

  transfer.active = false;
  transfer.state  = BleFirmwareUpdate::State::Rebooting;
  transfer.error  = BleFirmwareUpdate::Error::None;
  publishStatus(transfer.connectionHandle);
  SS2K_LOG(BLE_OTA_LOG_TAG, "SUCCESS: conn=%u bytes=%lu elapsed=%lums; boot partition=%s, rebooting", connectionHandle,
           static_cast<unsigned long>(transfer.receivedBytes), static_cast<unsigned long>(millis() - transfer.startedMs), transfer.updatePartition->label);
  if (ss2k != nullptr) ss2k->rebootFlag = true;
}

class ControlCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
    TransferGuard guard(portMAX_DELAY);
    if (!guard) return;

    const NimBLEAttValue value = characteristic->getValue();
    if (value.size() == 0) {
      publishTransientError(connInfo.getConnHandle(), BleFirmwareUpdate::Error::InvalidCommand);
      return;
    }

    const uint8_t* data = value.data();
    switch (static_cast<BleFirmwareUpdate::Command>(data[0])) {
      case BleFirmwareUpdate::Command::Start:
        startTransfer(data, value.size(), connInfo);
        break;
      case BleFirmwareUpdate::Command::Finish:
        if (value.size() != 1) {
          publishTransientError(connInfo.getConnHandle(), BleFirmwareUpdate::Error::InvalidCommand);
        } else {
          finishTransfer(connInfo.getConnHandle());
        }
        break;
      case BleFirmwareUpdate::Command::Abort:
        if (value.size() != 1) {
          publishTransientError(connInfo.getConnHandle(), BleFirmwareUpdate::Error::InvalidCommand);
        } else if (ownsTransfer(connInfo.getConnHandle())) {
          failTransfer(BleFirmwareUpdate::Error::Aborted, "BLE firmware update aborted by client");
        } else {
          publishTransientError(connInfo.getConnHandle(), transfer.active ? BleFirmwareUpdate::Error::WrongConnection : BleFirmwareUpdate::Error::NotStarted);
        }
        break;
      case BleFirmwareUpdate::Command::Query:
        if (value.size() != 1) {
          publishTransientError(connInfo.getConnHandle(), BleFirmwareUpdate::Error::InvalidCommand);
        } else {
          publishStatus(connInfo.getConnHandle());
        }
        break;
      default:
        publishTransientError(connInfo.getConnHandle(), BleFirmwareUpdate::Error::InvalidCommand);
        break;
    }
  }
};

class DataCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
    TransferGuard guard(portMAX_DELAY);
    if (!guard) return;

    const NimBLEAttValue value = characteristic->getValue();
    if (!ownsTransfer(connInfo.getConnHandle())) {
      publishTransientError(connInfo.getConnHandle(), transfer.active ? BleFirmwareUpdate::Error::WrongConnection : BleFirmwareUpdate::Error::NotStarted);
      return;
    }
    if (!transfer.otaBegun) {
      publishStatus(connInfo.getConnHandle());
      return;
    }
    const size_t valueSize = value.size();
    if (valueSize == 0) {
      failTransfer(BleFirmwareUpdate::Error::EmptyData, "Received an empty firmware data packet");
      return;
    }
    if (valueSize > transfer.imageSize - transfer.receivedBytes) {
      failTransfer(BleFirmwareUpdate::Error::TooMuchData, "Received more firmware data than declared by START");
      return;
    }

    transfer.lastActivityMs     = millis();
    const bool firstDataPacket = transfer.receivedBytes == 0;
    const uint8_t* data         = value.data();
    transfer.runningCrc32       = esp_rom_crc32_le(transfer.runningCrc32, data, valueSize);
    transfer.receivedBytes += valueSize;
    if (firstDataPacket) {
      SS2K_LOG(BLE_OTA_LOG_TAG, "First data: conn=%u mtu=%u chunk=%u", transfer.connectionHandle, connInfo.getMTU(), static_cast<unsigned>(valueSize));
    }

    size_t offset = 0;
    if (!transfer.headerValidated) {
      const size_t headerRemaining = sizeof(transfer.header) - transfer.headerBytes;
      const size_t headerToCopy    = std::min(headerRemaining, valueSize);
      memcpy(transfer.header + transfer.headerBytes, data, headerToCopy);
      transfer.headerBytes += headerToCopy;
      offset = headerToCopy;

      if (transfer.headerBytes < sizeof(transfer.header)) return;
      if (!validateAndWriteHeader()) return;
    }

    if (!writeFirmwareBytes(data + offset, valueSize - offset)) return;

    if (transfer.state != BleFirmwareUpdate::State::Flashing) {
      transfer.state = BleFirmwareUpdate::State::Flashing;
      SS2K_LOG(BLE_OTA_LOG_TAG, "Flashing: conn=%u bytes=%lu/%lu", transfer.connectionHandle, static_cast<unsigned long>(transfer.receivedBytes),
               static_cast<unsigned long>(transfer.imageSize));
      publishStatus(transfer.connectionHandle);
    } else if (transfer.receivedBytes >= transfer.nextProgressNotification || transfer.receivedBytes == transfer.imageSize) {
      while (transfer.nextProgressNotification <= transfer.receivedBytes) {
        transfer.nextProgressNotification += PROGRESS_NOTIFICATION_INTERVAL;
      }
      publishStatus(transfer.connectionHandle);
    }

    if (transfer.receivedBytes >= transfer.nextProgressLog || transfer.receivedBytes == transfer.imageSize) {
      const uint32_t elapsedMs = std::max<uint32_t>(1, millis() - transfer.startedMs);
      const uint32_t percent   = transfer.imageSize == 0 ? 0 : static_cast<uint32_t>((static_cast<uint64_t>(transfer.receivedBytes) * 100U) / transfer.imageSize);
      const uint32_t rateKiBs  = static_cast<uint32_t>((static_cast<uint64_t>(transfer.receivedBytes) * 1000U) / elapsedMs / 1024U);
      SS2K_LOG(BLE_OTA_LOG_TAG, "Progress: conn=%u bytes=%lu/%lu (%lu%%) rate=%lu KiB/s", transfer.connectionHandle,
               static_cast<unsigned long>(transfer.receivedBytes), static_cast<unsigned long>(transfer.imageSize), static_cast<unsigned long>(percent),
               static_cast<unsigned long>(rateKiBs));
      while (transfer.nextProgressLog <= transfer.receivedBytes) {
        transfer.nextProgressLog += PROGRESS_LOG_INTERVAL;
      }
    }
  }
};

ControlCallback controlCallback;
DataCallback dataCallback;

}  // namespace

void BLEFirmwareSetup(NimBLEServer* server) {
  transferMutex = xSemaphoreCreateMutexStatic(&transferMutexStorage);
  if (transferMutex == nullptr) {
    SS2K_LOGE(BLE_OTA_LOG_TAG, "Unable to create BLE firmware update mutex");
    return;
  }

  NimBLEService* service = server->createService(FIRMWARE_SERVICE_UUID);

  statusCharacteristic = service->createCharacteristic(
      FIRMWARE_CHARACTERISTIC_TX_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY, BleFirmwareUpdate::STATUS_PACKET_SIZE);
  statusCharacteristic->setCallbacks(&controlCallback);

  NimBLECharacteristic* dataCharacteristic =
      service->createCharacteristic(FIRMWARE_CHARACTERISTIC_OTA_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR, BleFirmwareUpdate::MAX_DATA_CHUNK_SIZE);
  dataCharacteristic->setCallbacks(&dataCallback);

  resetTransfer();
  publishStatus(BLE_HS_CONN_HANDLE_NONE, false);
}

void BLEFirmwareUpdateLoop() {
  TransferGuard guard(0);
  if (!guard || !transfer.active) return;
  if (!transfer.otaBegun) {
    if (millis() - transfer.lastActivityMs < OTA_PREPARE_DELAY_MS) return;
    prepareOtaPartition();
    return;
  }
  if (!BleFirmwareUpdate::hasTransferTimedOut(millis(), transfer.lastActivityMs)) return;

  failTransfer(BleFirmwareUpdate::Error::TransferTimedOut, "BLE firmware update timed out waiting for data");
}

void BLEFirmwareUpdateOnDisconnect(uint16_t connectionHandle) {
  TransferGuard guard(portMAX_DELAY);
  if (!guard) return;
  if (transfer.connectionHandle != connectionHandle && transfer.state == BleFirmwareUpdate::State::Waiting) return;
  SS2K_LOGW(BLE_OTA_LOG_TAG, "Disconnect: conn=%u owner=%u match=%u active=%u state=%s error=%s(%u) bytes=%lu/%lu", connectionHandle, transfer.connectionHandle,
            ownsTransfer(connectionHandle), transfer.active, stateName(transfer.state), errorName(transfer.error), static_cast<unsigned>(transfer.error),
            static_cast<unsigned long>(transfer.receivedBytes), static_cast<unsigned long>(transfer.imageSize));
  if (!ownsTransfer(connectionHandle)) return;
  failTransfer(BleFirmwareUpdate::Error::ConnectionLost, "BLE firmware update connection was lost", ESP_OK, false);
}
