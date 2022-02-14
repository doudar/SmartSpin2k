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
#include <SPIFFS.h>
#include <HardwareSerial.h>
#include "FastAccelStepper.h"
#include "ERG_Mode.h"
#include "UdpAppender.h"
#include "WebsocketAppender.h"

HardwareSerial stepperSerial(2);
TMC2208Stepper driver(&SERIAL_PORT, R_SENSE);  // Hardware Serial
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper     = NULL;
// Setup a task so the stepper will run on a different core than the main code
// to prevent stuttering
TaskHandle_t moveStepperTask;
TaskHandle_t maintenanceLoopTask;

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
  stepperSerial.begin(57600, SERIAL_8N2, STEPPERSERIAL_RX, STEPPERSERIAL_TX);
  SS2K_LOG(MAIN_LOG_TAG, "Compiled %s%s", __DATE__, __TIME__);

  // Initialize SPIFFS
  SS2K_LOG(MAIN_LOG_TAG, "Mounting Filesystem");
  if (!SPIFFS.begin(true)) {
    SS2K_LOG(MAIN_LOG_TAG, "An Error has occurred while mounting SPIFFS");
    // TODO reset flash here
    return;
  }

  // Load Config
  userConfig.loadFromSPIFFS();
  userConfig.printFile();  // Print userConfig.contents to serial
  userConfig.saveToSPIFFS();

  // load PWC for HR to Pwr Calculation
  userPWC.loadFromSPIFFS();
  userPWC.printFile();
  userPWC.saveToSPIFFS();

  pinMode(RADIO_PIN, INPUT_PULLUP);
  pinMode(SHIFT_UP_PIN, INPUT_PULLUP);    // Push-Button with input Pullup
  pinMode(SHIFT_DOWN_PIN, INPUT_PULLUP);  // Push-Button with input Pullup
  pinMode(LED_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);   // Stepper Direction Pin
  pinMode(STEP_PIN, OUTPUT);  // Stepper Step Pin
  digitalWrite(ENABLE_PIN,
               HIGH);  // Should be called a disable Pin - High Disables FETs
  digitalWrite(DIR_PIN, LOW);
  digitalWrite(STEP_PIN, LOW);
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

  WiFiControl.start();

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
  // Setup Interrups so shifters work anytime
  attachInterrupt(digitalPinToInterrupt(SHIFT_UP_PIN), ss2k.shiftUp, CHANGE);
  attachInterrupt(digitalPinToInterrupt(SHIFT_DOWN_PIN), ss2k.shiftDown, CHANGE);
  digitalWrite(LED_PIN, HIGH);

  xTaskCreatePinnedToCore(SS2K::maintenanceLoop,     /* Task function. */
                          "maintenanceLoopFunction", /* name of task. */
                          3500,                      /* Stack size of task */
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
    if (rtConfig.getShifterPosition() > ss2k.lastShifterPosition) {
      SS2K_LOG(MAIN_LOG_TAG, "Shift UP: %l", rtConfig.getShifterPosition());
      Serial.println(ss2k.targetPosition);
      spinBLEServer.notifyShift();
    } else if (rtConfig.getShifterPosition() < ss2k.lastShifterPosition) {
      SS2K_LOG(MAIN_LOG_TAG, "Shift DOWN: %l", rtConfig.getShifterPosition());
      Serial.println(ss2k.targetPosition);
      spinBLEServer.notifyShift();
    }
    ss2k.lastShifterPosition = rtConfig.getShifterPosition();
    webSocketAppender.Loop();

    if ((millis() - intervalTimer) > 500) {
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

#ifdef DEBUG_STACK
      Serial.printf("Step Task: %d \n", uxTaskGetStackHighWaterMark(moveStepperTask));
      Serial.printf("Shft Task: %d \n", uxTaskGetStackHighWaterMark(maintenanceLoopTask));
      Serial.printf("Free Heap: %d \n", ESP.getFreeHeap());
      Serial.printf("Best Blok: %d \n", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#endif  // DEBUG_STACK
      loopCounter = 0;
    }
    loopCounter++;
  }
}
#endif  // UNIT_TEST

void SS2K::moveStepper(void *pvParameters) {
  engine.init();
  bool _stepperDir = userConfig.getStepperDir();
  stepper          = engine.stepperConnectToPin(STEP_PIN);
  stepper->setDirectionPin(DIR_PIN, _stepperDir);
  stepper->setEnablePin(ENABLE_PIN);
  stepper->setAutoEnable(true);
  stepper->setSpeedInHz(STEPPER_SPEED);
  stepper->setAcceleration(STEPPER_ACCELERATION);
  stepper->setDelayToDisable(1000);

  while (1) {
    if (stepper) {
      ss2k.stepperIsRunning = stepper->isRunning();
      if (!ss2k.externalControl) {
        if (rtConfig.getERGMode()) {
          // ERG Mode
          // Shifter not used.
          stepper->setSpeedInHz(STEPPER_ERG_SPEED);
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

      if ((ss2k.targetPosition >= rtConfig.getMinStep()) && (ss2k.targetPosition <= rtConfig.getMaxStep())) {
        stepper->moveTo(ss2k.targetPosition);
      } else if (ss2k.targetPosition <= rtConfig.getMinStep()) {  // Limit Stepper to Min Position
        stepper->moveTo(rtConfig.getMinStep());
      } else {  // Limit Stepper to Max Position
        stepper->moveTo(rtConfig.getMaxStep());
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
        while (stepper->isMotorRunning()) {
          vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        stepper->setDirectionPin(DIR_PIN, _stepperDir);
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
void IRAM_ATTR SS2K::shiftUp() {  // Handle the shift up interrupt IRAM_ATTR is to keep the interrput code in ram always
  if (ss2k.deBounce() && !rtConfig.getERGMode()) {
    if (!digitalRead(SHIFT_UP_PIN)) {  // double checking to make sure the interrupt wasn't triggered by emf
      rtConfig.setShifterPosition(rtConfig.getShifterPosition() - 1 + userConfig.getShifterDir() * 2);
    } else {
      ss2k.lastDebounceTime = 0;
    }  // Probably Triggered by EMF, reset the debounce
  }
}

void IRAM_ATTR SS2K::shiftDown() {  // Handle the shift down interrupt
  if (ss2k.deBounce() && !rtConfig.getERGMode()) {
    if (!digitalRead(SHIFT_DOWN_PIN)) {  // double checking to make sure the interrupt wasn't triggered by emf
      rtConfig.setShifterPosition(rtConfig.getShifterPosition() + 1 - userConfig.getShifterDir() * 2);
    } else {
      ss2k.lastDebounceTime = 0;
    }  // Probably Triggered by EMF, reset the debounce
  }
}

void SS2K::resetIfShiftersHeld() {
  if ((digitalRead(SHIFT_UP_PIN) == LOW) && (digitalRead(SHIFT_DOWN_PIN) == LOW)) {
    SS2K_LOG(MAIN_LOG_TAG, "Resetting to defaults via shifter buttons.");
    for (int x = 0; x < 10; x++) {  // blink fast to acknowledge
      digitalWrite(LED_PIN, HIGH);
      vTaskDelay(200 / portTICK_PERIOD_MS);
      digitalWrite(LED_PIN, LOW);
    }
    for (int i = 0; i < 20; i++) {
      userConfig.setDefaults();
      vTaskDelay(200 / portTICK_PERIOD_MS);
      userConfig.saveToSPIFFS();
      vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    ESP.restart();
  }
}

void SS2K::scanIfShiftersHeld() {
  if ((digitalRead(SHIFT_UP_PIN) == LOW) && (digitalRead(SHIFT_DOWN_PIN) == LOW)) {  // are both shifters held?
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

  uint16_t msread = driver.microsteps();
  SS2K_LOG(MAIN_LOG_TAG, " read:ms=%ud", msread);

  driver.rms_current(userConfig.getStepperPower());  // Set motor RMS current
  driver.microsteps(4);                              // Set microsteps to 1/8th
  driver.irun(DRIVER_MAX_PWR_SCALER);
  driver.ihold((uint8_t)(DRIVER_MAX_PWR_SCALER * .65));  // hold current % 0-DRIVER_MAX_PWR_SCALER
  driver.iholddelay(10);                                 // Controls the number of clock cycles for motor
                                                         // power down after standstill is detected
  driver.TPOWERDOWN(128);
  msread               = driver.microsteps();
  uint16_t currentread = driver.cs_actual();
  SS2K_LOG(MAIN_LOG_TAG, " read:current=%ud", currentread);
  SS2K_LOG(MAIN_LOG_TAG, " read:ms=%ud", msread);

  driver.toff(5);
  bool t_bool = userConfig.getStealthchop();
  driver.en_spreadCycle(!t_bool);
  driver.pwm_autoscale(t_bool);
  driver.pwm_autograd(t_bool);
}

// Applies current power to driver
void SS2K::updateStepperPower() {
  SS2K_LOG(MAIN_LOG_TAG, "Stepper power is now %d", userConfig.getStepperPower());
  driver.rms_current(userConfig.getStepperPower());
}

// Applies current Stealthchop to driver
void SS2K::updateStealthchop() {
  bool t_bool = userConfig.getStealthchop();
  driver.en_spreadCycle(!t_bool);
  driver.pwm_autoscale(t_bool);
  driver.pwm_autograd(t_bool);
  SS2K_LOG(MAIN_LOG_TAG, "Stealthchop is now %d", t_bool);
}

// Checks the driver temperature and throttles power if above threshold.
void SS2K::checkDriverTemperature() {
  static bool overtemp = false;
  if (static_cast<int>(temperatureRead()) > 72) {  // Start throttling driver power at 72C on the ESP32
    uint8_t throttledPower = (72 - static_cast<int>(temperatureRead())) + DRIVER_MAX_PWR_SCALER;
    driver.irun(throttledPower);
    SS2K_LOGW(MAIN_LOG_TAG, "Overtemp! Driver is throttleing down! ESP32 @ %f C", temperatureRead());
    overtemp = true;
  } else if ((driver.cs_actual() < DRIVER_MAX_PWR_SCALER) && !driver.stst()) {
    if (overtemp) {
      SS2K_LOG(MAIN_LOG_TAG, "Temperature is now under control. Driver current reset.");
      driver.irun(DRIVER_MAX_PWR_SCALER);
    }
    overtemp = false;
  }
}

void SS2K::motorStop(bool releaseTension) {
  stepper->stopMove();
  stepper->setCurrentPosition(ss2k.targetPosition);
  if (releaseTension) {
    stepper->moveTo(ss2k.targetPosition - userConfig.getShiftStep() * 4);
  }
}
