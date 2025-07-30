/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "SS2KLog.h"
#include "BLE_Common.h"

#include <esp_task_wdt.h>
#include <esp_ota_ops.h>
#include <Constants.h>
#include <NimBLEDevice.h>
#include <BLE_Custom_Characteristic.h>

#define BLE_OTA_LOG_TAG "BLE_OTA"

/*------------------------------------------------------------------------------
  BLE instances & variables
  ----------------------------------------------------------------------------*/
BLECharacteristic *pTxCharacteristic;
BLECharacteristic *pOtaCharacteristic;

bool deviceConnected    = false;
bool oldDeviceConnected = false;

String fileExtension = "";

/*------------------------------------------------------------------------------
  OTA instances & variables
  ----------------------------------------------------------------------------*/
static esp_ota_handle_t otaHandler             = 0;
static const esp_partition_t *update_partition = NULL;

int bufferCount   = 0;
bool downloadFlag = false;

/*------------------------------------------------------------------------------
  BLE Peripheral callback(s)
  ----------------------------------------------------------------------------*/

class otaCallback : public BLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override {
    std::string rxData = pCharacteristic->getValue();
    bufferCount++;

    if (!downloadFlag) {
      ss2k->isUpdating = true;
      //-----------------------------------------------
      // First BLE bytes have arrived
      //-----------------------------------------------
      // update the connection interval so that it provides enough time for the long writes

      Serial.printf("1. BeginOTA");
      BLEDevice::getServer()->updateConnParams(connInfo.getConnHandle(), 12, 12, 0, 1000);
      const esp_partition_t *configured = esp_ota_get_boot_partition();
      const esp_partition_t *running    = esp_ota_get_running_partition();

      if (configured != running) {
        SS2K_LOG(BLE_OTA_LOG_TAG, "ERROR: Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x", configured->address, running->address);
        SS2K_LOG(BLE_OTA_LOG_TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
        downloadFlag = false;
        esp_ota_end(otaHandler);
      } else {
        SS2K_LOG(BLE_OTA_LOG_TAG, "2. Running partition type %d subtype %d (offset 0x%08x) \n", running->type, running->subtype, running->address);
      }

      update_partition = esp_ota_get_next_update_partition(NULL);
      if (update_partition == NULL) {
        SS2K_LOG(BLE_OTA_LOG_TAG, "ERROR: No valid partition found");
        downloadFlag     = false;
        ss2k->rebootFlag = true;
        return;
      }

      SS2K_LOG(BLE_OTA_LOG_TAG, "3. Writing to partition subtype %d at offset 0x%x \n", update_partition->subtype, update_partition->address);

      //------------------------------------------------------------------------------------------
      // esp_ota_begin can take a while to complete as it erase the flash partition (3-5 seconds)
      // so make sure there's no timeout on the client side (iOS) that triggers before that.
      //------------------------------------------------------------------------------------------
      esp_task_wdt_config_t wdt_config = {.timeout_ms     = 20000,  // 20 seconds
                                          .idle_core_mask = 0,
                                          .trigger_panic  = false};
      esp_task_wdt_init(&wdt_config);

      // if (BLECommunicationTask != NULL) {
      //   SS2K_LOG(MAIN_LOG_TAG, "Stop BLE Tasks");
      //   if (NimBLEDevice::getScan()->isScanning()) {
      //     NimBLEDevice::getScan()->stop();
      //   }
      //   vTaskDelete(BLECommunicationTask);
      //   BLECommunicationTask = NULL;
      // }
      // if (BLEClientTask != NULL) {
      //   vTaskDelete(BLEClientTask);
      //   BLEClientTask = NULL;
      // }

      // vTaskDelay(5);

      if (esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &otaHandler) != ESP_OK) {
        downloadFlag     = false;
        ss2k->isUpdating = false;
        SS2K_LOG(BLE_OTA_LOG_TAG, "OTA begin failed");
        return;
      }
      downloadFlag = true;
    }

    if (bufferCount >= 1 || rxData.length() > 0) {
      if (esp_ota_write(otaHandler, (uint8_t *)rxData.c_str(), rxData.length()) != ESP_OK) {
        SS2K_LOG(BLE_OTA_LOG_TAG, "Error: write to flash failed");
        downloadFlag = false;
        pTxCharacteristic->notify(0x04, 1);
        ss2k->rebootFlag = true;
        return;
      } else {
        bufferCount = 1;
        // Serial.printf("%d bytes", rxData.length());
        //  Notify the iOS app so next batch can be sent
        Serial.printf(".");
        pTxCharacteristic->notify(0x02, sizeof(uint8_t));
      }

      //-------------------------------------------------------------------
      // check if this was the last data chunk? (normally the last chunk is
      // smaller than the maximum MTU size). For improvement: let iOS app send byte
      // length instead of hardcoding "510"
      //-------------------------------------------------------------------
      if (rxData.length() < 512)  // TODO Asumes at least 511 data bytes (@BLE 4.2).
      {
        SS2K_LOG(BLE_OTA_LOG_TAG, "4. Final byte arrived");
        //-----------------------------------------------------------------
        // Final chunk arrived. Now check that
        // the length of total file is correct
        //-----------------------------------------------------------------
        if (esp_ota_end(otaHandler) != ESP_OK) {
          SS2K_LOG(BLE_OTA_LOG_TAG, "OTA end failed ");
          downloadFlag = false;
          pTxCharacteristic->notify(0x04, sizeof(uint8_t));
          ss2k->rebootFlag = true;
          return;
        }
        pTxCharacteristic->notify(0x05, sizeof(uint8_t));
        //-----------------------------------------------------------------
        // Clear download flag and restart the ESP32 if the firmware
        // update was successful
        //-----------------------------------------------------------------
        SS2K_LOG(BLE_OTA_LOG_TAG, "Set Boot partition");
        if (ESP_OK == esp_ota_set_boot_partition(update_partition)) {
          esp_ota_end(otaHandler);
          downloadFlag = false;
          SS2K_LOG(BLE_OTA_LOG_TAG, "Restarting...");
          ss2k->rebootFlag = true;
          return;
        } else {
          //------------------------------------------------------------
          // Something went wrong, the upload was not successful
          //------------------------------------------------------------
          SS2K_LOG(BLE_OTA_LOG_TAG, "Upload Error");
          pTxCharacteristic->notify(0x04, sizeof(uint8_t));
          downloadFlag = false;
          esp_ota_end(otaHandler);
          ss2k->rebootFlag = true;
          return;
        }
      }
    } else {
      SS2K_LOG(BLE_OTA_LOG_TAG, "Data Length < 1");
      ss2k->isUpdating = false;
      downloadFlag     = false;
      ss2k->rebootFlag = true;
    }
  }
};

void BLEFirmwareSetup(NimBLEServer *pServer) {
  // 3. Create BLE Service
  NimBLEService *pService = pServer->createService(FIRMWARE_SERVICE_UUID);

  // 4. Create BLE Characteristics inside the service(s)
  pTxCharacteristic = pService->createCharacteristic(FIRMWARE_CHARACTERISTIC_TX_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);

  pOtaCharacteristic = pService->createCharacteristic(FIRMWARE_CHARACTERISTIC_OTA_UUID, NIMBLE_PROPERTY::WRITE);
  pOtaCharacteristic->setCallbacks(new otaCallback());

  // 5. Start the service(s)
  pService->start();

  // 6. Start advertising
  // spinBLEServer.pServer->getAdvertising()->addServiceUUID(pService->getUUID());

  downloadFlag = false;
}
