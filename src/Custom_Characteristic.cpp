/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
Custom Characteristic for userConfig Variable manipulation via BLE

An example follows to read/write 26.3kph to simulatedSpeed:
simulatedSpeed is a float and first needs to be converted to int by *10 for transmission, so convert 26.3kph to 263 (multiply by 10)
Decimal 263 == hexidecimal 0107 but the data needs to be converted to LSO, MSO to match the rest of the BLE spec so 263 == 0x07, 0x01 (LSO,MSO)

So,

If client wants to write (0x02) int value 263 (0x07 0x01) to simulatedSpeed(0x06):

Client Writes:
0x02, 0x06, 0x07, 0x01
(operator, variable, LSO, MSO)

Server will then indicate:
0x80, 0x06, 0x07, 0x01
(status, variable, LSO, MSO)

Example to read (0x01) from simulatedSpeed (0x06)

Client Writes:
0x01, 0x06
Server will then indicate:
0x80, 0x06, 0x07, 0x01
success, simulatedSpeed,0x07,0x01

Pay special attention to the float values below. Since they have to be transmitted as an int, some are converted *100, others are converted *10.
True values are >00. False are 00.
*/

#include <BLE_Common.h>
#include <Custom_Characteristic.h>

void ss2kCustomCharacteristicCallbacks::onWrite(BLECharacteristic *pCharacteristic) {
  std::string rxValue = pCharacteristic->getValue();
  uint8_t read        = 0x01;  // value to request read operation
  uint8_t write       = 0x02;  // Value to request write operation
  uint8_t error       = 0xff;  // value server error/unable
  uint8_t success     = 0x80;  // value for success

  uint8_t *pData = reinterpret_cast<uint8_t *>(&rxValue[0]);
  int length     = rxValue.length();

  const int kLogBufCapacity = (rxValue.length() * 2) + 60;  // needs to be bigger than the largest message.
  char logBuf[kLogBufCapacity];
  int logBufLength = ss2k_log_hex_to_buffer(pData, length, logBuf, 0, kLogBufCapacity);

  size_t returnLength = rxValue.length();
  uint8_t returnValue[returnLength];
  std::string returnString = "";
  returnValue[0]           = error;
  for (size_t i = 1; i < returnLength; i++) {
    returnValue[i] = rxValue[i];
  }

  switch (rxValue[1]) {
    
    case BLE_firmwareUpdateURL:  // 0x01
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-Firmware Update URL");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnString = userConfig->getFirmwareUpdateURL();
      } else if (rxValue[0] == write) {
        String str = (char *)pData;
        str.remove(0, 2);
        userConfig->setFirmwareUpdateURL(str);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", userConfig->getFirmwareUpdateURL());
      }
      break;

    case BLE_incline: {  // 0x02
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-incline");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        int inc        = rtConfig->getTargetIncline() * 10;
        returnValue[2] = (uint8_t)(inc & 0xff);
        returnValue[3] = (uint8_t)(inc >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        rtConfig->setTargetIncline(bytes_to_u16(rxValue[3], rxValue[2]) / 10);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%f)", rtConfig->getTargetIncline());
      }
    } break;

    case BLE_simulatedWatts:  // 0x03
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-simulatedWatts");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(rtConfig->watts.getValue() & 0xff);
        returnValue[3] = (uint8_t)(rtConfig->watts.getValue() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        rtConfig->watts.setValue(bytes_to_u16(rxValue[3], rxValue[2]));
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%d)", rtConfig->watts.getValue());
      }
      break;

    case BLE_simulatedHr:  // 0x04
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-simulatedHr");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(rtConfig->hr.getValue() & 0xff);
        returnValue[3] = (uint8_t)(rtConfig->hr.getValue() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        rtConfig->hr.setValue(bytes_to_u16(rxValue[3], rxValue[2]));
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%d)", rtConfig->hr.getValue());
      }
      break;

    case BLE_simulatedCad:  // 0x05
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-simulatedCad");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(rtConfig->cad.getValue() & 0xff);
        returnValue[3] = (uint8_t)(rtConfig->cad.getValue() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        rtConfig->cad.setValue(bytes_to_u16(rxValue[3], rxValue[2]));
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%d)", rtConfig->cad.getValue());
      }
      break;

    case BLE_simulatedSpeed: {  // 0x06
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-simulatedSpeed");
      returnValue[0] = success;
      int spd        = rtConfig->getSimulatedSpeed() * 10;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(spd & 0xff);
        returnValue[3] = (uint8_t)(spd >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        rtConfig->setSimulatedSpeed(bytes_to_u16(rxValue[3], rxValue[2]) / 10);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%d)", rtConfig->getSimulatedSpeed());
      }
    } break;

    case BLE_deviceName:  // 0x07
      logBufLength   = snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-deviceName");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnString = userConfig->getDeviceName();
      } else if (rxValue[0] == write) {
        String str = (char *)pData;
        str.remove(0, 2);
        userConfig->setDeviceName(str);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", userConfig->getDeviceName());
      }
      break;

    case BLE_shiftStep:  // 0x08
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-shiftStep");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig->getShiftStep() & 0xff);
        returnValue[3] = (uint8_t)(userConfig->getShiftStep() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        userConfig->setShiftStep(bytes_to_u16(rxValue[3], rxValue[2]));
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%d)", userConfig->getShiftStep());
      }
      break;

    case BLE_stepperPower:  // 0x09
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-stepperPower");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig->getStepperPower() & 0xff);
        returnValue[3] = (uint8_t)(userConfig->getStepperPower() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        userConfig->setStepperPower(bytes_to_u16(rxValue[3], rxValue[2]));
        ss2k->updateStepperPower();
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%d)", userConfig->getStepperPower());
      }
      break;

    case BLE_stealthChop:  // 0x0A
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-stealthChop");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig->getStealthChop());
        returnLength += 1;
      }
      if (rxValue[0] == write) {
        userConfig->setStealthChop(rxValue[2]);
        ss2k->updateStealthChop();
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", userConfig->getStealthChop() ? "true" : "false");
      }
      break;

    case BLE_inclineMultiplier: {  // 0x0B
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-inclineMultiplier");
      returnValue[0] = success;
      int inc        = userConfig->getInclineMultiplier() * 10;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(inc & 0xff);
        returnValue[3] = (uint8_t)(inc >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        userConfig->setInclineMultiplier(bytes_to_u16(rxValue[3], rxValue[2]) / 10);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%f)", userConfig->getInclineMultiplier());
      }
    } break;

    case BLE_powerCorrectionFactor: {  // 0x0C
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-powerCorrectionFactor");
      returnValue[0] = success;
      int pcf        = userConfig->getPowerCorrectionFactor() * 10;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(pcf & 0xff);
        returnValue[3] = (uint8_t)(pcf >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        userConfig->setPowerCorrectionFactor(bytes_to_u16(rxValue[3], rxValue[2]) / 10);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%f)", userConfig->getPowerCorrectionFactor());
      }
    } break;

    case BLE_simulateHr:  // 0x0D
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-simulateHr");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(rtConfig->hr.getSimulate());
        returnLength += 1;
      }
      if (rxValue[0] == write) {
        rtConfig->hr.setSimulate(rxValue[2]);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", rtConfig->hr.getSimulate() ? "true" : "false");
      }
      break;

    case BLE_simulateWatts:  // 0x0E
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-simulateWatts");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(rtConfig->watts.getSimulate());
        returnLength += 1;
      }
      if (rxValue[0] == write) {
        rtConfig->watts.setSimulate(rxValue[2]);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", rtConfig->watts.getSimulate() ? "true" : "false");
      }
      break;

    case BLE_simulateCad:  // 0x0F
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-simulateCad");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(rtConfig->cad.getSimulate());
        returnLength += 1;
      }
      if (rxValue[0] == write) {
        rtConfig->cad.setSimulate(rxValue[2]);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", rtConfig->cad.getSimulate() ? "true" : "false");
      }
      break;

    case BLE_FTMSMode:  // 0x10
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-FTMSMode");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(rtConfig->getFTMSMode() & 0xff);
        returnValue[3] = (uint8_t)(rtConfig->getFTMSMode() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        rtConfig->setFTMSMode(bytes_to_u16(rxValue[3], rxValue[2]));
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%hhu)", rtConfig->getFTMSMode());
      }
      break;

    case BLE_autoUpdate:  // 0x11
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-autoUpdate");

      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig->getAutoUpdate());
        returnLength += 1;
      }
      if (rxValue[0] == write) {
        userConfig->setAutoUpdate(rxValue[2]);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", userConfig->getAutoUpdate() ? "true" : "false");
      }
      break;

    case BLE_ssid:  // 0x12
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-ssid");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnString = userConfig->getSsid();
      } else if (rxValue[0] == write) {
        String str = (char *)pData;
        str.remove(0, 2);
        userConfig->setSsid(str);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", userConfig->getSsid());
      }
      break;

    case BLE_password:  // 0x13
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-password");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnString = userConfig->getPassword();
      } else if (rxValue[0] == write) {
        String str = (char *)pData;
        str.remove(0, 2);
        userConfig->setPassword(str);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", "******");
      }
      break;

    case BLE_foundDevices:  // 0x14
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-foundDevices");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnString = userConfig->getFoundDevices();
      } else if (rxValue[0] == write) {
        String str = (char *)pData;
        str.remove(0, 2);
        userConfig->setFoundDevices(str);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", userConfig->getFoundDevices());
      }
      break;

    case BLE_connectedPowerMeter:  // 0x15
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-connectedPowerMete");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnString = userConfig->getConnectedPowerMeter();
      } else if (rxValue[0] == write) {
        String str = (char *)pData;
        str.remove(0, 2);
        userConfig->setConnectedPowerMeter(str);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", userConfig->getConnectedPowerMeter());
      }
      break;

    case BLE_connectedHeartMonitor:  // 0x16
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-connectedHeartMonitor");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnString = userConfig->getConnectedHeartMonitor();
      } else if (rxValue[0] == write) {
        String str = (char *)pData;
        str.remove(0, 2);
        userConfig->setConnectedHeartMonitor(str);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", userConfig->getConnectedHeartMonitor());
      }
      break;

    case BLE_shifterPosition:  // 0x17
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-shifterPosition");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(rtConfig->getShifterPosition() & 0xff);
        returnValue[3] = (uint8_t)(rtConfig->getShifterPosition() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        rtConfig->setShifterPosition(bytes_to_u16(rxValue[3], rxValue[2]));
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%d)", rtConfig->getShifterPosition());
        SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "%s", logBuf);
        return;  // Return here and let SpinBLEServer::notifyShift() handle the return to prevent duplicate notifications.
      }
      break;

    case BLE_saveToLittleFS:  // 0x18
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-saveToLittleFS");
      if (rxValue[0] == write) {
        ss2k->saveFlag = true;
        returnValue[0] = success;
      }

      break;

    case BLE_targetPosition:  // 0x19
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-targetPosition");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(ss2k->targetPosition & 0xff);
        returnValue[3] = (uint8_t)(ss2k->targetPosition >> 8);
        returnValue[4] = (uint8_t)(ss2k->targetPosition >> 16);
        returnValue[5] = (uint8_t)(ss2k->targetPosition >> 24);
        returnLength += 4;
      }
      if (rxValue[0] == write) {
        ss2k->targetPosition = (int32_t((uint8_t)(rxValue[2]) << 0 | (uint8_t)(rxValue[3]) << 8 | (uint8_t)(rxValue[4]) << 16 | (uint8_t)(rxValue[5]) << 24));
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, " (%f)", ss2k->targetPosition);
      }
      break;

    case BLE_externalControl:  // 0x1A
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-externalControl");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(ss2k->externalControl);
        returnLength += 1;
      }
      if (rxValue[0] == write) {
        ss2k->externalControl = static_cast<bool>(rxValue[2]);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", ss2k->externalControl? "On" : "Off");
      }
      break;

    case BLE_syncMode:  // 0x1B
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-syncMode");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(ss2k->syncMode);
        returnLength += 1;
      }
      if (rxValue[0] == write) {
        ss2k->syncMode = static_cast<bool>(rxValue[2]);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", ss2k->syncMode ? "true" : "false");
      }
      break;

    case BLE_reboot:  // 0x1C
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-reboot");
      if (rxValue[0] == write) {
        ss2k->rebootFlag = true;
        returnValue[0]   = success;
      }
      break;

    case BLE_resetToDefaults:  // 0x1D
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-reset to defaults");
      if (rxValue[0] == write) {
        userConfig->setDefaults();
        returnValue[0] = success;
      }

      break;
    case BLE_stepperSpeed:  // 0x1E
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-stepperSpeed");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig->getStepperSpeed() & 0xff);
        returnValue[3] = (uint8_t)(userConfig->getStepperSpeed() >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        userConfig->setStepperSpeed(bytes_to_u16(rxValue[3], rxValue[2]));
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%d)", userConfig->getStepperSpeed());
        ss2k->updateStepperSpeed();
        SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "%s", logBuf);
        return;
      }
      break;

    case BLE_ERGSensitivity: {  // 0x1F
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-ERGSensitivity");
      returnValue[0] = success;
      int pcf        = userConfig->getERGSensitivity() * 10;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(pcf & 0xff);
        returnValue[3] = (uint8_t)(pcf >> 8);
        returnLength += 2;
      }
      if (rxValue[0] == write) {
        userConfig->setERGSensitivity(bytes_to_u16(rxValue[3], rxValue[2]) / 10);
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%f)", userConfig->getERGSensitivity());
      }
    } break;

    case BLE_shiftDir:  // 0x20
      logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "<-ShiftDir");
      returnValue[0] = success;
      if (rxValue[0] == read) {
        returnValue[2] = (uint8_t)(userConfig->getShifterDir());
        returnLength += 1;
      }
      if (rxValue[0] == write) {
        userConfig->setShifterDir(static_cast<bool>(rxValue[2]));
        logBufLength += snprintf(logBuf + logBufLength, kLogBufCapacity - logBufLength, "(%s)", userConfig->getShifterDir() ? "Normal" : "Reverse");
      }
      break;
  }

  SS2K_LOG(CUSTOM_CHAR_LOG_TAG, "%s", logBuf);
  if (returnString == "") {
    pCharacteristic->setValue(returnValue, returnLength);
  } else {  // Need to send a string instead
    uint8_t returnChar[returnString.length() + 2];
    returnChar[0] = 0x80;
    returnChar[1] = rxValue[1];
    for (int i = 0; i < returnString.length(); i++) {
      returnChar[i + 2] = returnString[i];
    }
    pCharacteristic->setValue(returnChar, returnString.length() + 2);
  }

  pCharacteristic->indicate();
}