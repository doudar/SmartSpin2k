// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

#include "Main.h"
#include <TMCStepper.h>
#include <Arduino.h>
#include <SPIFFS.h>
#include <HardwareSerial.h>

String debugToHTML = "<br>Firmware Version " + String(FIRMWARE_VERSION);
bool GlobalBLEClientConnected = false;

bool lastDir = true; //Stepper Last Direction

// Debounce Setup
unsigned long lastDebounceTime = 0; // the last time the output pin was toggled
unsigned long debounceDelay = 500;  // the debounce time; increase if the output flickers

//Stepper Speed - Lower is faster
int maxStepperSpeed = 500;
int shifterPosition = 0;
int stepperPosition = 0;
HardwareSerial stepperSerial(2);
TMC2208Stepper driver(&SERIAL_PORT, R_SENSE); // Hardware Serial

//Setup a task so the stepper will run on a different core than the main code to prevent stuttering
TaskHandle_t moveStepperTask;

//*************************Load The Config*********************************
userParameters userConfig;

///////////////////////////////////////////////////////BEGIN SETUP/////////////////////////////////////
void setup()
{

  // Serial port for debugging purposes
  Serial.begin(512000);
  stepperSerial.begin(57600, SERIAL_8N2, STEPPERSERIAL_RX, STEPPERSERIAL_TX);

  debugDirector("Compiled " + String(__DATE__) + String(__TIME__));

  // Initialize SPIFFS
  debugDirector("Mounting Filesystem");
  if (!SPIFFS.begin(true))
  {
    debugDirector("An Error has occurred while mounting SPIFFS");
    return;
  }

  //Load Config
  userConfig.loadFromSPIFFS();
  userConfig.printFile(); //Print userConfig.contents to serial
  userConfig.saveToSPIFFS();

  pinMode(RADIO_PIN, INPUT_PULLUP);
  pinMode(SHIFT_UP_PIN, INPUT_PULLUP);   // Push-Button with input Pullup
  pinMode(SHIFT_DOWN_PIN, INPUT_PULLUP); // Push-Button with input Pullup
  pinMode(LED_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);       // Stepper Direction Pin
  pinMode(STEP_PIN, OUTPUT);      // Stepper Step Pin
  digitalWrite(ENABLE_PIN, HIGH); //Should be called a disable Pin - High Disables FETs
  digitalWrite(DIR_PIN, LOW);
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  setupTMCStepperDriver();

  debugDirector("Setting up cpu Tasks");
  //create a task that will be executed in the moveStepper function, with priority 1 and executed on core 0
  //the main loop function always runs on core 1 by default

  disableCore0WDT(); //Disable the watchdog timer on core 0 (so long stepper moves don't cause problems)
  //disableCore1WDT();

  xTaskCreatePinnedToCore(
      moveStepper,           /* Task function. */
      "moveStepperFunction", /* name of task. */
      700,                   /* Stack size of task */
      NULL,                  /* parameter of the task */
      18,                    /* priority of the task  - 29 worked  at 1 I get stuttering */
      &moveStepperTask,      /* Task handle to keep track of created task */
      0);                    /* pin task to core 0 */

  setupBLE();
  //BLEServerScan(true);
  digitalWrite(LED_PIN, HIGH);
  startWifi();
  startHttpServer();
  if (userConfig.getautoUpdate())
  {
    FirmwareUpdate();
  }
  startBLEServer();
  resetIfShiftersHeld();
  debugDirector("Creating Shifter Interrupts");
  //Setup Interrups so shifters work at anytime
  attachInterrupt(digitalPinToInterrupt(SHIFT_UP_PIN), shiftUp, CHANGE);
  attachInterrupt(digitalPinToInterrupt(SHIFT_DOWN_PIN), shiftDown, CHANGE);
}

void loop()
{

  if (!GlobalBLEClientConnected)
  {
    digitalWrite(LED_PIN, LOW); //blink if no client connected
  }

  BLENotify();
  vTaskDelay(500 / portTICK_RATE_MS); //guessing it's good to have task delays seperating client & Server?
  bleClient();
  digitalWrite(LED_PIN, HIGH);
  vTaskDelay(500 / portTICK_RATE_MS);

  if (debugToHTML.length() > 500)
  { //Clear up memory
    debugToHTML = "<br>HTML Debug Truncated. Increase buffer if required.";
  }
  uint16_t currentread = driver.cs_actual();
  uint16_t msread=driver.microsteps();
  debugDirector(" read:current=" + String(currentread));
  debugDirector(" read:ms=" + String(msread)); 
}

void moveStepper(void *pvParameters)
{
  int acceleration = maxStepperSpeed;
  int targetPosition = 0;

  while (1)
  {
    targetPosition = shifterPosition + (userConfig.getIncline() * userConfig.getInclineMultiplier());
    if (stepperPosition == targetPosition)
    {
      vTaskDelay(300 / portTICK_PERIOD_MS);
      if (!GlobalBLEClientConnected)
      {
        digitalWrite(ENABLE_PIN, HIGH); //disable output FETs so stepper can cool
      }
      vTaskDelay(300 / portTICK_PERIOD_MS);
    }
    else
    {
      digitalWrite(ENABLE_PIN, LOW);
      vTaskDelay(1);

      if (stepperPosition < targetPosition)
      {
        if (lastDir == false)
        {
          vTaskDelay(100); //Stepper was running in opposite direction. Give it time to stop.
        }
        digitalWrite(DIR_PIN, HIGH);
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(acceleration);
        digitalWrite(STEP_PIN, LOW);
        stepperPosition++;
        lastDir = true;
      }
      else // must be (stepperPosition > targetPosition)
      {
        if (lastDir == true)
        {
          vTaskDelay(100); //Stepper was running in opposite direction. Give it time to stop.
        }
        digitalWrite(DIR_PIN, LOW);
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(acceleration);
        digitalWrite(STEP_PIN, LOW);
        stepperPosition--;
        lastDir = false;
      }
    }
  }
}

bool IRAM_ATTR deBounce()
{

  if ((millis() - lastDebounceTime) > debounceDelay)
  { //<----------------This should be assigned it's own task and just switch a global bool
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:
    // if the button state has changed:
    lastDebounceTime = millis();

    return true;
  }

  return false;
}

///////////////////////////////////////////Interrupt Functions///////////////////////////////
void IRAM_ATTR shiftUp() // Handle the shift up interrupt IRAM_ATTR is to keep the interrput code in ram always
{
  if (deBounce())
  {
    if (!digitalRead(SHIFT_UP_PIN)) //double checking to make sure the interrupt wasn't triggered by emf
    {
      shifterPosition = (shifterPosition + userConfig.getShiftStep());
      debugDirector("Shift UP: " + String(shifterPosition));
    }
    else
    {
      lastDebounceTime = 0;
    } //Probably Triggered by EMF, reset the debounce
  }
}

void IRAM_ATTR shiftDown() //Handle the shift down interrupt
{
  if (deBounce())
  {
    if (!digitalRead(SHIFT_DOWN_PIN)) //double checking to make sure the interrupt wasn't triggered by emf
    {
      shifterPosition = (shifterPosition - userConfig.getShiftStep());
      debugDirector("Shift DOWN: " + String(shifterPosition));
    }
    else
    {
      lastDebounceTime = 0;
    } //Probably Triggered by EMF, reset the debounce
  }
}

void resetIfShiftersHeld()
{
  if ((digitalRead(SHIFT_UP_PIN) == LOW) && (digitalRead(SHIFT_DOWN_PIN) == LOW))
  {
    debugDirector("Resetting to defaults via shifter buttons.");
    for (int x = 0; x < 10; x++)
    { //blink fast to acknoledge
      digitalWrite(LED_PIN, HIGH);
      vTaskDelay(200 / portTICK_PERIOD_MS);
      digitalWrite(LED_PIN, LOW);
    }
    for(int i=0;i<20;i++){
    userConfig.setDefaults();
    vTaskDelay(200 / portTICK_PERIOD_MS);
    userConfig.saveToSPIFFS();
    vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    ESP.restart();
  }
}

void debugDirector(String textToPrint, bool newline)
{
  if (newline)
  {
    Serial.println(textToPrint);
    debugToHTML += String("<br>") + textToPrint;
  }
  else
  {
    Serial.print(textToPrint);
    debugToHTML += textToPrint;
  }
}

void setupTMCStepperDriver()
{
  driver.begin();          //  SPI: Init CS pins and possible SW SPI pins
                           // UART: Init SW UART (if selected) with default 115200 baudrate
  driver.pdn_disable(true);
  driver.mstep_reg_select(true);


  uint16_t msread=driver.microsteps();
  debugDirector(" read:ms=" + msread); 
  
  driver.rms_current(2000); // Set motor RMS current
  
  driver.microsteps(4);   // Set microsteps to 1/8th
  driver.irun(255);
  driver.ihold(200);  //hold current
  driver.iholddelay(10); //Controls the number of clock cycles for motor power down after standstill is detected
  driver.TPOWERDOWN(128);
  msread=driver.microsteps();
  uint16_t currentread = driver.cs_actual();
  debugDirector(" read:current=" + currentread);
  debugDirector(" read:ms=" + msread); 
  driver.toff(5);
  driver.en_spreadCycle(true);
  driver.pwm_autoscale(false); // Needed for stealthChop
  driver.pwm_autograd(false);
//hold torque isnt enough
//add erg stuff in settings.html

}
