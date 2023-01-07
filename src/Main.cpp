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
#include "UdpAppender.h"
#include "WebsocketAppender.h"
#include <Constants.h>

// Stepper Motor Serial
HardwareSerial stepperSerial(1);
TMC2208Stepper driver(&SERIAL_PORT, R_SENSE);  // Hardware Serial

// Peloton Serial
HardwareSerial auxSerial(2);
AuxSerialBuffer auxSerialBuffer;
// should we interrogate the bike for cadence and power?

FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper     = NULL;
// Setup a task so the stepper will run on a different core than the main code
// to prevent stuttering
TaskHandle_t moveStepperTask;
TaskHandle_t maintenanceLoopTask;

Boards boards;
Board currentBoard;

///////////// Initialize the Config /////////////
SS2K ss2k;
userParameters userConfig;
RuntimeParameters rtConfig;
physicalWorkingCapacity userPWC;

///////////// Log Appender /////////////
UdpAppender udpAppender;
WebSocketAppender webSocketAppender;

///////////// BEGIN SETUP /////////////
#ifndef UNIT_TEST

void SS2K::startTasks() {
  SS2K_LOG(MAIN_LOG_TAG, "Start BLE + ERG Tasks");
  if (BLECommunicationTask == NULL) {
    setupBLE();
  }
  if (ErgTask == NULL) {
    setupERG();
  }
}

void SS2K::stopTasks() {
  SS2K_LOG(MAIN_LOG_TAG, "Stop BLE + ERG Tasks");
  if (BLECommunicationTask != NULL) {
    vTaskDelete(BLECommunicationTask);
  }
  if (ErgTask != NULL) {
    vTaskDelete(ErgTask);
  }
}

void setup() {
  // Serial port for debugging purposes
  Serial.begin(512000);
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
  // initialize aux serial port
  if (currentBoard.auxSerialTxPin) {
    auxSerial.setTxBufferSize(500);
    auxSerial.setRxBufferSize(500);
    auxSerial.begin(19200, SERIAL_8N1, currentBoard.auxSerialRxPin, currentBoard.auxSerialTxPin, false);  //////////////////////////////////change to false after testing!!!
    if (!auxSerial) {
      SS2K_LOG(MAIN_LOG_TAG, "Invalid Serial Pin Configuration");
    }
    // auxSerial.onReceive(auxSerialRX, true);  // setup callback
  }
  // Initialize LittleFS
  SS2K_LOG(MAIN_LOG_TAG, "Mounting Filesystem");
  if (!LittleFS.begin(false)) {
    FSUpgrader upgrade;
    SS2K_LOG(MAIN_LOG_TAG, "An Error has occurred while mounting LittleFS.");
    // BEGIN FS UPGRADE SPECIFIC//
    upgrade.upgradeFS();
    // END FS UPGRADE SPECIFIC//
  }

  // Load Config
  userConfig.loadFromLittleFS();
  userConfig.printFile();  // Print userConfig.contents to serial
  userConfig.saveToLittleFS();

  // load PWC for HR to Pwr Calculation
  userPWC.loadFromLittleFS();
  userPWC.printFile();
  userPWC.saveToLittleFS();

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

  ss2k.setupTMCStepperDriver();

  SS2K_LOG(MAIN_LOG_TAG, "Setting up cpu Tasks");
  disableCore0WDT();  // Disable the watchdog timer on core 0 (so long stepper
                      // moves don't cause problems)

  xTaskCreatePinnedToCore(SS2K::moveStepper,     /* Task function. */
                          "moveStepperFunction", /* name of task. */
                          1500,                  /* Stack size of task */
                          NULL,                  /* parameter of the task */
                          18,                    /* priority of the task */
                          &moveStepperTask,      /* Task handle to keep track of created task */
                          0);                    /* pin task to core */

  digitalWrite(LED_PIN, HIGH);

  startWifi();

  // Configure and Initialize Logger
  logHandler.addAppender(&webSocketAppender);
  logHandler.addAppender(&udpAppender);
  logHandler.initialize();

  // Check for firmware update. It's important that this stays before BLE &
  // HTTP setup because otherwise they use too much traffic and the device
  // fails to update which really sucks when it corrupts your settings.

  httpServer.FirmwareUpdate();
  ss2k.startTasks();
  httpServer.start();

  ss2k.resetIfShiftersHeld();
  SS2K_LOG(MAIN_LOG_TAG, "Creating Shifter Interrupts");
  // Setup Interrupts so shifters work anytime
  attachInterrupt(digitalPinToInterrupt(currentBoard.shiftUpPin), ss2k.shiftUp, CHANGE);
  attachInterrupt(digitalPinToInterrupt(currentBoard.shiftDownPin), ss2k.shiftDown, CHANGE);
  digitalWrite(LED_PIN, HIGH);

  xTaskCreatePinnedToCore(SS2K::maintenanceLoop,     /* Task function. */
                          "maintenanceLoopFunction", /* name of task. */
                          4500,                      /* Stack size of task */
                          NULL,                      /* parameter of the task */
                          1,                         /* priority of the task */
                          &maintenanceLoopTask,      /* Task handle to keep track of created task */
                          1);                        /* pin task to core */
}

void loop() {  // Delete this task so we can make one that's more memory efficient.
  vTaskDelete(NULL);
}

void SS2K::maintenanceLoop(void *pvParameters) {
  static int loopCounter              = 0;
  static unsigned long intervalTimer  = millis();
  static unsigned long intervalTimer2 = millis();
  static bool isScanning              = false;

  while (true) {
    vTaskDelay(200 / portTICK_RATE_MS);
    ss2k.FTMSModeShiftModifier();
    webSocketAppender.Loop();

    if ((millis() - intervalTimer) > 500) {  // add check here for when to restart WiFi
                                             // maybe if in STA mode and 8.8.8.8 no ping return?
      // ss2k.restartWifi();
      logHandler.writeLogs();
      intervalTimer = millis();
    }

    if ((millis() - intervalTimer2) > 6000) {
      if (NimBLEDevice::getScan()->isScanning()) {  // workaround to prevent occasional runaway scans
        if (isScanning == true) {
          SS2K_LOG(MAIN_LOG_TAG, "Forcing Scan to stop.");
          NimBLEDevice::getScan()->stop();
          isScanning = false;
        } else {
          isScanning = true;
        }
      }
      intervalTimer2 = millis();
    }
    if (loopCounter > 4) {
      ss2k.scanIfShiftersHeld();
      ss2k.checkDriverTemperature();
      ss2k.checkBLEReconnect();
      // SS2K_LOG(MAIN_LOG_TAG, "target %f  current %f", rtConfig.getTargetIncline(), rtConfig.getCurrentIncline());

#ifdef DEBUG_STACK
      Serial.printf("Step Task: %d \n", uxTaskGetStackHighWaterMark(moveStepperTask));
      Serial.printf("Shft Task: %d \n", uxTaskGetStackHighWaterMark(maintenanceLoopTask));
      Serial.printf("Free Heap: %d \n", ESP.getFreeHeap());
      Serial.printf("Best Blok: %d \n", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#endif  // DEBUG_STACK
      loopCounter = 0;
    }

    // Monitor serial port for data
    if (currentBoard.auxSerialTxPin) {
      ss2k.checkSerial();
    }
    loopCounter++;
  }
}

#endif  // UNIT_TEST

void SS2K::FTMSModeShiftModifier() {
  int shiftDelta = rtConfig.getShifterPosition() - ss2k.lastShifterPosition;
  if (shiftDelta) {
    switch (rtConfig.getFTMSMode()) {
      case FitnessMachineControlPointProcedure::SetTargetPower:

        rtConfig.setShifterPosition(ss2k.lastShifterPosition);  // reset shifter position because we're remapping it to ERG target
        if ((rtConfig.watts.getTarget() + (shiftDelta * ERG_PER_SHIFT) < userConfig.getMinWatts()) ||
            (rtConfig.watts.getTarget() + (shiftDelta * ERG_PER_SHIFT) > userConfig.getMaxWatts())) {
          SS2K_LOG(MAIN_LOG_TAG, "Shift to %dw blocked", rtConfig.watts.getTarget() + shiftDelta);
          break;
        }

        rtConfig.watts.setTarget(rtConfig.watts.getTarget() + (ERG_PER_SHIFT * shiftDelta));
        SS2K_LOG(MAIN_LOG_TAG, "ERG Shift. New Target: %dw", rtConfig.watts.getTarget());
        break;

      case FitnessMachineControlPointProcedure::SetTargetResistanceLevel:

        if (rtConfig.getMaxResistance() != DEFAULT_RESISTANCE_RANGE) {
          if ((rtConfig.resistance.getTarget() + shiftDelta < rtConfig.getMinResistance()) || (rtConfig.resistance.getTarget() + shiftDelta > rtConfig.getMaxResistance())) {
            SS2K_LOG(MAIN_LOG_TAG, "Shift to %d blocked", rtConfig.resistance.getTarget() + shiftDelta);
            break;
          }

          rtConfig.setShifterPosition(ss2k.lastShifterPosition);  // reset shifter position because we're remapping it to resistance target
          rtConfig.resistance.setTarget(rtConfig.resistance.getTarget() + shiftDelta);
          SS2K_LOG(MAIN_LOG_TAG, "Resistance Shift. New Target: %d", rtConfig.resistance.getTarget());

          break;
        }

      default:

        SS2K_LOG(MAIN_LOG_TAG, "Shift %+d pos %d tgt %d min %d max %d r_min %d r_max %d", shiftDelta, rtConfig.getShifterPosition(), ss2k.targetPosition, rtConfig.getMinStep(),
                 rtConfig.getMaxStep(), rtConfig.getMinResistance(), rtConfig.getMaxResistance());

        if ((ss2k.targetPosition > rtConfig.getMaxStep()) || (rtConfig.resistance.getValue() > rtConfig.getMaxResistance()) || (ss2k.targetPosition < rtConfig.getMinStep()) ||
            (rtConfig.resistance.getValue() < rtConfig.getMinResistance())) {
          SS2K_LOG(MAIN_LOG_TAG, "Shift Blocked by limits.");
          rtConfig.setShifterPosition(ss2k.lastShifterPosition);

          spinBLEServer.notifyShift();
        }
    }
    ss2k.lastShifterPosition = rtConfig.getShifterPosition();
  }
}

void SS2K::restartWifi() {
  httpServer.stop();
  vTaskDelay(100 / portTICK_RATE_MS);
  stopWifi();
  vTaskDelay(100 / portTICK_RATE_MS);
  startWifi();
  httpServer.start();
}

void SS2K::moveStepper(void *pvParameters) {
  engine.init();
  bool _stepperDir = userConfig.getStepperDir();
  stepper          = engine.stepperConnectToPin(currentBoard.stepPin);
  stepper->setDirectionPin(currentBoard.dirPin, _stepperDir);
  stepper->setEnablePin(currentBoard.enablePin);
  stepper->setAutoEnable(true);
  stepper->setSpeedInHz(STEPPER_SPEED);
  stepper->setAcceleration(STEPPER_ACCELERATION);
  stepper->setDelayToDisable(1000);

  while (1) {
    if (stepper) {
      ss2k.stepperIsRunning = stepper->isRunning();
      if (!ss2k.externalControl) {
        if ((rtConfig.getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetPower) ||
            (rtConfig.getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetResistanceLevel)) {
          ss2k.targetPosition = rtConfig.getTargetIncline();
        } else {
          // Simulation Mode
          ss2k.targetPosition = rtConfig.getShifterPosition() * userConfig.getShiftStep();
          ss2k.targetPosition += rtConfig.getTargetIncline() * userConfig.getInclineMultiplier();
        }
      }

      if (ss2k.syncMode) {
        stepper->stopMove();
        vTaskDelay(100 / portTICK_PERIOD_MS);
        stepper->setCurrentPosition(ss2k.targetPosition);
        vTaskDelay(100 / portTICK_PERIOD_MS);
      }

      if (rtConfig.getMaxResistance() != DEFAULT_RESISTANCE_RANGE) {
        if ((rtConfig.resistance.getValue() => rtConfig.getMinResistance()) && (rtConfig.resistance.getValue() <= rtConfig.getMaxResistance())) {
          stepper->moveTo(ss2k.targetPosition);
        } else if (rtConfig.resistance.getValue() < rtConfig.getMinResistance()) {  // Limit Stepper to Min Resistance
          stepper->moveTo(stepper->getCurrentPosition() + 10);
        } else {  // Limit Stepper to Max Resistance
          stepper->moveTo(stepper->getCurrentPosition() - 10);
        }

      } else {
        if ((ss2k.targetPosition >= rtConfig.getMinStep()) && (ss2k.targetPosition <= rtConfig.getMaxStep())) {
          stepper->moveTo(ss2k.targetPosition);
        } else if (ss2k.targetPosition <= rtConfig.getMinStep()) {  // Limit Stepper to Min Position
          stepper->moveTo(rtConfig.getMinStep());
        } else {  // Limit Stepper to Max Position
          stepper->moveTo(rtConfig.getMaxStep());
        }
      }

      vTaskDelay(100 / portTICK_PERIOD_MS);
      rtConfig.setCurrentIncline((float)stepper->getCurrentPosition());

      if (connectedClientCount() > 0) {
        stepper->setAutoEnable(false);  // Keep the stepper from rolling back due to head tube slack. Motor Driver still lowers power between moves
        stepper->enableOutputs();
      } else {
        stepper->setAutoEnable(true);  // disable output FETs between moves so stepper can cool. Can still shift.
      }

      if (_stepperDir != userConfig.getStepperDir()) {  // User changed the config direction of the stepper wires
        _stepperDir = userConfig.getStepperDir();
        while (stepper->isRunning()) {  // Wait until the motor stops running
          vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        stepper->setDirectionPin(currentBoard.dirPin, _stepperDir);
      }
    }
  }
}

bool IRAM_ATTR SS2K::deBounce() {
  if ((millis() - lastDebounceTime) > debounceDelay) {  // <----------------This should be assigned it's own task and just switch a global bool whatever the reading is at, it's
                                                        // been there for longer than the debounce delay, so take it as the actual current state: if the button state has changed:
    lastDebounceTime = millis();
    return true;
  }

  return false;
}

///////////// Interrupt Functions /////////////
void IRAM_ATTR SS2K::shiftUp() {  // Handle the shift up interrupt IRAM_ATTR is to keep the interrupt code in ram always
  if (ss2k.deBounce()) {
    if (!digitalRead(currentBoard.shiftUpPin)) {  // double checking to make sure the interrupt wasn't triggered by emf
      rtConfig.setShifterPosition(rtConfig.getShifterPosition() - 1 + userConfig.getShifterDir() * 2);
    } else {
      ss2k.lastDebounceTime = 0;
    }  // Probably Triggered by EMF, reset the debounce
  }
}

void IRAM_ATTR SS2K::shiftDown() {  // Handle the shift down interrupt
  if (ss2k.deBounce()) {
    if (!digitalRead(currentBoard.shiftDownPin)) {  // double checking to make sure the interrupt wasn't triggered by emf
      rtConfig.setShifterPosition(rtConfig.getShifterPosition() + 1 - userConfig.getShifterDir() * 2);
    } else {
      ss2k.lastDebounceTime = 0;
    }  // Probably Triggered by EMF, reset the debounce
  }
}

void SS2K::resetIfShiftersHeld() {
  if ((digitalRead(currentBoard.shiftUpPin) == LOW) && (digitalRead(currentBoard.shiftDownPin) == LOW)) {
    SS2K_LOG(MAIN_LOG_TAG, "Resetting to defaults via shifter buttons.");
    for (int x = 0; x < 10; x++) {  // blink fast to acknowledge
      digitalWrite(LED_PIN, HIGH);
      vTaskDelay(200 / portTICK_PERIOD_MS);
      digitalWrite(LED_PIN, LOW);
    }
    for (int i = 0; i < 20; i++) {
      userConfig.setDefaults();
      vTaskDelay(200 / portTICK_PERIOD_MS);
      userConfig.saveToLittleFS();
      vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    ESP.restart();
  }
}

void SS2K::scanIfShiftersHeld() {
  if ((digitalRead(currentBoard.shiftUpPin) == LOW) && (digitalRead(currentBoard.shiftDownPin) == LOW)) {  // are both shifters held?
    SS2K_LOG(MAIN_LOG_TAG, "Shifters Held %d", shiftersHoldForScan);
    if (shiftersHoldForScan < 1) {  // have they been held for enough loops?
      SS2K_LOG(MAIN_LOG_TAG, "Shifters Held < 1 %d", shiftersHoldForScan);
      if ((millis() - scanDelayStart) >= scanDelayTime) {  // Has this already been done within 10 seconds?
        scanDelayStart += scanDelayTime;
        spinBLEClient.resetDevices();
        spinBLEClient.serverScan(true);
        shiftersHoldForScan = SHIFTERS_HOLD_FOR_SCAN;
        digitalWrite(LED_PIN, LOW);
        SS2K_LOG(MAIN_LOG_TAG, "Scan From Buttons");
      } else {
        SS2K_LOG(MAIN_LOG_TAG, "Shifters Held but timer not up %d", (millis() - scanDelayStart) >= scanDelayTime);
        shiftersHoldForScan = SHIFTERS_HOLD_FOR_SCAN;
        return;
      }
    } else {
      shiftersHoldForScan--;
    }
  }
}

void SS2K::setupTMCStepperDriver() {
  driver.begin();
  driver.pdn_disable(true);
  driver.mstep_reg_select(true);

  ss2k.updateStepperPower();
  driver.microsteps(4);  // Set microsteps to 1/8th
  driver.irun(currentBoard.pwrScaler);
  driver.ihold((uint8_t)(currentBoard.pwrScaler * .5));  // hold current % 0-DRIVER_MAX_PWR_SCALER
  driver.iholddelay(10);                                 // Controls the number of clock cycles for motor
                                                         // power down after standstill is detected
  driver.TPOWERDOWN(128);

  driver.toff(5);
  bool t_bool = userConfig.getStealthChop();
  driver.en_spreadCycle(!t_bool);
  driver.pwm_autoscale(t_bool);
  driver.pwm_autograd(t_bool);
}

// Applies current power to driver
void SS2K::updateStepperPower() {
  uint16_t rmsPwr = (userConfig.getStepperPower());
  driver.rms_current(rmsPwr);
  uint16_t current = driver.cs_actual();
  SS2K_LOG(MAIN_LOG_TAG, "Stepper power is now %d.  read:cs=%U", userConfig.getStepperPower(), current);
}

// Applies current StealthChop to driver
void SS2K::updateStealthChop() {
  bool t_bool = userConfig.getStealthChop();
  driver.en_spreadCycle(!t_bool);
  driver.pwm_autoscale(t_bool);
  driver.pwm_autograd(t_bool);
  SS2K_LOG(MAIN_LOG_TAG, "StealthChop is now %d", t_bool);
}

// Checks the driver temperature and throttles power if above threshold.
void SS2K::checkDriverTemperature() {
  static bool overTemp = false;
  if (static_cast<int>(temperatureRead()) > THROTTLE_TEMP) {  // Start throttling driver power at 72C on the ESP32
    uint8_t throttledPower = (THROTTLE_TEMP - static_cast<int>(temperatureRead())) + currentBoard.pwrScaler;
    driver.irun(throttledPower);
    SS2K_LOG(MAIN_LOG_TAG, "Over temp! Driver is throttling down! ESP32 @ %f C", temperatureRead());
    overTemp = true;
  } else if (static_cast<int>(temperatureRead()) < THROTTLE_TEMP) {
    if (overTemp) {
      SS2K_LOG(MAIN_LOG_TAG, "Temperature is now under control. Driver current reset.");
      driver.irun(currentBoard.pwrScaler);
    }
    overTemp = false;
  }
}

void SS2K::motorStop(bool releaseTension) {
  stepper->stopMove();
  stepper->setCurrentPosition(ss2k.targetPosition);
  if (releaseTension) {
    stepper->moveTo(ss2k.targetPosition - userConfig.getShiftStep() * 4);
  }
}

void SS2K::checkSerial() {
  static int txCheck = TX_CHECK_INTERVAL;
  if (auxSerial.available() >= 8) {           // if at least 8 bytes are available to read from the serial port
    txCheck             = TX_CHECK_INTERVAL;  // Data received so write to serial port.
    int i               = 0;
    int k               = 0;
    auxSerialBuffer.len = auxSerial.readBytes(auxSerialBuffer.data, AUX_BUF_SIZE);
    // pre-process Peloton Data. If we get more serial devices we will have to move this into sensor data factory.
    // This is done here to prevent a lot of extra logging.
    for (i = 0; i < auxSerialBuffer.len; i++) {  // Find start of data string
      if (auxSerialBuffer.data[i] == HEADER) {
        for (k = i; k < auxSerialBuffer.len; k++) {  // Find end of data string
          if (auxSerialBuffer.data[k] == FOOTER) {
            k++;
            break;
          }
        }
        size_t newLen = k - i;  // find length of sub data
        uint8_t newBuf[newLen];
        for (int j = i; j < k; j++) {
          newBuf[j - i] = auxSerialBuffer.data[j];
        }
        rtConfig.setMinResistance(MIN_PELOTON_RESISTANCE);
        rtConfig.setMaxResistance(MAX_PELOTON_RESISTANCE);
        stepper->setSpeedInHz(STEPPER_PELOTON_SPEED);
        collectAndSet(PELOTON_DATA_UUID, PELOTON_DATA_UUID, PELOTON_ADDRESS, newBuf, newLen);
        break;
      }
    }
  }
  if (PELOTON_TX && (txCheck >= TX_CHECK_INTERVAL)) {
    static int alternate = 0;
    switch (alternate) {
      case 0:
        for (int i = 0; i < PELOTON_RQ_SIZE; i++) {
          auxSerial.write(peloton_rq_watts[i]);
          alternate++;
        }
        break;
      case 1:
        for (int i = 0; i < PELOTON_RQ_SIZE; i++) {
          auxSerial.write(peloton_rq_cad[i]);
          alternate++;
        }
        break;
      case 2:
        for (int i = 0; i < PELOTON_RQ_SIZE; i++) {
          auxSerial.write(peloton_rq_res[i]);
          alternate = 0;
        }
        break;
    }
    txCheck = 0;
  } else if (PELOTON_TX) {
    txCheck++;
    rtConfig.setMinResistance(-DEFAULT_RESISTANCE_RANGE);
    rtConfig.setMaxResistance(DEFAULT_RESISTANCE_RANGE);
  }
}

void SS2K::checkBLEReconnect() {
  static int bleCheck = 0;
  if (((userConfig.getConnectedPowerMeter() == "none") && (userConfig.getConnectedHeartMonitor() == "none"))) {  // Exit immediately if "none" and "none"
    bleCheck = 0;
    return;
  }
  if (((spinBLEClient.connectedHR) && (spinBLEClient.connectedPM))) {  // Exit if both are connected
    bleCheck = 0;
    return;
  }
  if (((userConfig.getConnectedPowerMeter() == "none") && (spinBLEClient.connectedHR))) {  // Exit if "none" PM and HR is connected
    bleCheck = 0;
    return;
  }
  if (((userConfig.getConnectedHeartMonitor() == "none") && (spinBLEClient.connectedPM))) {  // Exit if "none" HR and PM is connected
    bleCheck = 0;
    return;
  }
  if (bleCheck >= BLE_RECONNECT_INTERVAL) {
    bleCheck = 0;
    if (!NimBLEDevice::getScan()->isScanning()) {
      SS2K_LOG(MAIN_LOG_TAG, "Scanning from Check BLE Reconnect %d", bleCheck);
      spinBLEClient.resetDevices();
      spinBLEClient.scanProcess(BLE_RECONNECT_SCAN_DURATION);
    }
  }
  if (NimBLEDevice::getScan()->isScanning()) {
    bleCheck = 0;
  } else {
    bleCheck++;
  }
}
