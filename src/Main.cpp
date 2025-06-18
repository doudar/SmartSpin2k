/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Main.h"
#include "SS2KLog.h"
#include <TMCStepper.h>
#include <Arduino.h>
#include <LittleFS.h>
#include <HardwareSerial.h>
#include "FastAccelStepper.h"
#include "ERG_Mode.h"
#include "Power_Table.h"
#include "UdpAppender.h"
#include "WebsocketAppender.h"
#include "BLE_Custom_Characteristic.h"
#include <Constants.h>
#include "settings.h"
// #include "BLE_Wattbike_Service.h"
#include "BLE_Fitness_Machine_Service.h"
#include "DirConManager.h"

// Stepper Motor Serial
HardwareSerial stepperSerial(2);
TMC2209Stepper driver(&SERIAL_PORT, R_SENSE, 0b00);  // Hardware Serial

// Peloton Serial
HardwareSerial auxSerial(1);
AuxSerialBuffer auxSerialBuffer;

FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper     = NULL;

TaskHandle_t maintenanceLoopTask;

Boards boards;
Board currentBoard;

///////////// Initialize the Config /////////////
ErgMode *ergMode                 = new ErgMode;
PowerTable *powerTable           = new PowerTable;
SS2K *ss2k                       = new SS2K;
userParameters *userConfig       = new userParameters;
RuntimeParameters *rtConfig      = new RuntimeParameters;

///////////// Log Appender /////////////
UdpAppender udpAppender;
WebSocketAppender webSocketAppender;

///////////// BEGIN SETUP /////////////
#ifndef UNIT_TEST

void SS2K::startTasks() {
  SS2K_LOG(MAIN_LOG_TAG, "Start BLE + ERG Tasks");
  spinBLEClient.intentionalDisconnect = 0;
  setupBLE();
}

void SS2K::stopTasks() {
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Shutting Down all BLE services");
  spinBLEClient.reconnectTries        = 0;
  spinBLEClient.intentionalDisconnect = NUM_BLE_DEVICES;
  if (NimBLEDevice::isInitialized()) {
    NimBLEDevice::deinit();
    ss2k->stopTasks();
  }
  SS2K_LOG(MAIN_LOG_TAG, "Stop BLE + ERG Tasks");
  if (BLEClientTask != NULL) {
    vTaskDelete(BLEClientTask);
    BLEClientTask = NULL;
  }
}

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);
  SS2K_LOG(MAIN_LOG_TAG, "Compiled %s%s", __DATE__, __TIME__);
  pinMode(REV_PIN, INPUT);
  int actualVoltage = analogRead(REV_PIN);
  if (actualVoltage - boards.rev1.versionVoltage >= boards.rev2.versionVoltage - actualVoltage) {
    currentBoard = boards.rev2;
  } else {
    currentBoard = boards.rev1;
  }
  SS2K_LOG(MAIN_LOG_TAG, "Current Board Revision is: %s", currentBoard.name);

  // initialize Stepper serial port

  stepperSerial.begin(57600, SERIAL_8N2, currentBoard.stepperSerialRxPin, currentBoard.stepperSerialTxPin);
  // initialize aux serial port (Peloton)
  if (currentBoard.auxSerialTxPin) {
    auxSerial.begin(19200, SERIAL_8N1, currentBoard.auxSerialRxPin, currentBoard.auxSerialTxPin, false);
    if (!auxSerial) {
      SS2K_LOG(MAIN_LOG_TAG, "Invalid Serial Pin Configuration");
    }
    auxSerial.onReceive(SS2K::rxSerial, false);  // setup callback
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

  // Check for firmware update. It's important that this stays before BLE &
  // HTTP setup because otherwise they use too much traffic and the device
  // fails to update which really sucks when it corrupts your settings.
  startWifi();
  httpServer.FirmwareUpdate();

  pinMode(currentBoard.shiftUpPin, INPUT_PULLUP);    // Push-Button with input Pullup
  pinMode(currentBoard.shiftDownPin, INPUT_PULLUP);  // Push-Button with input Pullup
  pinMode(LED_PIN, OUTPUT);
  pinMode(currentBoard.enablePin, OUTPUT);
  pinMode(currentBoard.dirPin, OUTPUT);   // Stepper Direction Pin
  pinMode(currentBoard.stepPin, OUTPUT);  // Stepper Step Pin
  digitalWrite(currentBoard.enablePin,
               HIGH);  // Should be called a disable Pin - High Disables FETs
  digitalWrite(currentBoard.dirPin, LOW);
  digitalWrite(currentBoard.stepPin, LOW);
  digitalWrite(LED_PIN, LOW);

  ss2k->setupTMCStepperDriver();

  SS2K_LOG(MAIN_LOG_TAG, "Setting up cpu Tasks");

  // disableCore0WDT();  // Disable the watchdog timer on core 0 (so long stepper
  //  moves don't cause problems)

  digitalWrite(LED_PIN, HIGH);
  Serial.println("Starting BLE");
  // Configure and Initialize Logger
  logHandler.addAppender(&webSocketAppender);
  Serial.println("Starting BLE");
  logHandler.addAppender(&udpAppender);
  Serial.println("Starting BLE");
  logHandler.initialize();
  Serial.println("Starting BLE");
  ss2k->startTasks();
  httpServer.start();

  // Start DirCon TCP server for direct control over the bike trainer
  SS2K_LOG(MAIN_LOG_TAG, "Starting DirCon TCP service");
  if (DirConManager::start()) {
    SS2K_LOG(MAIN_LOG_TAG, "DirCon TCP service started successfully");
  } else {
    SS2K_LOG(MAIN_LOG_TAG, "Failed to start DirCon TCP service");
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
  SS2K_LOG(MAIN_LOG_TAG, "Creating Shifter Interrupts");
  // Setup Interrupts so shifters work anytime
  attachInterrupt(digitalPinToInterrupt(currentBoard.shiftUpPin), ss2k->handleShift, CHANGE);
  attachInterrupt(digitalPinToInterrupt(currentBoard.shiftDownPin), ss2k->handleShift, CHANGE);
  digitalWrite(LED_PIN, HIGH);

  xTaskCreatePinnedToCore(SS2K::maintenanceLoop,     /* Task function. */
                          "maintenanceLoopFunction", /* name of task. */
                          MAIN_STACK,                /* Stack size of task */
                          NULL,                      /* parameter of the task */
                          2,                         /* priority of the task */
                          &maintenanceLoopTask,      /* Task handle to keep track of created task */
                          0);                        /* pin task to core */
}

void loop() {  // Delete this task so we can make one that's more memory efficient.
  vTaskDelete(NULL);
}

void SS2K::maintenanceLoop(void *pvParameters) {
  static unsigned long intervalTimer2 = millis();
  static unsigned long rebootTimer    = millis();

  while (true) {
    delay(5);

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
        powerTable->fillTable();
      }
      // wattbikeService.parseNemit();
    }

    // send BLE notification for any userConfig values that changed.
    BLE_ss2kCustomCharacteristic::parseNemit();
    // Update Zwift Gear UI if shift happened

    httpServer.webClientUpdate();
    // Update DirCon protocol
    DirConManager::update();
    // If we're in ERG mode, modify shift commands to inc/dec the target watts instead.

    // If we have a resistance bike attached, slow down when we're close to the limits.
    if (ss2k->pelotonIsConnected && !rtConfig->getHomed() && !spinBLEServer.spinDownFlag) {
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

    // if this hardware version has serial pins, check and process their data.
    if (currentBoard.auxSerialTxPin) {
      ss2k->txSerial();
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

    // Things to do every 6 seconds
    if ((millis() - intervalTimer2) > 6007) {
      // reboot every half hour if not in use.
      static int _oldHR               = 0;
      static int _oldWatts            = 0;
      static double _oldTargetIncline = 0;
      if (_oldHR == rtConfig->hr.getValue() && _oldWatts == rtConfig->watts.getValue() && _oldTargetIncline == rtConfig->getTargetIncline() &&
          NimBLEDevice::getServer()->getConnectedCount() == 0) {
        // Inactivity detected
        if (((millis() - rebootTimer) > 1800000)) {
          // Timer expired
          SS2K_LOG(MAIN_LOG_TAG, "Rebooting due to inactivity.");
          ss2k->rebootFlag = true;
          logHandler.writeLogs();
          webSocketAppender.Loop();
        }

      } else {
        // We have activity, update monitored values
        _oldHR            = rtConfig->hr.getValue();
        _oldWatts         = rtConfig->watts.getValue();
        _oldTargetIncline = rtConfig->getTargetIncline();
        rebootTimer       = millis();
      }

#ifdef DEBUG_STACK
      SS2K_LOG(MAIN_LOG_TAG, "Main Task: %d", uxTaskGetStackHighWaterMark(maintenanceLoopTask));
      SS2K_LOG(MAIN_LOG_TAG, "BLEClient: %d", uxTaskGetStackHighWaterMark(BLEClientTask));
      SS2K_LOG(MAIN_LOG_TAG, "Min Heap: %d", esp_get_minimum_free_heap_size());
      SS2K_LOG(MAIN_LOG_TAG, "Free Heap: %d", esp_get_free_heap_size());
      SS2K_LOG(MAIN_LOG_TAG, "Best Block: %d", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#endif  // DEBUG_STACK
      // Log userParameters
      SS2K_LOG(MAIN_LOG_TAG, "PM Con %d, HRM Con %d, W %d, Cad %d, HR %d, Gear %d, Target Position %d", spinBLEClient.connectedPM, spinBLEClient.connectedHRM,
               rtConfig->watts.getValue(), rtConfig->cad.getValue(), rtConfig->hr.getValue(), rtConfig->getShifterPosition(), ss2k->targetPosition);

      intervalTimer2 = millis();
    }
  }
}

#endif  // UNIT_TEST

void SS2K::FTMSModeShiftModifier() {
  int shiftDelta = rtConfig->getShifterPosition() - ss2k->lastShifterPosition;
  if (shiftDelta) {  // Shift detected
    switch (rtConfig->getFTMSMode()) {
      case FitnessMachineControlPointProcedure::SetTargetPower:  // ERG Mode
      {
        rtConfig->setShifterPosition(ss2k->lastShifterPosition);  // reset shifter position because we're remapping it to ERG target
        if ((rtConfig->watts.getTarget() + (shiftDelta * ERG_PER_SHIFT) < userConfig->getMinWatts()) ||
            (rtConfig->watts.getTarget() + (shiftDelta * ERG_PER_SHIFT) > userConfig->getMaxWatts())) {
          SS2K_LOG(MAIN_LOG_TAG, "Shift to %dw blocked", rtConfig->watts.getTarget() + shiftDelta);
          break;
        }
        rtConfig->watts.setTarget(rtConfig->watts.getTarget() + (ERG_PER_SHIFT * shiftDelta));
        SS2K_LOG(MAIN_LOG_TAG, "ERG Shift. New Target: %dw", rtConfig->watts.getTarget());
// Format output for FTMS passthrough
#ifndef INTERNAL_ERG_4EXT_FTMS
        int adjustedTarget         = rtConfig->watts.getTarget() / userConfig->getPowerCorrectionFactor();
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
        SS2K_LOG(MAIN_LOG_TAG, "Shift %+d pos %d tgt %d min %d max %d r_min %d r_max %d", shiftDelta, rtConfig->getShifterPosition(), ss2k->targetPosition, rtConfig->getMinStep(),
                 rtConfig->getMaxStep(), rtConfig->getMinResistance(), rtConfig->getMaxResistance());
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
  httpServer.start();
}

void SS2K::moveStepper() {
  bool _stepperDir = userConfig->getStepperDir();
  if (stepper) {
    ss2k->stepperIsRunning = stepper->isRunning();
    ss2k->currentPosition  = stepper->getCurrentPosition();
    if (!ss2k->externalControl) {
      if ((rtConfig->getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetPower)) {
#ifdef ERG_GUARDRAILS
        // don't drive lower out of bounds. This is a final test that should never happen.
        if ((stepper->getCurrentPosition() > rtConfig->getTargetIncline()) && (rtConfig->watts.getValue() < rtConfig->watts.getTarget())) {
          rtConfig->setTargetIncline(stepper->getCurrentPosition() + 1);
        }
        // don't drive higher out of bounds. This is a final test that should never happen.
        if ((stepper->getCurrentPosition() < rtConfig->getTargetIncline()) && (rtConfig->watts.getValue() > rtConfig->watts.getTarget())) {
          rtConfig->setTargetIncline(stepper->getCurrentPosition() - 1);
        }
#endif
        ss2k->targetPosition = rtConfig->getTargetIncline();
      } else if (rtConfig->getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetResistanceLevel) {
        rtConfig->setTargetIncline(ss2k->currentPosition + ((rtConfig->resistance.getTarget() - rtConfig->resistance.getValue()) * 20));
        ss2k->targetPosition = rtConfig->getTargetIncline();
      } else {
        // Simulation Mode
        ss2k->targetPosition = rtConfig->getShifterPosition() * userConfig->getShiftStep();
        ss2k->targetPosition += rtConfig->getTargetIncline() * userConfig->getInclineMultiplier();
      }
    } else {
      // periodically log external control message
      static long int lastTime = millis();
      if (millis() - lastTime > 5000) {
        SS2K_LOG(MAIN_LOG_TAG, "External Control Mode");
        lastTime = millis();
      }
    }

    if (ss2k->syncMode) {
      stepper->stopMove();
      SS2K_LOG(MAIN_LOG_TAG, "Sync Mode");
      stepper->setCurrentPosition(ss2k->targetPosition);
      ss2k->syncMode = false;
    }

    if (ss2k->pelotonIsConnected && !rtConfig->getHomed()) {
      if ((rtConfig->resistance.getValue() > rtConfig->getMinResistance()) && (rtConfig->resistance.getValue() < rtConfig->getMaxResistance())) {
        stepper->moveTo(ss2k->targetPosition);
      } else if (rtConfig->resistance.getValue() <= rtConfig->getMinResistance()) {  // Limit Stepper to Min Resistance
        if (rtConfig->resistance.getValue() != rtConfig->getMinResistance()) {
          stepper->moveTo(stepper->getCurrentPosition() + 20);
        }
        // Let the user Shift Out of this Position
        if (ss2k->targetPosition > stepper->getCurrentPosition()) {
          stepper->moveTo(ss2k->targetPosition);
        }
      } else {  // Limit Stepper to Max Resistance
        if (rtConfig->resistance.getValue() != rtConfig->getMaxResistance()) {
          stepper->moveTo(stepper->getCurrentPosition() - 20);
        }
        // Let the user Shift Out of this Position
        if (ss2k->targetPosition < stepper->getCurrentPosition()) {
          stepper->moveTo(ss2k->targetPosition);
        }
      }
    } else {  // Normal move code for non-Peloton
      if ((ss2k->targetPosition >= rtConfig->getMinStep()) && (ss2k->targetPosition <= rtConfig->getMaxStep())) {
        stepper->moveTo(ss2k->targetPosition);
      } else if (ss2k->targetPosition <= rtConfig->getMinStep()) {  // Limit Stepper to Min Position
        stepper->moveTo(rtConfig->getMinStep() + 1);
      } else {  // Limit Stepper to Max Position
        stepper->moveTo(rtConfig->getMaxStep() - 1);
      }
    }

    if (rtConfig->cad.getValue() > 1) {
      stepper->enableOutputs();
      stepper->setAutoEnable(false);
    } else {
      stepper->setAutoEnable(true);
    }

    if (_stepperDir != userConfig->getStepperDir()) {  // User changed the config direction of the stepper wires
      _stepperDir = userConfig->getStepperDir();
      while (stepper->isRunning()) {  // Wait until the motor stops running
        delay(100);
      }
      stepper->setDirectionPin(currentBoard.dirPin, _stepperDir);
    }
  }
}

bool SS2K::deBounce() {
  if ((millis() - ss2k->lastDebounceTime) >
      ss2k->debounceDelay) {  // <----------------This should be assigned it's own task and just switch a global bool whatever the reading is at, it's
                              // been there for longer than the debounce delay, so take it as the actual current state: if the button state has changed:
    ss2k->lastDebounceTime = millis();
    return true;
  }
  return false;
}

///////////// Interrupt Functions /////////////
void ARDUINO_ISR_ATTR SS2K::handleShift() {  // Handle the shift up interrupt IRAM_ATTR is to keep the interrupt code in ram always
  if (ss2k->deBounce()) {
    if (!digitalRead(currentBoard.shiftUpPin)) {  // double checking to make sure the interrupt wasn't triggered by emf
      rtConfig->setShifterPosition(rtConfig->getShifterPosition() - 1 + userConfig->getShifterDir() * 2);
      // Stop homing initiation
      spinBLEServer.spinDownFlag = 0;
    } else if (!digitalRead(currentBoard.shiftDownPin)) {  // double checking to make sure the interrupt wasn't triggered by emf
      rtConfig->setShifterPosition(rtConfig->getShifterPosition() + 1 - userConfig->getShifterDir() * 2);
      // Stop homing initiation
      spinBLEServer.spinDownFlag = 0;
    } else {
      ss2k->lastDebounceTime = 0;
    }  // Probably Triggered by EMF, reset the debounce
  }
}

void SS2K::resetIfShiftersHeld() {
  if ((digitalRead(currentBoard.shiftUpPin) == LOW) && (digitalRead(currentBoard.shiftDownPin) == LOW)) {
    SS2K_LOG(MAIN_LOG_TAG, "Resetting to defaults via shifter buttons.");
    for (int x = 0; x < 10; x++) {  // blink fast to acknowledge
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
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

void SS2K::setupTMCStepperDriver(bool reset) {
  // FastAccel setup
  if (!reset) {
    engine.init();
    stepper = engine.stepperConnectToPin(currentBoard.stepPin);
    stepper->setDirectionPin(currentBoard.dirPin, userConfig->getStepperDir());
    stepper->setEnablePin(currentBoard.enablePin);
    stepper->setAutoEnable(true);
    stepper->setSpeedInHz(DEFAULT_STEPPER_SPEED);
    stepper->setAcceleration(STEPPER_ACCELERATION);
    stepper->setDelayToDisable(65535);
    // TMC Driver Setup
    driver.begin();
  }

  driver.pdn_disable(true);       // Use PDN pin to enable UART communication instead of grounding signal
  driver.mstep_reg_select(true);  // Use register instead of ms1&ms2 pins for microstep selection
  driver.microsteps(4);           // Set microsteps to 1/8th
  driver.iholddelay(10);          // Controls the number of clock cycles for motor power down after standstill is detected
  driver.TPOWERDOWN(128);         // delay until hold current
  driver.toff(5);                 // needs >0 for driver enable. 1-15 controls duration of slow decay phase of pwm.
  this->updateStealthChop();
  driver.irun(currentBoard.pwrScaler);
  driver.ihold((uint8_t)(currentBoard.pwrScaler * .1));  // hold current % 0-DRIVER_MAX_PWR_SCALER
  this->updateStepperSpeed();
  this->updateStepperPower();
  this->setCurrentPosition(stepper->getCurrentPosition());
}

void SS2K::goHome(bool bothDirections) {
  if (stepper) {
    if (currentBoard.name != r2_NAME) {
      SS2K_LOG(MAIN_LOG_TAG, "Board Doesn't support homing");
      fitnessMachineService.spinDown(0x02);
      return;
    }
    SS2K_LOG(MAIN_LOG_TAG, "Homing...");
    SS2K_LOG(MAIN_LOG_TAG, "Updating driver...");
    fitnessMachineService.spinDown(0x01);
    updateStepperPower(userConfig->getStepperPower() * .2);
    delay(50);
    driver.irun(0x02);  // low power
    delay(50);
    driver.ihold(0x01);
    delay(50);
    int threshold = 0;
    bool stalled  = false;
    // Back off limit in case we are alread here.
    stepper->move(userConfig->getShiftStep(), true);
    this->updateStepperSpeed(1500);
    delay(500);
    stepper->runBackward();
    delay(250);
    threshold = driver.SG_RESULT();
    Serial.printf("%d ", driver.SG_RESULT());
    delay(300);
    fitnessMachineService.spinDown(0x04);
    while (!stalled) {
      if (abs(rtConfig->getShifterPosition() - ss2k->lastShifterPosition)) {  // let the user abort with the shift button.
        userConfig->setHMin(INT32_MIN);
        userConfig->setHMax(INT32_MIN);
        return;
      }
      stalled = (driver.SG_RESULT() < threshold - userConfig->getHomingSensitivity());
    }
    stepper->forceStop();
    delay(100);
    stepper->moveTo(stepper->getCurrentPosition() + userConfig->getShiftStep());
    while (stepper->isRunning()) {
      delay(10);
    }
    stepper->setCurrentPosition((int32_t)0);
    ss2k->setTargetPosition(0);
    rtConfig->setMinStep(0);
    SS2K_LOG(MAIN_LOG_TAG, "Min Position found: %d.", rtConfig->getMinStep());
    stalled = false;
    fitnessMachineService.spinDown(0x02);
    if (bothDirections) {
      // Back off limit in case we are alread here.
      this->updateStepperSpeed(1500);
      delay(500);
      stepper->runForward();
      delay(1000);                     // wait until stable
      threshold = driver.SG_RESULT();  // take reading
      Serial.printf("%d ", driver.SG_RESULT());
      delay(250);
      while (!stalled) {
        if (abs(rtConfig->getShifterPosition() - ss2k->lastShifterPosition)) {  // let the user abort with the shift button.
          userConfig->setHMin(INT32_MIN);
          userConfig->setHMax(INT32_MIN);
          return;
        }
        stalled = (driver.SG_RESULT() < threshold - userConfig->getHomingSensitivity());
      }
      stepper->forceStop();
      fitnessMachineService.spinDown(0x02);
      delay(500);
      rtConfig->setMaxStep(stepper->getCurrentPosition() - 200);
      SS2K_LOG(MAIN_LOG_TAG, "Max Position found: %d.", rtConfig->getMaxStep());
      this->updateStepperSpeed();
      stepper->moveTo(0, true);
    }
  }
  fitnessMachineService.spinDown(0x02);
  // Start Saving Settings
  if (bothDirections) {
    userConfig->setHMin(rtConfig->getMinStep());
    userConfig->setHMax(rtConfig->getMaxStep());
  }
  // In case this was only one direction homing.
  rtConfig->setMaxStep(userConfig->getHMax());
  userConfig->saveToLittleFS();
  rtConfig->setHomed(true);
  this->setupTMCStepperDriver(true);
  rtConfig->setShifterPosition(0);
  ss2k->setTargetPosition(0);
}

// Applies current power to driver
void SS2K::updateStepperPower(int pwr) {
  uint16_t rmsPwr = (pwr == 0) ? userConfig->getStepperPower() : pwr;
  driver.rms_current(rmsPwr);
  uint16_t current = driver.cs_actual();
  SS2K_LOG(MAIN_LOG_TAG, "Stepper power is now %d.  read:cs=%U", rmsPwr, current);
}

// Applies current StealthChop to driver
void SS2K::updateStealthChop() {
  bool t_bool = userConfig->getStealthChop();
  driver.en_spreadCycle(!t_bool);
  driver.pwm_autoscale(t_bool);
  driver.pwm_autograd(t_bool);
  SS2K_LOG(MAIN_LOG_TAG, "StealthChop is now %d", t_bool);
}

// Applies userconfig stepper speed if speed not specified
/**
 * @brief Updates the speed of the stepper motor.
 *
 * This function updates the speed of the stepper motor to the specified value.
 * If the provided speed is 0, it retrieves the speed from the user configuration.
 * The function also includes a tolerance check to avoid unnecessary updates if
 * the current speed is within 5 units of the target speed.
 *
 * @param speed The desired speed for the stepper motor. If 0, the speed is retrieved from user configuration.
 */
void SS2K::updateStepperSpeed(int speed) {
  if (speed == 0) {
    speed = userConfig->getStepperSpeed();
  }
  int s = stepper->getSpeedInMilliHz() / 1000;
  // Because the conversion to/from the TMC driver is not perfect, we need to allow a little bit of slop.
  // Skip the update if the speed is within 5 of the target.
  if (abs(s - speed) < 5) {
    return;
  }
  speed = speed;
  // SS2K_LOG(MAIN_LOG_TAG, "StepperSpeed is now %d, %d", speed, s);
  stepper->setSpeedInHz(speed);
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
    rtConfig->setMinResistance(-DEFAULT_RESISTANCE_RANGE);
    rtConfig->setMaxResistance(DEFAULT_RESISTANCE_RANGE);
    txCheck++;
  }
}

void SS2K::pelotonConnected() {
  txCheck = TX_CHECK_INTERVAL;
  if (rtConfig->resistance.getValue() > 0) {
    rtConfig->setMinResistance(MIN_PELOTON_RESISTANCE);
    rtConfig->setMaxResistance(MAX_PELOTON_RESISTANCE);
  } else {
    rtConfig->setMinResistance(-DEFAULT_RESISTANCE_RANGE);
    rtConfig->setMaxResistance(DEFAULT_RESISTANCE_RANGE);
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
        collectAndSet(PELOTON_DATA_UUID, PELOTON_DATA_UUID, PELOTON_ADDRESS, newBuf, newLen);
      }
    }
  }
}
