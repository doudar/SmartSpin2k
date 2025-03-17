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

uint8_t txValue   = 0;
int bufferCount   = 0;
bool downloadFlag = false;

/*------------------------------------------------------------------------------
  BLE Peripheral callback(s)
  ----------------------------------------------------------------------------*/

class otaCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic, ble_gap_conn_desc *connDesc) {
    std::string rxData = pCharacteristic->getValue();
    bufferCount++;

    if (!downloadFlag) {
      ss2k->isUpdating = true;
      //-----------------------------------------------
      // First BLE bytes have arrived
      //-----------------------------------------------
      // update the connection interval so that it provides enough time for the long writes

      Serial.printf("1. BeginOTA");
      BLEDevice::getServer()->updateConnParams(connDesc->conn_handle, 12, 12, 0, 1000);
      const esp_partition_t *configured = esp_ota_get_boot_partition();
      const esp_partition_t *running    = esp_ota_get_running_partition();

      if (configured != running) {
        Serial.printf("ERROR: Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x", configured->address, running->address);
        Serial.printf("(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
        downloadFlag = false;
        esp_ota_end(otaHandler);
      } else {
        Serial.printf("2. Running partition type %d subtype %d (offset 0x%08x) \n", running->type, running->subtype, running->address);
      }

      update_partition = esp_ota_get_next_update_partition(NULL);
      assert(update_partition != NULL);

      Serial.printf("3. Writing to partition subtype %d at offset 0x%x \n", update_partition->subtype, update_partition->address);

      //------------------------------------------------------------------------------------------
      // esp_ota_begin can take a while to complete as it erase the flash partition (3-5 seconds)
      // so make sure there's no timeout on the client side (iOS) that triggers before that.
      //------------------------------------------------------------------------------------------
      esp_task_wdt_init(10, false);

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
        downloadFlag = false;
         ss2k->isUpdating = false;
        return;
      }
      downloadFlag = true;
    }

    if (bufferCount >= 1 || rxData.length() > 0) {
      if (esp_ota_write(otaHandler, (uint8_t *)rxData.c_str(), rxData.length()) != ESP_OK) {
        Serial.printf("Error: write to flash failed");
        downloadFlag = false;
        pTxCharacteristic->setValue(&txValue, 0x04);
        pTxCharacteristic->notify();
        return;
      } else {
        bufferCount = 1;
        // Serial.printf("%d bytes", rxData.length());
        //  Notify the iOS app so next batch can be sent
        Serial.printf(".");
        pTxCharacteristic->setValue(&txValue, 0x02);
        // pTxCharacteristic->notify();
      }

      //-------------------------------------------------------------------
      // check if this was the last data chunk? (normally the last chunk is
      // smaller than the maximum MTU size). For improvement: let iOS app send byte
      // length instead of hardcoding "510"
      //-------------------------------------------------------------------
      if (rxData.length() < 512)  // TODO Asumes at least 511 data bytes (@BLE 4.2).
      {
        Serial.printf("4. Final byte arrived");
        //-----------------------------------------------------------------
        // Final chunk arrived. Now check that
        // the length of total file is correct
        //-----------------------------------------------------------------
        if (esp_ota_end(otaHandler) != ESP_OK) {
          Serial.printf("OTA end failed ");
          downloadFlag = false;
          pTxCharacteristic->setValue(&txValue, 0x04);
          pTxCharacteristic->notify();
          return;
        }
        pTxCharacteristic->setValue(&txValue, 0x05);
        pTxCharacteristic->notify();
        //-----------------------------------------------------------------
        // Clear download flag and restart the ESP32 if the firmware
        // update was successful
        //-----------------------------------------------------------------
        Serial.printf("Set Boot partion");
        if (ESP_OK == esp_ota_set_boot_partition(update_partition)) {
          esp_ota_end(otaHandler);
          downloadFlag = false;
          Serial.printf("Restarting...");
          ss2k->rebootFlag = true;
          return;
        } else {
          //------------------------------------------------------------
          // Something went wrong, the upload was not successful
          //------------------------------------------------------------
          Serial.printf("Upload Error");
          pTxCharacteristic->setValue(&txValue, 4);
          pTxCharacteristic->notify();
          downloadFlag = false;
          esp_ota_end(otaHandler);
          return;
        }
      }
    } else {
       ss2k->isUpdating = false;
      downloadFlag = false;
    }
  }
};

void BLEFirmwareSetup() {
  // 3. Create BLE Service
  NimBLEService *pService = spinBLEServer.pServer->createService(FIRMWARE_SERVICE_UUID);

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
