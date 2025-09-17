/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*

Custom Characteristic for userConfig Variable manipulation via BLE

**Overview:**
This characteristic allows for reading and writing various user configuration parameters via BLE. The format for writing and reading data follows a specific protocol.

**Writing Data:**
- Format:
  0x02, <variable>, <LSO>, <MSO>
  - 0x02: Operator for write
  - <variable>: The identifier for the variable to be written
  - <LSO>: Least significant byte of the value
  - <MSO>: Most significant byte of the value

- Example:
  To write 26.3 kph to simulatedSpeed:
  - Convert 26.3 to an integer by multiplying by 10: 263
  - Convert 263 to hexadecimal: 0x0107
  - Swap bytes for little-endian format: 0x07, 0x01
  - Write command: 0x02, 0x06, 0x07, 0x01

**Reading Data:**
- Format:
  0x01, <variable>
  - 0x01: Operator for read
  - <variable>: The identifier for the variable to be read

- Example:
  To read the value of simulatedSpeed:
  - Read command: 0x01, 0x06

**Server Response:**
- For both read and write operations, the server responds with:
  0x80, <variable>, <LSO>, <MSO>
  - 0x80: Status indicating success
  - <variable>: The identifier for the variable
  - <LSO>: Least significant byte of the value
  - <MSO>: Most significant byte of the value

**Detailed Variable Handling:**
- Some float values are multiplied by 10 or 100 for transmission.
- True values are > 00, and false values are 00.

**Examples for Other Variables:**

1. Incline (0x02):
   - Read command: 0x01, 0x02
   - Server response for 5.5% incline:
     - Stored as integer: 55 (multiplied by 10)
     - Hexadecimal: 0x0037
     - Little-endian: 0x37, 0x00
     - Response: 0x80, 0x02, 0x37, 0x00

2. Simulated Watts (0x03):
   - Read command: 0x01, 0x03
   - Server response for 200 watts:
     - Integer: 200
     - Hexadecimal: 0x00C8
     - Little-endian: 0xC8, 0x00
     - Response: 0x80, 0x03, 0xC8, 0x00

3. Simulated Heart Rate (0x04):
   - Read command: 0x01, 0x04
   - Server response for 75 bpm:
     - Integer: 75
     - Hexadecimal: 0x004B
     - Little-endian: 0x4B, 0x00
     - Response: 0x80, 0x04, 0x4B, 0x00

4. Device Name (0x07):
   - Read command: 0x01, 0x07
   - Server response for "MyDevice":
     - ASCII for "MyDevice": 0x4D, 0x79, 0x44, 0x65, 0x76, 0x69, 0x63, 0x65
     - Response: 0x80, 0x07, 0x4D, 0x79, 0x44, 0x65, 0x76, 0x69, 0x63, 0x65

*/

#include <BLE_Common.h>
#include <Power_Table.h>
#include <BLE_Custom_Characteristic.h>
#include <Constants.h>

void BLE_ss2kCustomCharacteristic::setupService(NimBLEServer *pServer) {
  pSmartSpin2kService = spinBLEServer.pServer->createService(SMARTSPIN2K_SERVICE_UUID);
  smartSpin2kCharacteristic =
      pSmartSpin2kService->createCharacteristic(SMARTSPIN2K_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE | NIMBLE_PROPERTY::NOTIFY);
  smartSpin2kCharacteristic->setValue(ss2kCustomCharacteristicValue, sizeof(ss2kCustomCharacteristicValue));
  smartSpin2kCharacteristic->setCallbacks(new ss2kCustomCharacteristicCallbacks());
  pSmartSpin2kService->start();
  spinBLEServer.pServer->getAdvertising()->addServiceUUID(pSmartSpin2kService->getUUID());
}

void BLE_ss2kCustomCharacteristic::update() {}

void ss2kCustomCharacteristicCallbacks::onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) {
  std::string rxValue = pCharacteristic->getValue();
  // SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "Write from %s", connInfo.getAddress().toString().c_str());
  BLE_ss2kCustomCharacteristic::process(rxValue);
}

void ss2kCustomCharacteristicCallbacks::onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue) {
  SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "Subscribe from %s", connInfo.getAddress().toString().c_str());
  NimBLEDevice::setMTU(515);
}
void ss2kCustomCharacteristicCallbacks::onStatus(NimBLECharacteristic *pCharacteristic, int code) {
// loop through and accumulate the data into a C++ string
#ifdef CUSTOM_CHAR_DEBUG
  std::string characteristicValue = pCharacteristic->getValue();
  std::string logValue;
  for (size_t i = 0; i < characteristicValue.length(); ++i) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02x ", (unsigned char)characteristicValue[i]);
    logValue += buf;
  }
  SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "%s -> %s", pCharacteristic->getUUID().toString().c_str(), logValue.c_str());
#endif
}

void BLE_ss2kCustomCharacteristic::notify(char _item, int tableRow) {
  // regular non power table update
  std::string returnValue = {cc_read, _item};
  if (tableRow > -1) {
    returnValue += (uint8_t)tableRow;
  }
  process(returnValue);
}

void BLE_ss2kCustomCharacteristic::process(std::string rxValue) {
  // Find the Characteristic
  if (NimBLEDevice::getServer()->getServiceByUUID(SMARTSPIN2K_SERVICE_UUID) == nullptr) {
    return;
  }
  NimBLECharacteristic *pCharacteristic = NimBLEDevice::getServer()->getServiceByUUID(SMARTSPIN2K_SERVICE_UUID)->getCharacteristic(SMARTSPIN2K_CHARACTERISTIC_UUID);
  uint8_t *pData                        = reinterpret_cast<uint8_t *>(&rxValue[0]);

#ifdef CUSTOM_CHAR_DEBUG
#define LOG_BUF_APPEND(...) logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, __VA_ARGS__)
  int length                = rxValue.length();
  const int kLogBufCapacity = (rxValue.length() * 2) + 60;  // needs to be bigger than the largest message.
  char logBuf[kLogBufCapacity];
  int logBufLength = ss2k_log_hex_to_buffer(pData, length, logBuf, 0, kLogBufCapacity);
#else
#define LOG_BUF_APPEND(...)
#endif

  size_t returnLength = rxValue.length();
  uint8_t returnValue[returnLength];
  std::string returnString = "";
  returnValue[0]           = cc_error;
  for (size_t i = 1; i < returnLength; i++) {
    returnValue[i] = rxValue[i];
  }

  switch (rxValue[1]) {
    case BLE_firmwareUpdateURL:  // 0x01
      LOG_BUF_APPEND("<-Firmware Update URL");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnString   = userConfig->getFirmwareUpdateURL();
      } else if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        String str     = (char *)pData;
        str.remove(0, 2);
        userConfig->firmwareUpdateURL.set(str);
        LOG_BUF_APPEND("(%s)", userConfig->getFirmwareUpdateURL());
      }
      break;

    case BLE_incline: {  // 0x02
      LOG_BUF_APPEND("<-incline");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        int inc        = rtConfig->targetIncline.get() * 10;
        returnValue[2] = (uint8_t)(inc & 0xff);
        returnValue[3] = (uint8_t)(inc >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->targetIncline.set(bytes_to_u16(rxValue[3], rxValue[2]) / 10);
        LOG_BUF_APPEND("(%f)", rtConfig->targetIncline.get());
      }
    } break;

    case BLE_simulatedWatts:  // 0x03
      LOG_BUF_APPEND("<-simulatedWatts");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(rtConfig->watts.getValue() & 0xff);
        returnValue[3] = (uint8_t)(rtConfig->watts.getValue() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->watts.setValue(bytes_to_u16(rxValue[3], rxValue[2]));
        LOG_BUF_APPEND("(%d)", rtConfig->watts.getValue());
      }
      break;

    case BLE_simulatedHr:  // 0x04
      LOG_BUF_APPEND("<-simulatedHr");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(rtConfig->hr.getValue() & 0xff);
        returnValue[3] = (uint8_t)(rtConfig->hr.getValue() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->hr.setValue(bytes_to_u16(rxValue[3], rxValue[2]));
        LOG_BUF_APPEND("(%d)", rtConfig->hr.getValue());
      }
      break;

    case BLE_simulatedCad:  // 0x05
      LOG_BUF_APPEND("<-simulatedCad");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(rtConfig->cad.getValue() & 0xff);
        returnValue[3] = (uint8_t)(rtConfig->cad.getValue() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->cad.setValue(bytes_to_u16(rxValue[3], rxValue[2]));
        LOG_BUF_APPEND("(%d)", rtConfig->cad.getValue());
      }
      break;

    case BLE_simulatedSpeed: {  // 0x06
      LOG_BUF_APPEND("<-simulatedSpeed");
      int spd = rtConfig->simulatedSpeed.get() * 10;
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(spd & 0xff);
        returnValue[3] = (uint8_t)(spd >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->simulatedSpeed.set(bytes_to_u16(rxValue[3], rxValue[2]) / 10);
        LOG_BUF_APPEND("(%d)", rtConfig->simulatedSpeed.get());
      }
    } break;

    case BLE_deviceName:  // 0x07
      LOG_BUF_APPEND("<-deviceName");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnString   = userConfig->getDeviceName();
      } else if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        String str     = (char *)pData;
        str.remove(0, 2);
        userConfig->deviceName.set(str);
        LOG_BUF_APPEND("(%s)", userConfig->getDeviceName());
      }
      break;

    case BLE_shiftStep:  // 0x08
      LOG_BUF_APPEND("<-shiftStep");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(userConfig->shiftStep.get() & 0xff);
        returnValue[3] = (uint8_t)(userConfig->shiftStep.get() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->shiftStep.set(bytes_to_u16(rxValue[3], rxValue[2]));
        LOG_BUF_APPEND("(%d)", userConfig->shiftStep.get());
      }
      break;

    case BLE_stepperPower:  // 0x09
      LOG_BUF_APPEND("<-stepperPower");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(userConfig->stepperPower.get() & 0xff);
        returnValue[3] = (uint8_t)(userConfig->stepperPower.get() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->stepperPower.set(bytes_to_u16(rxValue[3], rxValue[2]));
        ss2k->updateStepperPower();
        LOG_BUF_APPEND("(%d)", userConfig->stepperPower.get());
      }
      break;

    case BLE_stealthChop:  // 0x0A
      LOG_BUF_APPEND("<-stealthChop");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(userConfig->stealthChop.get());
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->stealthChop.set(rxValue[2]);
        ss2k->updateStealthChop();
        LOG_BUF_APPEND("(%s)", userConfig->stealthChop.get() ? "true" : "false");
      }
      break;

    case BLE_inclineMultiplier: {  // 0x0B
      LOG_BUF_APPEND("<-inclineMultiplier");
      int inc = userConfig->inclineMultiplier.get() * 10;
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(inc & 0xff);
        returnValue[3] = (uint8_t)(inc >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->inclineMultiplier.set((bytes_to_u16(rxValue[3], rxValue[2])) / 10.0);
        LOG_BUF_APPEND("(%f)", userConfig->inclineMultiplier.get());
      }
    } break;

    case BLE_powerCorrectionFactor: {  // 0x0C
      LOG_BUF_APPEND("<-powerCorrectionFactor");
      int pcf = userConfig->powerCorrectionFactor.get() * 10;
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(pcf & 0xff);
        returnValue[3] = (uint8_t)(pcf >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->powerCorrectionFactor.set((bytes_to_u16(rxValue[3], rxValue[2])) / 10.0);
        LOG_BUF_APPEND("(%f)", userConfig->powerCorrectionFactor.get());
      }
    } break;

    case BLE_simulateHr:  // 0x0D
      LOG_BUF_APPEND("<-simulateHr");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(rtConfig->hr.getSimulate());
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->hr.setSimulate(rxValue[2]);
        LOG_BUF_APPEND("(%s)", rtConfig->hr.getSimulate() ? "true" : "false");
      }
      break;

    case BLE_simulateWatts:  // 0x0E
      LOG_BUF_APPEND("<-simulateWatts");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(rtConfig->watts.getSimulate());
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->watts.setSimulate(rxValue[2]);
        LOG_BUF_APPEND("(%s)", rtConfig->watts.getSimulate() ? "true" : "false");
      }
      break;

    case BLE_simulateCad:  // 0x0F
      LOG_BUF_APPEND("<-simulateCad");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(rtConfig->cad.getSimulate());
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->cad.setSimulate(rxValue[2]);
        LOG_BUF_APPEND("(%s)", rtConfig->cad.getSimulate() ? "true" : "false");
      }
      break;

    case BLE_FTMSMode:  // 0x10
      LOG_BUF_APPEND("<-FTMSMode");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(rtConfig->FTMSMode.get() & 0xff);
        returnValue[3] = (uint8_t)(rtConfig->FTMSMode.get() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->FTMSMode.set(bytes_to_u16(rxValue[3], rxValue[2]));
        LOG_BUF_APPEND("(%hhu)", rtConfig->FTMSMode.get());
      }
      break;

    case BLE_autoUpdate:  // 0x11
      LOG_BUF_APPEND("<-autoUpdate");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(userConfig->autoUpdate.get());
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->autoUpdate.set(rxValue[2]);
        LOG_BUF_APPEND("(%s)", userConfig->autoUpdate.get() ? "true" : "false");
      }
      break;

    case BLE_ssid:  // 0x12
      LOG_BUF_APPEND("<-ssid");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnString   = userConfig->getSsid();
      } else if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        String str     = (char *)pData;
        str.remove(0, 2);
        userConfig->ssid.set(str);
        LOG_BUF_APPEND("(%s)", userConfig->getSsid());
      }
      break;

    case BLE_password:  // 0x13
      LOG_BUF_APPEND("<-password");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnString   = userConfig->getPassword();
      } else if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        String str     = (char *)pData;
        str.remove(0, 2);
        userConfig->password.set(str);
        LOG_BUF_APPEND("(%s)", "******");
      }
      break;

    case BLE_foundDevices:  // 0x14
      LOG_BUF_APPEND("<-foundDevices");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnString   = userConfig->getFoundDevices();
      } else if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        String str     = (char *)pData;
        str.remove(0, 2);
        userConfig->foundDevices.set(str);
        LOG_BUF_APPEND("(%s)", userConfig->getFoundDevices());
      }
      break;

    case BLE_connectedPowerMeter:  // 0x15
      LOG_BUF_APPEND("<-connectedPowerMete");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnString   = userConfig->getConnectedPowerMeter();
      } else if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        String str     = (char *)pData;
        str.remove(0, 2);
        userConfig->connectedPowerMeter.set(str);
        LOG_BUF_APPEND("(%s)", userConfig->getConnectedPowerMeter());
      }
      break;

    case BLE_connectedHeartMonitor:  // 0x16
      LOG_BUF_APPEND("<-connectedHeartMonitor");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnString   = userConfig->getConnectedHeartMonitor();
      } else if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        String str     = (char *)pData;
        str.remove(0, 2);
        userConfig->connectedHeartMonitor.set(str);
        LOG_BUF_APPEND("(%s)", userConfig->getConnectedHeartMonitor());
      }
      break;

    case BLE_shifterPosition:  // 0x17
      LOG_BUF_APPEND("<-shifterPosition");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(rtConfig->shifterPosition.get() & 0xff);
        returnValue[3] = (uint8_t)(rtConfig->shifterPosition.get() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->shifterPosition.set(bytes_to_u16(rxValue[3], rxValue[2]));
        LOG_BUF_APPEND("(%d)", rtConfig->shifterPosition.get());
#ifdef CUSTOM_CHAR_DEBUG
        SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "%s", logBuf);
#endif
        return;  // Return here and let SpinBLEServer::notifyShift() handle the return to prevent duplicate notifications.
      }
      break;

    case BLE_saveToLittleFS:  // 0x18
      LOG_BUF_APPEND("<-saveToLittleFS");
      if (rxValue[0] == cc_write) {
        ss2k->saveFlag = true;
        returnValue[0] = cc_success;
      }

      break;

    case BLE_targetPosition:  // 0x19
      LOG_BUF_APPEND("<-targetPosition");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(ss2k->getTargetPosition() & 0xff);
        returnValue[3] = (uint8_t)(ss2k->getTargetPosition() >> 8);
        returnValue[4] = (uint8_t)(ss2k->getTargetPosition() >> 16);
        returnValue[5] = (uint8_t)(ss2k->getTargetPosition() >> 24);
        returnLength += 4;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        ss2k->setTargetPosition(int32_t((uint8_t)(rxValue[2]) << 0 | (uint8_t)(rxValue[3]) << 8 | (uint8_t)(rxValue[4]) << 16 | (uint8_t)(rxValue[5]) << 24));
        LOG_BUF_APPEND(" (%f)", ss2k->getTargetPosition());
      }
      break;

    case BLE_externalControl:  // 0x1A
      LOG_BUF_APPEND("<-externalControl");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(ss2k->externalControl);
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0]        = cc_success;
        ss2k->externalControl = static_cast<bool>(rxValue[2]);
        LOG_BUF_APPEND("(%s)", ss2k->externalControl ? "On" : "Off");
      }
      break;

    case BLE_syncMode:  // 0x1B
      LOG_BUF_APPEND("<-syncMode");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(ss2k->syncMode);
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        ss2k->syncMode = static_cast<bool>(rxValue[2]);
        LOG_BUF_APPEND("(%s)", ss2k->syncMode ? "true" : "false");
      }
      break;

    case BLE_reboot:  // 0x1C
      LOG_BUF_APPEND("<-reboot");
      if (rxValue[0] == cc_write) {
        ss2k->rebootFlag = true;
        returnValue[0]   = cc_success;
      }
      break;

    case BLE_resetToDefaults:  // 0x1D
      LOG_BUF_APPEND("<-reset to defaults");
      if (rxValue[0] == cc_write) {
        ss2k->resetDefaultsFlag = true;
        returnValue[0]          = cc_success;
      }

      break;
    case BLE_stepperSpeed:  // 0x1E
      LOG_BUF_APPEND("<-stepperSpeed");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(userConfig->stepperSpeed.get() & 0xff);
        returnValue[3] = (uint8_t)(userConfig->stepperSpeed.get() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->stepperSpeed.set(bytes_to_u16(rxValue[3], rxValue[2]));
        LOG_BUF_APPEND("(%d)", userConfig->stepperSpeed.get());
        ss2k->updateStepperSpeed();
#ifdef CUSTOM_CHAR_DEBUG
        SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "%s", logBuf);
#endif
      }
      break;

    case BLE_ERGSensitivity: {  // 0x1F
      LOG_BUF_APPEND("<-ERGSensitivity");
      int pcf = userConfig->ERGSensitivity.get() * 10;
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(pcf & 0xff);
        returnValue[3] = (uint8_t)(pcf >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->ERGSensitivity.set((bytes_to_u16(rxValue[3], rxValue[2])) / 10);
        LOG_BUF_APPEND("(%f)", userConfig->ERGSensitivity.get());
      }
    } break;

    case BLE_shiftDir:  // 0x20
      LOG_BUF_APPEND("<-ShiftDir");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(userConfig->shifterDir.get());
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->shifterDir.set(static_cast<bool>(rxValue[2]));
        LOG_BUF_APPEND("(%s)", userConfig->shifterDir.get() ? "Normal" : "Reverse");
      }
      break;
      ///////////////
    case BLE_minBrakeWatts:  // 0x21
      LOG_BUF_APPEND("<-MinWatts");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(userConfig->minWatts.get() & 0xff);
        returnValue[3] = (uint8_t)(userConfig->minWatts.get() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->minWatts.set(bytes_to_u16(rxValue[3], rxValue[2]));
        LOG_BUF_APPEND("(%d)", userConfig->minWatts.get());
#ifdef CUSTOM_CHAR_DEBUG
        SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "%s", logBuf);
#endif
      }
      break;
    case BLE_maxBrakeWatts:  // 0x22
      LOG_BUF_APPEND("<-MaxWatts");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(userConfig->maxWatts.get() & 0xff);
        returnValue[3] = (uint8_t)(userConfig->maxWatts.get() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->maxWatts.set(bytes_to_u16(rxValue[3], rxValue[2]));
        LOG_BUF_APPEND("(%d)", userConfig->maxWatts.get());
#ifdef CUSTOM_CHAR_DEBUG
        SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "%s", logBuf);
#endif
      }
      break;
    case BLE_restartBLE:  // 0x23
      LOG_BUF_APPEND("<-restart BLE");
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        spinBLEClient.reconnectAllDevices();
      }
      break;
    case BLE_scanBLE:  // 0x24
      LOG_BUF_APPEND("<-scan BLE");
      if (rxValue[0] == cc_write) {
        returnValue[0]       = cc_success;
        spinBLEClient.doScan = true;
      }
      break;
    case BLE_firmwareVer:  // 0x25
      LOG_BUF_APPEND("<-Firmware Version");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnString   = FIRMWARE_VERSION;
      }
      break;
    case BLE_resetPowerTable:  // 0x26
      LOG_BUF_APPEND("<-Reset PTab");
      if (rxValue[0] == cc_write) {
        returnValue[0]            = cc_success;
        ss2k->resetPowerTableFlag = true;
      }
      break;
    case BLE_powerTableData:  // 0x27
      LOG_BUF_APPEND("<-Power Tab Data");
      if (rxValue[0] == cc_read) {
        int row = 6;  // 90rpm
        if (rxValue[2] >= 0 || rxValue[2] < POWERTABLE_CAD_SIZE) {
          row = rxValue[2];
        }
        returnString += (uint8_t)row;
        for (int i = 0; i < POWERTABLE_WATT_SIZE; i++) {
          returnString += (uint8_t)(powerTable->ptData.tableRow[row].tableEntry[i].targetPosition & 0xff);
          returnString += (uint8_t)(powerTable->ptData.tableRow[row].tableEntry[i].targetPosition >> 8);
          //  Serial.printf("%02x%02x ", (uint8_t)(powerTable->ptData.tableRow[row].tableEntry[i].targetPosition & 0xff),
          //               (uint8_t)(powerTable->ptData.tableRow[row].tableEntry[i].targetPosition >> 8));
        }
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        if (rxValue[2] >= 0 && rxValue[2] < POWERTABLE_CAD_SIZE) {
          for (int i = 0; i < POWERTABLE_WATT_SIZE; i++) {
            powerTable->ptData.tableRow[rxValue[2]].tableEntry[i].targetPosition = (int16_t((uint8_t)(rxValue[i * 2 + 3]) << 0 | (uint8_t)(rxValue[i * 2 + 4]) << 8));
            // Ensure each entry has a valid reading count to be considered during loading
            if (powerTable->ptData.tableRow[rxValue[2]].tableEntry[i].targetPosition != INT16_MIN) {
              powerTable->ptData.tableRow[rxValue[2]].tableEntry[i].readings = MINIMUM_RELIABLE_POSITIONS + 1;
            }
          }
          // Save with explicit version management
          powerTable->_hasBeenLoadedThisSession = true;  // Prevent reload attempts
          powerTable->saveFlag                  = true;
          // Saved tables all use hMin of Zero and this is not set by the app.
          userConfig->hMin.set(0);
        } else {
          // SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "Table row invalid");
          //  Logging causes crashes in ISR
        }
      }
      break;
    case BLE_simulatedTargetWatts:  // 0x28
      LOG_BUF_APPEND("<-targetWatts");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(rtConfig->watts.getTarget() & 0xff);
        returnValue[3] = (uint8_t)(rtConfig->watts.getTarget() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->watts.setValue(bytes_to_u16(rxValue[3], rxValue[2]));
        LOG_BUF_APPEND("(%d)", rtConfig->watts.getTarget());
      }
      break;
    case BLE_simulateTargetWatts:  // 0x29
      LOG_BUF_APPEND("<-simulatetargetwatts");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(rtConfig->simTargetWatts.get());
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        rtConfig->simTargetWatts.set(rxValue[2]);
        LOG_BUF_APPEND("(%s)", rtConfig->simTargetWatts.get() ? "true" : "false");
      }
      break;
    case BLE_hMin:  // 0x2A
      LOG_BUF_APPEND("<-hMin");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(userConfig->hMin.get() & 0xff);
        returnValue[3] = (uint8_t)(userConfig->hMin.get() >> 8);
        returnValue[4] = (uint8_t)(userConfig->hMin.get() >> 16);
        returnValue[5] = (uint8_t)(userConfig->hMin.get() >> 24);
        returnLength += 4;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        int32_t hMin = int32_t((uint8_t)(rxValue[2]) << 0 | (uint8_t)(rxValue[3]) << 8 | (uint8_t)(rxValue[4]) << 16 | (uint8_t)(rxValue[5]) << 24);
        userConfig->hMin.set(hMin);
        rtConfig->setMinStep(hMin);
        LOG_BUF_APPEND(" (%d)", hMin);
      }
      break;

    case BLE_hMax:  // 0x2B
      LOG_BUF_APPEND("<-hMax");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(userConfig->hMax.get() & 0xff);
        returnValue[3] = (uint8_t)(userConfig->hMax.get() >> 8);
        returnValue[4] = (uint8_t)(userConfig->hMax.get() >> 16);
        returnValue[5] = (uint8_t)(userConfig->hMax.get() >> 24);
        returnLength += 4;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        int32_t hMax   = int32_t((uint8_t)(rxValue[2]) << 0 | (uint8_t)(rxValue[3]) << 8 | (uint8_t)(rxValue[4]) << 16 | (uint8_t)(rxValue[5]) << 24);
        Serial.printf("hMax: %d\n <--------------------------------------------", hMax);
        userConfig->hMax.set(hMax);
        rtConfig->setMaxStep(hMax);
        LOG_BUF_APPEND(" (%d)", userConfig->hMax.get());
      }
      break;

    case BLE_homingSensitivity:  // 0x2C
      LOG_BUF_APPEND("<-homingSensitivity");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(userConfig->homingSensitivity.get() & 0xff);
        returnValue[3] = (uint8_t)(userConfig->homingSensitivity.get() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->homingSensitivity.set(bytes_to_u16(rxValue[3], rxValue[2]));
        LOG_BUF_APPEND("(%d)", userConfig->homingSensitivity.get());
      }
      break;

    case BLE_pTab4Pwr:  // 0x2D
      LOG_BUF_APPEND("<-pTab4Pwr");
      if (rxValue[0] == cc_read) {
        returnValue[0] = cc_success;
        returnValue[2] = (uint8_t)(userConfig->pTab4Pwr.get());
        returnLength += 1;
      }
      if (rxValue[0] == cc_write) {
        returnValue[0] = cc_success;
        userConfig->pTab4Pwr.set(rxValue[2]);
        LOG_BUF_APPEND("(%s)", userConfig->pTab4Pwr.get() ? "true" : "false");
      }
      break;

    default:
      LOG_BUF_APPEND("<-Unknown Characteristic");
      returnValue[0] = cc_error;
      break;
  }

#ifdef CUSTOM_CHAR_DEBUG
  SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "%s", logBuf);
#endif
  if (returnString == "") {
    pCharacteristic->setValue(returnValue, returnLength);
  } else {  // Need to send a string instead
    uint8_t returnChar[returnString.length() + 2];
    returnChar[0] = cc_success;
    returnChar[1] = rxValue[1];
    for (int i = 0; i < returnString.length(); i++) {
      returnChar[i + 2] = returnString[i];
    }
    pCharacteristic->setValue(returnChar, returnString.length() + 2);
  }

  pCharacteristic->indicate();
}

// iterate through all smartspin user parameters and notify the specific one if changed
void BLE_ss2kCustomCharacteristic::parseNemit() {
  static userParameters _oldParams;
  static RuntimeParameters _oldRTParams;

  if (userConfig->autoUpdate.get() != _oldParams.autoUpdate.get()) {
    _oldParams.autoUpdate.set(userConfig->autoUpdate.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_autoUpdate);
    return;  // only do one at a time because immediate update isn't super important for these values
  }

  if (strcmp(userConfig->getFirmwareUpdateURL(), _oldParams.getFirmwareUpdateURL()) != 0) {
    _oldParams.firmwareUpdateURL.set(userConfig->getFirmwareUpdateURL());
    BLE_ss2kCustomCharacteristic::notify(BLE_firmwareUpdateURL);
    return;
  }

  if (strcmp(userConfig->getDeviceName(), _oldParams.getDeviceName()) != 0) {
    _oldParams.deviceName.set(userConfig->getDeviceName());
    BLE_ss2kCustomCharacteristic::notify(BLE_deviceName);
    return;
  }

  if (userConfig->shiftStep.get() != _oldParams.shiftStep.get()) {
    _oldParams.shiftStep.set(userConfig->shiftStep.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_shiftStep);
    return;
  }

  if (userConfig->stealthChop.get() != _oldParams.stealthChop.get()) {
    _oldParams.stealthChop.set(userConfig->stealthChop.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_stealthChop);
    return;
  }

  if (userConfig->inclineMultiplier.get() != _oldParams.inclineMultiplier.get()) {
    _oldParams.inclineMultiplier.set(userConfig->inclineMultiplier.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_inclineMultiplier);
    return;
  }

  if (userConfig->powerCorrectionFactor.get() != _oldParams.powerCorrectionFactor.get()) {
    _oldParams.powerCorrectionFactor.set(userConfig->powerCorrectionFactor.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_powerCorrectionFactor);
    return;
  }

  if (strcmp(userConfig->getSsid(), _oldParams.getSsid()) != 0) {
    _oldParams.ssid.set(userConfig->getSsid());
    BLE_ss2kCustomCharacteristic::notify(BLE_ssid);
    return;
  }

  if (strcmp(userConfig->getPassword(), _oldParams.getPassword()) != 0) {
    _oldParams.password.set(userConfig->getPassword());
    BLE_ss2kCustomCharacteristic::notify(BLE_password);
    return;
  }

  if (strcmp(userConfig->getConnectedPowerMeter(), _oldParams.getConnectedPowerMeter()) != 0) {
    _oldParams.connectedPowerMeter.set(userConfig->getConnectedPowerMeter());
    BLE_ss2kCustomCharacteristic::notify(BLE_connectedPowerMeter);
    return;
  }

  if (strcmp(userConfig->getConnectedHeartMonitor(), _oldParams.getConnectedHeartMonitor()) != 0) {
    _oldParams.connectedHeartMonitor.set(userConfig->getConnectedHeartMonitor());
    BLE_ss2kCustomCharacteristic::notify(BLE_connectedHeartMonitor);
    return;
  }

  if (userConfig->stepperPower.get() != _oldParams.stepperPower.get()) {
    _oldParams.stepperPower.set(userConfig->stepperPower.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_stepperPower);
    return;
  }

  if (userConfig->stepperSpeed.get() != _oldParams.stepperSpeed.get()) {
    _oldParams.stepperSpeed.set(userConfig->stepperSpeed.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_stepperSpeed);
    return;
  }

  if (userConfig->ERGSensitivity.get() != _oldParams.ERGSensitivity.get()) {
    _oldParams.ERGSensitivity.set(userConfig->ERGSensitivity.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_ERGSensitivity);
    return;
  }

  if (userConfig->stepperDir.get() != _oldParams.stepperDir.get()) {
    _oldParams.stepperDir.set(userConfig->stepperDir.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_shiftDir);
    return;
  }

  if (strcmp(userConfig->getFoundDevices(), _oldParams.getFoundDevices()) != 0) {
    _oldParams.foundDevices.set(userConfig->getFoundDevices());
    BLE_ss2kCustomCharacteristic::notify(BLE_foundDevices);
    return;
  }

  if (userConfig->minWatts.get() != _oldParams.minWatts.get()) {
    _oldParams.minWatts.set(userConfig->minWatts.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_minBrakeWatts);
    return;
  }

  if (userConfig->maxWatts.get() != _oldParams.maxWatts.get()) {
    _oldParams.maxWatts.set(userConfig->maxWatts.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_maxBrakeWatts);
    return;
  }
  if (userConfig->shifterDir.get() != _oldParams.shifterDir.get()) {
    _oldParams.shifterDir.set(userConfig->shifterDir.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_shiftDir);
    return;
  }
  if (rtConfig->FTMSMode.get() != _oldRTParams.FTMSMode.get()) {
    _oldRTParams.FTMSMode.set(rtConfig->FTMSMode.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_FTMSMode);
    return;
  }
  if (rtConfig->watts.getTarget() != _oldRTParams.watts.getTarget()) {
    _oldRTParams.watts.setTarget(rtConfig->watts.getTarget());
    BLE_ss2kCustomCharacteristic::notify(BLE_simulatedTargetWatts);
    return;
  }
  if (rtConfig->simTargetWatts.get() != _oldRTParams.simTargetWatts.get()) {
    _oldRTParams.simTargetWatts.set(rtConfig->simTargetWatts.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_simulateTargetWatts);
    return;
  }
  if (userConfig->hMin.get() != _oldParams.hMin.get()) {
    _oldParams.hMin.set(userConfig->hMin.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_hMin);
    userConfig->saveToLittleFS();
    return;
  }
  if (userConfig->hMax.get() != _oldParams.hMax.get()) {
    _oldParams.hMax.set(userConfig->hMax.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_hMax);
    userConfig->saveToLittleFS();
    return;
  }
  if (userConfig->homingSensitivity.get() != _oldParams.homingSensitivity.get()) {
    _oldParams.homingSensitivity.set(userConfig->homingSensitivity.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_homingSensitivity);
    return;
  }
  if (userConfig->pTab4Pwr.get() != _oldParams.pTab4Pwr.get()) {
    _oldParams.pTab4Pwr.set(userConfig->pTab4Pwr.get());
    BLE_ss2kCustomCharacteristic::notify(BLE_pTab4Pwr);
    // Home whenever this value is flipped true
    if (userConfig->pTab4Pwr.get()) {
      spinBLEServer.spinDownFlag = 1;
    }
    return;
  }
}
