/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "Stepper.h"
#include "SS2KLog.h"
#include <Arduino.h>
#include <cctype>
#include <cstdlib>
#include <LittleFS.h>
#include <HardwareSerial.h>
#include "ERG_Mode.h"
#include "Power_Table.h"
#include "UdpAppender.h"
#include "WebsocketAppender.h"
#include "BleAppender.h"
#include "BLE_Custom_Characteristic.h"
#include "BLE_Definitions.h"
#include <Constants.h>
#include "settings.h"
// #include "BLE_Wattbike_Service.h"
#include "BLE_Fitness_Machine_Service.h"
#include "BLE_Zwift_Service.h"
#include "BLE_OpenBikeControl_Service.h"
#include "DirConManager.h"

// Peloton Serial
HardwareSerial auxSerial(1);
AuxSerialBuffer auxSerialBuffer;

TaskHandle_t maintenanceLoopTask;

Boards boards;
Board currentBoard;

///////////// Initialize the Config /////////////
ErgMode* ergMode            = new ErgMode;
PowerTable* powerTable      = new PowerTable;
SS2K* ss2k                  = new SS2K;
userParameters* userConfig  = new userParameters;
RuntimeParameters* rtConfig = new RuntimeParameters;

#ifndef UNIT_TEST
namespace {
RTC_NOINIT_ATTR bool inhibitLedOnBoot = false;

#ifdef SERIAL_CUSTOM_CHARACTERISTIC
constexpr size_t SERIAL_CUSTOM_CHARACTERISTIC_MAX_REQUEST = 128;

// Debug-only USB serial protocol:
//   cc 01 1e              read BLE_stepperSpeed
//   cc 02 1e b8 0b 00 00  write BLE_stepperSpeed (3000, little-endian)
// Each complete command is newline-delimited and replies as `cc_response` followed
// by the characteristic's raw response bytes in hexadecimal.
void processSerialCustomCharacteristic() {
  static char line[SERIAL_CUSTOM_CHARACTERISTIC_MAX_REQUEST * 3 + 1];
  static size_t lineLength = 0;

  while (Serial.available()) {
    const int received = Serial.read();
    if (received < 0) return;

    if (received == '\r') continue;
    if (received == '\n') {
      line[lineLength] = '\0';

      if (lineLength >= 2 && line[0] == 'c' && line[1] == 'c') {
        uint8_t request[SERIAL_CUSTOM_CHARACTERISTIC_MAX_REQUEST];
        size_t requestLength = 0;
        char* cursor = line + 2;
        bool valid = true;

        while (*cursor != '\0') {
          while (*cursor == ' ' || *cursor == '\t') cursor++;
          if (*cursor == '\0') break;
          if (requestLength == sizeof(request) || !isxdigit(static_cast<unsigned char>(cursor[0])) || !isxdigit(static_cast<unsigned char>(cursor[1]))) {
            valid = false;
            break;
          }

          char byteText[] = {cursor[0], cursor[1], '\0'};
          request[requestLength++] = static_cast<uint8_t>(strtoul(byteText, nullptr, 16));
          cursor += 2;
          if (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
            valid = false;
            break;
          }
        }

        if (valid && requestLength >= 2 && request[1] != BLE_allSettings) {
          BLE_ss2kCustomCharacteristic::process(std::string(reinterpret_cast<const char*>(request), requestLength), BLE_HS_CONN_HANDLE_NONE, 23, false);
          NimBLEService* service = NimBLEDevice::getServer()->getServiceByUUID(SMARTSPIN2K_SERVICE_UUID);
          NimBLECharacteristic* characteristic = service == nullptr ? nullptr : service->getCharacteristic(SMARTSPIN2K_CHARACTERISTIC_UUID);
          if (characteristic != nullptr) {
            NimBLEAttValue response = characteristic->getValue();
            Serial.print("cc_response");
            for (size_t i = 0; i < response.size(); ++i) {
              Serial.printf(" %02x", response[i]);
            }
            Serial.println();
          } else {
            Serial.println("cc_error unavailable");
          }
        } else {
          Serial.println("cc_error invalid_request");
        }
      }

      lineLength = 0;
      continue;
    }

    if (lineLength < sizeof(line) - 1) {
      line[lineLength++] = static_cast<char>(received);
    } else {
      lineLength = 0;
      Serial.println("cc_error request_too_long");
    }
  }
}
#endif

  // RTC memory survives commanded ESP.restart() calls but not power loss. A set
  // flag means the previous reboot was intentional, so consume it and keep the
  // LED quiet until activity turns it back on.
bool shouldStartWithLedEnabled() {
  bool inhibitLed = inhibitLedOnBoot;
  inhibitLedOnBoot = false;
  return !inhibitLed;
}

// Set a flag in RTC memory to keep the LED off on the next boot.
void keepLedOffAfterReboot() {
  inhibitLedOnBoot = true;
}
}  // namespace
#endif

///////////// Log Appender /////////////
UdpAppender udpAppender;
WebSocketAppender webSocketAppender;
BleAppender bleAppender;

///////////// BEGIN SETUP /////////////
#ifndef UNIT_TEST

void SS2K::startTasks() {
  SS2K_LOG(MAIN_LOG_TAG, "Start BLE + ERG Tasks");
  setupBLE();
}

void SS2K::stopTasks() {
  // In favor of stopping the tasks, BLE communications loop just disconnects all connected devices.
}

extern "C" void app_main() {
  initArduino();
  // Serial port for debugging purposes
  Serial.begin(115200);

  BaseType_t taskCreated = xTaskCreatePinnedToCore(SS2K::maintenanceLoop,     /* Task function. */
                                                   "maintenanceLoopFunction", /* name of task. */
                                                   MAIN_STACK,                /* Stack size of task */
                                                   NULL,                      /* parameter of the task */
                                                   10,                        /* priority of the task */
                                                   &maintenanceLoopTask,      /* task handle */
                                                   1);                        /* pin task to core */
  if (taskCreated != pdPASS) {
    Serial.println("Failed to create maintenance task; restarting");
    ESP.restart();
  }
}

void SS2K::finishSetup() {
  SS2K_LOG(MAIN_LOG_TAG, "Compiled %s%s", __DATE__, __TIME__);
#if defined(SMARTSPIN2K_S3)
  currentBoard = boards.rev3;
#else
  // Revisions one and two share the same hardware-detection pin.
  currentBoard = boards.rev1;
#endif
  pinMode(currentBoard.revisionPin, INPUT);
  int actualVoltage = analogRead(currentBoard.revisionPin);
#if defined(SMARTSPIN2K_S3)
  SS2K_LOG(MAIN_LOG_TAG, "Board ID ADC on GPIO%d: %d (expected %d +/- %d)", currentBoard.revisionPin, actualVoltage, currentBoard.versionVoltage,
           currentBoard.versionTolerance);
  if (abs(actualVoltage - currentBoard.versionVoltage) > currentBoard.versionTolerance) {
    SS2K_LOG(MAIN_LOG_TAG, "WARNING: Board ID resistor does not match the ESP32-S3 hardware revision");
  }
#else
  if (actualVoltage - boards.rev1.versionVoltage >= boards.rev2.versionVoltage - actualVoltage) {
    currentBoard = boards.rev2;
  } else {
    currentBoard = boards.rev1;
  }
#endif
  SS2K_LOG(MAIN_LOG_TAG, "Current Board Revision is: %s", currentBoard.name.c_str());

  // initialize Stepper serial port

  initializeStepperSerial();
  // initialize aux serial port (Peloton)
  if (currentBoard.auxSerialTxPin) {
    auxSerial.begin(19200, SERIAL_8N1, currentBoard.auxSerialRxPin, currentBoard.auxSerialTxPin, false);
    if (!auxSerial) {
      SS2K_LOG(MAIN_LOG_TAG, "Invalid Serial Pin Configuration");
    }
    auxSerial.onReceive(SS2K::rxSerial, true);  // setup callback
  }
  // Initialize LittleFS
  SS2K_LOG(MAIN_LOG_TAG, "Mounting Filesystem");
  if (!LittleFS.begin(false)) {
    SS2K_LOG(MAIN_LOG_TAG, "An Error has occurred while mounting LittleFS.");
    LittleFS.format();  // Format so that the settings can be saved.
    delay(100);         // Provide some time for the format to happen.
  }

  // Load Config
  userConfig->loadFromLittleFS();
  userConfig->printFile();  // Print userConfig->contents to serial
  userConfig->saveToLittleFS();

  // if we have homing data, use that instead.
  if (userConfig->getHMax() != INT32_MIN && userConfig->getHMin() != INT32_MIN) {
    SS2K_LOG(MAIN_LOG_TAG, "Using homing data from config file.");
    spinBLEServer.spinDownFlag = 1;
  }

  // print littleFS free space and all file sizes on partition
  Serial.printf("LittleFS Total Bytes:%lu, Used Bytes:%lu\n", LittleFS.totalBytes(), LittleFS.usedBytes());
#if defined(SMARTSPIN2K_S3)
  SS2K_LOG(MAIN_LOG_TAG, "S3 heap: %u bytes, PSRAM: %u bytes", ESP.getHeapSize(), ESP.getPsramSize());
  if (!psramFound()) {
    SS2K_LOG(MAIN_LOG_TAG, "WARNING: ESP32-S3 QSPI PSRAM was not detected");
  }
#endif

  // Finish WiFi and web filesystem repair before BLE and network services add
  // their runtime memory and traffic load.
  startWifi();
  httpServer.syncWebServerFiles();

  pinMode(currentBoard.shiftUpPin, INPUT_PULLUP);    // Push-Button with input Pullup
  pinMode(currentBoard.shiftDownPin, INPUT_PULLUP);  // Push-Button with input Pullup
  pinMode(currentBoard.ledPin, OUTPUT);
  pinMode(currentBoard.enablePin, OUTPUT);
  pinMode(currentBoard.dirPin, OUTPUT);   // Stepper Direction Pin
  pinMode(currentBoard.stepPin, OUTPUT);  // Stepper Step Pin
  digitalWrite(currentBoard.enablePin,
               HIGH);  // Should be called a disable Pin - High Disables FETs
  digitalWrite(currentBoard.dirPin, LOW);
  digitalWrite(currentBoard.stepPin, LOW);
  digitalWrite(currentBoard.ledPin, LOW);
  ss2k->setLEDEnabled(shouldStartWithLedEnabled());

  ss2k->setupTMCStepperDriver();

  SS2K_LOG(MAIN_LOG_TAG, "Setting up cpu Tasks");

  // disableCore0WDT();  // Disable the watchdog timer on core 0 (so long stepper
  //  moves don't cause problems)

  digitalWrite(currentBoard.ledPin, LOW);
  // Configure and Initialize Logger
  logHandler.addAppender(&webSocketAppender);
  logHandler.addAppender(&udpAppender);
  logHandler.addAppender(&bleAppender);
  logHandler.initialize();
  ss2k->startTasks();
  httpServer.start();

  SS2K_LOG(MAIN_LOG_TAG, "Starting DirCon TCP service");
  if (DirConManager::start()) {
    SS2K_LOG(MAIN_LOG_TAG, "DirCon TCP service started successfully");
  } else {
    SS2K_LOGE(MAIN_LOG_TAG, "Failed to start DirCon TCP service");
  }

#ifdef TEST_PTAB4PWR
  userConfig->setHMin(0);
  userConfig->setHMax(27000);
  rtConfig->setMaxStep(userConfig->getHMax());
  rtConfig->setMinStep(userConfig->getHMin());
  rtConfig->setHomed(true);
  userConfig->setPTab4Pwr(true);
  spinBLEServer.spinDownFlag = 0;
#endif

  ss2k->resetIfShiftersHeld();
  digitalWrite(currentBoard.ledPin, LOW);
}

void loop() {  // Delete this task so we can make one that's more memory efficient.
  vTaskDelete(NULL);
}

void SS2K::maintenanceLoop(void* pvParameters) {
  finishSetup();

  static unsigned long maintenanceTimer    = millis();
  static unsigned long riderStatusLogTimer = millis();
  static unsigned long rebootTimer         = millis();

  while (true) {
    delay(10);
    BLEFirmwareUpdateLoop();

#ifdef SERIAL_CUSTOM_CHARACTERISTIC
    processSerialCustomCharacteristic();
#endif

    // be quiet while updating via BLE
    if (!ss2k->isUpdating) {
      static unsigned long bleTimer = millis();
      // 500ms
      if ((millis() - bleTimer) > BLE_NOTIFY_DELAY) {
        BLECommunications();
        logHandler.writeLogs();
        webSocketAppender.Loop();
        bleTimer = millis();
      }
      // Don't do these if updating and in spindown mode.
      if (!spinBLEServer.spinDownFlag) {
        ss2k->moveStepper();
        ss2k->FTMSModeShiftModifier();
        ergMode->runERG();
      }
      // wattbikeService.parseNemit();

      // if this hardware version has serial pins, check and process their data.
      // only do this every AUX_SERIAL_DELAY
      static unsigned long auxSerialTimer = millis();
      if ((millis() - auxSerialTimer) > AUX_SERIAL_DELAY) {
        if (currentBoard.auxSerialTxPin) {
          ss2k->txSerial();
        }
        auxSerialTimer = millis();
      }
    }

    // Handle the shifters
    ss2k->handleShiftButtons();

    // send BLE notification for any userConfig values that changed.
    BLE_ss2kCustomCharacteristic::parseNemit();
    // Update Zwift Gear UI if shift happened

    httpServer.webClientUpdate();
    // Update DirCon protocol
    DirConManager::update();
    ss2k->updateLED();
    // If we're in ERG mode, modify shift commands to inc/dec the target watts instead.

    // If we have a resistance bike attached, slow down when we're close to the limits.
    if (stepper && ss2k->pelotonIsConnected && !rtConfig->getHomed() && !spinBLEServer.spinDownFlag) {
      int speed           = userConfig->getStepperSpeed();
      float resistance    = rtConfig->resistance.getValue();
      float maxResistance = rtConfig->getMaxResistance();

      // Slow down when resistance is within 20% of the lower limit
      if (resistance < (maxResistance * 0.2)) {
        float factor = resistance / (maxResistance * 0.2);
        speed        = static_cast<int>(factor * userConfig->getStepperSpeed());
        if (speed < 500) {
          speed = 500;
        }
        if (ss2k->targetPosition > stepper->getCurrentPosition()) {
          speed = userConfig->getStepperSpeed();
        }
      }

      // Slow down when resistance is within 20% of the upper limit
      if (resistance > (maxResistance * 0.8)) {
        float factor = (maxResistance - resistance) / (maxResistance * 0.2);
        speed        = static_cast<int>(factor * userConfig->getStepperSpeed());
        if (speed < 500) {
          speed = 500;
        }
        if (ss2k->targetPosition < stepper->getCurrentPosition()) {
          speed = userConfig->getStepperSpeed();
        }
      }
      ss2k->updateStepperSpeed(speed);
    }

    // Handle flag set for rebooting
    if (ss2k->rebootFlag) {
      static bool _loopOnce = false;
      delay(1000);
      // Let the main task loop complete once before rebooting
      if (_loopOnce) {
        // Important to keep this delay high in order to allow coms to finish.
        delay(1000);
        ESP.restart();
      }
      _loopOnce = true;
    }

    // Handle a flag set to reset SmartSpin2k to defaults
    if (ss2k->resetDefaultsFlag) {
      LittleFS.format();
      userConfig->setDefaults();
      powerTable->reset();
      userConfig->saveToLittleFS();
      ss2k->resetDefaultsFlag = false;
      ss2k->rebootFlag        = true;
    }

    // required to set a flag instead of directly calling the function for saving from BLE_Custom Characteristic.
    if (ss2k->saveFlag) {
      ss2k->saveFlag = false;
      userConfig->saveToLittleFS();
    }

    // Periodic maintenance.
    if ((millis() - maintenanceTimer) > MAIN_MAINTENANCE_INTERVAL_MS) {
      // Reboot after half an hour without meaningful pedaling. Also treat unchanged values as inactive because disconnected servers can leave stale readings behind.
      constexpr int inactivityThreshold             = 10;
      constexpr unsigned long inactivityRebootDelay = 1800000;
      static int oldHR                              = 0;
      static int oldWatts                           = 0;
      static int oldCadence                         = 0;
      static float oldTargetIncline                 = 0.0f;
      bool powerAndCadenceAreLow = rtConfig->watts.getValue() < inactivityThreshold && rtConfig->cad.getValue() < inactivityThreshold;
      bool readingsAreUnchanged = oldHR == rtConfig->hr.getValue() && oldWatts == rtConfig->watts.getValue() && oldCadence == rtConfig->cad.getValue() &&
                                  oldTargetIncline == rtConfig->getTargetIncline();
      bool riderIsInactive      = powerAndCadenceAreLow || readingsAreUnchanged;
      if (riderIsInactive) {
        if ((millis() - rebootTimer) > inactivityRebootDelay) {
          // Timer expired
          SS2K_LOG(MAIN_LOG_TAG, "Rebooting due to inactivity.");
          keepLedOffAfterReboot();
          ss2k->rebootFlag = true;
          logHandler.writeLogs();
          webSocketAppender.Loop();
        }
      } else {
        // Fresh active readings restart the full inactivity window.
        oldHR            = rtConfig->hr.getValue();
        oldWatts         = rtConfig->watts.getValue();
        oldCadence       = rtConfig->cad.getValue();
        oldTargetIncline = rtConfig->getTargetIncline();
        rebootTimer      = millis();
        ss2k->setLEDEnabled(true);
      }

#ifdef DEBUG_STACK
      if (!ss2k->isUpdating) {
        SS2K_LOG(MAIN_LOG_TAG, "Maintenance Task: %d", uxTaskGetStackHighWaterMark(maintenanceLoopTask));
        SS2K_LOG(MAIN_LOG_TAG, "BLEClient: %d", uxTaskGetStackHighWaterMark(BLEClientTask));
        SS2K_LOG(MAIN_LOG_TAG, "Min Heap: %d", esp_get_minimum_free_heap_size());
        SS2K_LOG(MAIN_LOG_TAG, "Free Heap: %d", esp_get_free_heap_size());
        SS2K_LOG(MAIN_LOG_TAG, "Best Block: %d", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
      }
#endif  // DEBUG_STACK
      // Connection state changes infrequently, so keep it out of the one-second rider status record.
      SS2K_LOG(MAIN_LOG_TAG, "DEV PM=%d CAD=%d HRM=%d", spinBLEClient.connectedPM, spinBLEClient.connectedCD, spinBLEClient.connectedHRM);
      maintenanceTimer = millis();
    }

    if ((millis() - riderStatusLogTimer) >= RIDER_STATUS_LOG_INTERVAL_MS) {
      // Log rider status for diagnostics and ride analysis.
      SS2K_LOG(MAIN_LOG_TAG, "W=%d C=%d H=%d G=%d R=%d P=%d->%d", rtConfig->watts.getValue(), rtConfig->cad.getValue(), rtConfig->hr.getValue(),
               rtConfig->getShifterPosition(), rtConfig->resistance.getValue(), ss2k->getCurrentPosition(), ss2k->getTargetPosition());

      riderStatusLogTimer = millis();
    }
  }
}

#endif  // UNIT_TEST

void SS2K::setLEDEnabled(bool enabled) {
  ledEnabled = enabled;
  if (!enabled) {
    digitalWrite(currentBoard.ledPin, LOW);
  }
}

void SS2K::updateLED() {
  if (!ledEnabled) {
    digitalWrite(currentBoard.ledPin, LOW);
    return;
  }

  int currentCount = spinBLEServer.connectedClientCount();
  if (currentCount == 0) {
    // No app/client connected yet: simple idle blink.
    digitalWrite(currentBoard.ledPin, (millis() / 500) % 2 == 0 ? LOW : HIGH);
    return;
  }

  // Connected: stay mostly on, then count clients as brief off-pulses.
  constexpr unsigned long pulseOffTime = 200;
  constexpr unsigned long pulseGap     = 300;
  constexpr unsigned long countGap     = 5000;
  unsigned long pulsePeriod            = pulseOffTime + pulseGap;
  unsigned long cycleDuration          = currentCount * pulsePeriod + countGap;
  unsigned long cyclePosition           = millis() % cycleDuration;

  // After the diagnostic pulses, return to solid-on connected status.
  if (cyclePosition >= currentCount * pulsePeriod) {
    digitalWrite(currentBoard.ledPin, HIGH);
    return;
  }

  // Each pulse starts with a short off dip, followed by on-time between dips.
  digitalWrite(currentBoard.ledPin, (cyclePosition % pulsePeriod) < pulseOffTime ? LOW : HIGH);
}

void SS2K::FTMSModeShiftModifier() {
  int shiftDelta = rtConfig->getShifterPosition() - ss2k->lastShifterPosition;
  if (shiftDelta) {  // Shift detected
    ss2k->setLEDEnabled(true);
    // When Zwift virtual shifting is active, forward shifts to Zwift
    // instead of handling them internally. Zwift sends gear changes
    // back via the custom trainer protocol which we already handle.
    // This needs to be moved so shift blocking/knob crashing prevention is enforced.
    // Keeping here for now for development/testing purposes

    int absDelta = abs(shiftDelta);
    if (zwiftService.isConnected()) {
      for (int i = 0; i < absDelta; i++) {
        if (shiftDelta > 0) {
          zwiftService.sendShiftUp();
        } else {
          zwiftService.sendShiftDown();
        }
      }
    }
    if (openBikeControlService.isConnected()) {
      for (int i = 0; i < absDelta; i++) {
        if (shiftDelta > 0) {
          openBikeControlService.sendShiftUp();
        } else {
          openBikeControlService.sendShiftDown();
        }
      }
    }
    switch (rtConfig->getFTMSMode()) {
      case FitnessMachineControlPointProcedure::SetTargetPower:  // ERG Mode
      {
        rtConfig->setShifterPosition(ss2k->lastShifterPosition);  // reset shifter position because we're remapping it to ERG target
        const int proposedTarget = rtConfig->watts.getTarget() + (shiftDelta * ERG_PER_SHIFT);
        const int minimumTarget  = rtConfig->getHomed() ? 0 : userConfig->getMinWatts();
        if (proposedTarget < minimumTarget || proposedTarget > userConfig->getMaxWatts()) {
          SS2K_LOG(MAIN_LOG_TAG, "Shift to %dw blocked", proposedTarget);
          break;
        }
        rtConfig->watts.setTarget(proposedTarget);
        SS2K_LOG(MAIN_LOG_TAG, "ERG Shift. New Target: %dw", rtConfig->watts.getTarget());
// Format output for FTMS passthrough
#ifndef INTERNAL_ERG_4EXT_FTMS
        int adjustedTarget         = round(rtConfig->watts.getTarget() / userConfig->getPowerCorrectionFactor());
        const uint8_t translated[] = {FitnessMachineControlPointProcedure::SetTargetPower, (uint8_t)(adjustedTarget & 0xff), (uint8_t)(adjustedTarget >> 8)};
        spinBLEClient.FTMSControlPointWrite(translated, 3);
#endif
        break;
      }

      case FitnessMachineControlPointProcedure::SetTargetResistanceLevel:  // Resistance Mode
      {
        rtConfig->setShifterPosition(ss2k->lastShifterPosition);  // reset shifter position because we're remapping it to resistance target
        if (pelotonIsConnected) {
          if (rtConfig->resistance.getTarget() + shiftDelta < rtConfig->getMinResistance()) {
            rtConfig->resistance.setTarget(rtConfig->getMinResistance());
            SS2K_LOG(MAIN_LOG_TAG, "Resistance shift less than min %d", rtConfig->getMinResistance());
            break;
          } else if (rtConfig->resistance.getTarget() + shiftDelta > rtConfig->getMaxResistance()) {
            rtConfig->resistance.setTarget(rtConfig->getMaxResistance());
            SS2K_LOG(MAIN_LOG_TAG, "Resistance shift exceeded max %d", rtConfig->getMaxResistance());
            break;
          }
          rtConfig->resistance.setTarget(rtConfig->resistance.getTarget() + shiftDelta);
          SS2K_LOG(MAIN_LOG_TAG, "Resistance Shift. New Target: %d", rtConfig->resistance.getTarget());
        }
        break;
      }

      default:  // Sim Mode
      {
        SS2K_LOG(MAIN_LOG_TAG, "Shift %+d pos %d tgt %d min %d max %d r_min %d r_max %d", shiftDelta, rtConfig->getShifterPosition(), ss2k->getTargetPosition(),
                 rtConfig->getMinStep(), rtConfig->getMaxStep(), rtConfig->getMinResistance(), rtConfig->getMaxResistance());
        // Block Shifts further out of bounds
        if (((ss2k->targetPosition + shiftDelta * userConfig->getShiftStep()) < rtConfig->getMinStep()) && (shiftDelta < 0)) {
          SS2K_LOG(MAIN_LOG_TAG, "Shift Blocked by stepper limits.");
          rtConfig->setShifterPosition(ss2k->lastShifterPosition);
        } else if ((ss2k->targetPosition + shiftDelta * userConfig->getShiftStep()) > rtConfig->getMaxStep() && (shiftDelta > 0)) {
          SS2K_LOG(MAIN_LOG_TAG, "Shift Blocked by stepper limits.");
          rtConfig->setShifterPosition(ss2k->lastShifterPosition);
        } else if (rtConfig->getHomed()) {
          // was homed. Allow because previous test would have failed if out of bounds.
        } else if ((rtConfig->resistance.getValue() <= rtConfig->getMinResistance()) && (shiftDelta > 0)) {
          // User Shifted in the proper direction - allow
        } else if ((rtConfig->resistance.getValue() >= rtConfig->getMaxResistance()) && (shiftDelta < 0)) {
          // User Shifted in the proper direction - allow
        } else if ((rtConfig->resistance.getValue() > rtConfig->getMinResistance()) && (rtConfig->resistance.getValue() < rtConfig->getMaxResistance())) {
          // User Shifted in bounds - allow
        } else {
          // User tried shifting further into the limit - block.
          SS2K_LOG(MAIN_LOG_TAG, "Shift Blocked by resistance limit.");
          rtConfig->setShifterPosition(ss2k->lastShifterPosition);
        }
        uint8_t _controlData[] = {FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters, 0x00, 0x00, 0x00, 0x00, 0x28, 0x33};
        spinBLEClient.FTMSControlPointWrite(_controlData, 7);
      }
    }
    ss2k->lastShifterPosition = rtConfig->getShifterPosition();
    BLE_ss2kCustomCharacteristic::notify(BLE_shifterPosition);
  }
}

void SS2K::restartWifi() {
  httpServer.stop();
  delay(100);
  stopWifi();
  delay(100);
  startWifi();
  refreshBLEAdvertisementIp();
  httpServer.start();
  if (!DirConManager::start()) {
    SS2K_LOGE(MAIN_LOG_TAG, "Failed to restart DirCon TCP service");
  }
}

void SS2K::handleShiftButtons() {
  int upButtonIsPressed   = !digitalRead(currentBoard.shiftUpPin);
  int downButtonIsPressed = !digitalRead(currentBoard.shiftDownPin);

  // --- UP Button State Machine ---
  if (upButtonIsPressed && ss2k->upButtonState == RELEASED) {
    if (millis() - ss2k->lastDebounceTime > DEBOUNCE_DELAY) {
      // It's a valid press, take action!
      rtConfig->setShifterPosition(rtConfig->getShifterPosition() - 1 + userConfig->getShifterDir() * 2);
      ss2k->lastDebounceTime = millis();
    }
    ss2k->upButtonState = PRESSED;

  } else if (!upButtonIsPressed && ss2k->upButtonState == PRESSED) {
    // The button was pressed, but now it's not. Update the state.
    ss2k->upButtonState = RELEASED;
  }

  // --- DOWN Button State Machine ---
  if (downButtonIsPressed && ss2k->downButtonState == RELEASED) {
    if (millis() - ss2k->lastDebounceTime > DEBOUNCE_DELAY) {
      rtConfig->setShifterPosition(rtConfig->getShifterPosition() + 1 - userConfig->getShifterDir() * 2);
      ss2k->lastDebounceTime = millis();
    }
    ss2k->downButtonState = PRESSED;

  } else if (!downButtonIsPressed && ss2k->downButtonState == PRESSED) {
    ss2k->downButtonState = RELEASED;
  }
}

void SS2K::resetIfShiftersHeld() {
  if ((digitalRead(currentBoard.shiftUpPin) == LOW) && (digitalRead(currentBoard.shiftDownPin) == LOW)) {
    SS2K_LOG(MAIN_LOG_TAG, "Resetting to defaults via shifter buttons.");
    for (int x = 0; x < 10; x++) {  // blink fast to acknowledge
      digitalWrite(currentBoard.ledPin, HIGH);
      delay(200);
      digitalWrite(currentBoard.ledPin, LOW);
    }
    for (int i = 0; i < 20; i++) {
      LittleFS.format();
      userConfig->setDefaults();
      delay(200);
      userConfig->saveToLittleFS();
      delay(200);
    }
    ESP.restart();
  }
}

void SS2K::txSerial() {  // Serial.printf(" Before TX ");
  if (PELOTON_TX && (txCheck >= 1)) {
    static int alternate = 0;
    byte buf[4]          = {PELOTON_REQUEST, 0x00, 0x00, PELOTON_FOOTER};
    switch (alternate) {
      case 0:
        buf[PELOTON_REQ_POS] = PELOTON_POW_ID;
        alternate++;
        break;
      case 1:
        buf[PELOTON_REQ_POS] = PELOTON_CAD_ID;
        alternate++;
        break;
      case 2:
        buf[PELOTON_REQ_POS] = PELOTON_RES_ID;
        alternate            = 0;
        txCheck--;
        break;
    }
    buf[PELOTON_CHECKSUM_POS] = (buf[0] + buf[1]) % 256;
    if (auxSerial.availableForWrite() >= PELOTON_RQ_SIZE) {
      auxSerial.write(buf, PELOTON_RQ_SIZE);
    }
  } else if (PELOTON_TX && txCheck <= 0) {
    if (txCheck == 0) {
      txCheck = -TX_CHECK_INTERVAL;
    } else if (txCheck == -1) {
      txCheck = 1;
    }
    pelotonIsConnected = false;
    // Peloton serial shares the global power/cadence connection flags with BLE sensors.
    // Only release them when no specific BLE power meter is configured, matching the
    // Peloton coexistence checks in collectAndSet().
    if (strcmp(userConfig->getConnectedPowerMeter(), NONE) == 0 || strcmp(userConfig->getConnectedPowerMeter(), ANY) == 0) {
      spinBLEClient.connectedPM = false;
      spinBLEClient.connectedCD = false;
    }
    rtConfig->setMinResistance(-DEFAULT_RESISTANCE_RANGE);
    rtConfig->setMaxResistance(DEFAULT_RESISTANCE_RANGE);
    txCheck++;
  }
}
bool SS2K::pelotonConnected() {
  txCheck = TX_CHECK_INTERVAL;
  if (millis() - rtConfig->resistance.getTimestamp() < 5000 && !rtConfig->resistance.getSimulate()) {
    rtConfig->setMinResistance(MIN_PELOTON_RESISTANCE);
    rtConfig->setMaxResistance(MAX_PELOTON_RESISTANCE);
    return true;
  } else {
    rtConfig->setMinResistance(-DEFAULT_RESISTANCE_RANGE);
    rtConfig->setMaxResistance(DEFAULT_RESISTANCE_RANGE);
    return false;
  }
}

void SS2K::rxSerial(void) {
  while (auxSerial.available()) {
    ss2k->pelotonConnected();
    auxSerialBuffer.len = auxSerial.readBytesUntil(PELOTON_FOOTER, auxSerialBuffer.data, AUX_BUF_SIZE);
    for (int i = 0; i < auxSerialBuffer.len; i++) {  // Find start of data string
      if (auxSerialBuffer.data[i] == PELOTON_HEADER) {
        ss2k->pelotonIsConnected = true;
        size_t newLen            = auxSerialBuffer.len - i;  // find length of sub data
        uint8_t newBuf[newLen];
        for (int j = i; j < auxSerialBuffer.len; j++) {
          newBuf[j - i] = auxSerialBuffer.data[j];
        }
        std::string uniqueName = "Peloton";
        collectAndSet(PELOTON_DATA_UUID, PELOTON_DATA_UUID, uniqueName, newBuf, newLen);
      }
    }
  }
}
