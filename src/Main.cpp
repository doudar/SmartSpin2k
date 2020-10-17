// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive

#include "Main.h"
#include <Arduino.h>
#include <SPIFFS.h>

const String FirmwareVer = {"0.0.10.6"};
String debugToHTML = "Current Firmware Version: " + FirmwareVer;

bool lastDir = true; //Stepper Last Direction
bool changeRadioState = false;
// Debounce Setup
// the following variables are unsigned longs because the time, measured in
// milliseconds, will quickly become a bigger number than can be stored in an int.
unsigned long lastDebounceTime = 0; // the last time the output pin was toggled
unsigned long debounceDelay = 500;  // the debounce time; increase if the output flickers

//vTaskDelay timer Delay
int letSomthingElseRun = 200;

//Stepper Speed - Lower is faster
int maxStepperSpeed = 600;

// Define output pins
const int radioPin = 27;
const int shiftUpPin = 19;
const int shiftDownPin = 18;
const int enablePin = 5;
const int stepPin = 17;
const int dirPin = 16;
const int ledPin = 2; //one of those stupid blinding Blue LED's on the ESP32

// Default size for the shifter step
const int shiftStep = 400;
volatile int shifterPosition = 0;
volatile int stepperPosition = 0;

float displayValue = 0.0;

//Setup a task so the stepper will run on a different core thean the main code to prevent studdering
TaskHandle_t moveStepperTask;

//*************************Load The Config*********************************
userParameters userConfig;

///////////////////////////////////////////////////////BEGIN SETUP/////////////////////////////////////
void setup()
{

  // Serial port for debugging purposes
  Serial.begin(512000);

  Serial.printf("Program Version: %s %s \n",__DATE__, __TIME__);

  // Initialize SPIFFS
  Serial.println("Mounting Filesystem");
  if (!SPIFFS.begin(true))
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  //Load Config
  userConfig.loadFromSPIFFS();
  userConfig.printFile(); //Print userConfig.contents to serial
  userConfig.saveToSPIFFS();

  Serial.println("Configuring Hardware Pins");
  pinMode(radioPin, INPUT_PULLUP);
  pinMode(shiftUpPin, INPUT_PULLUP);   // Push-Button with input Pullup
  pinMode(shiftDownPin, INPUT_PULLUP); // Push-Button with input Pullup
  pinMode(ledPin, OUTPUT);
  pinMode(enablePin, OUTPUT);
  pinMode(dirPin, OUTPUT);       // Stepper Direction Pin
  pinMode(stepPin, OUTPUT);      // Stepper Step Pin
  digitalWrite(enablePin, HIGH); //Should be called a disable Pin - High Disables FETs
  digitalWrite(dirPin, LOW);
  digitalWrite(stepPin, LOW);
  digitalWrite(ledPin, LOW);

  Serial.println("Creating Interrupts");
  //Setup Interrups so shifters work at anytime
  attachInterrupt(digitalPinToInterrupt(shiftUpPin), shiftUp, CHANGE);
  attachInterrupt(digitalPinToInterrupt(shiftDownPin), shiftDown, CHANGE);

  Serial.println("Setting up cpu Tasks");
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
  //vTaskStartScheduler();
  Serial.println("Stepper Task Created");
  userConfig.setWifiOn(!digitalRead(radioPin));
  setupBLE();
  /************************************************StartingBLE Server***********************/

  
    Serial.println("****************Starting regular mode*********************");
    //BLEserverScan(); //Scan for Known BLE Servers
    //startBLEServer();
    Serial.println("BLE Server Started");
    //BLEServerScan(true);
    Serial.println(" - BLE Client Initialized");
    digitalWrite(ledPin, HIGH);
    startWifi();
    startHttpServer();
    FirmwareUpdate();
    startBLEServer();
}

void loop()
{

  if (userConfig.getWifiOn())
  {
    digitalWrite(ledPin, LOW); //blink led in configuration mode
  }
  
  BLENotify();
  vTaskDelay(500 / portTICK_RATE_MS); //guessing it's good to have task delays seperating client & Server?
  bleClient();

  if (displayValue != userConfig.getIncline() / (float)100)
  {
    Serial.println("Main Target Incline:");
    displayValue = userConfig.getIncline() / (float)100;
    Serial.println(displayValue);
  }
  vTaskDelay(500 / portTICK_RATE_MS);

if(debugToHTML.length()>50){ //Clear up memory
  debugToHTML = " ";
}



}

void moveStepper(void *pvParameters)
{
  int acceleration = 500;
  //int currentAcceleration = acceleration;
  //bool accelerating = true;
  int targetPosition = 0;

  while (1)
  {

    targetPosition = shifterPosition + (userConfig.getIncline() * userConfig.getInclineMultiplier());
    /*
      if(abs(stepperPosition-targetPosition)>(shiftStep/2)){
        if(currentAcceleration>maxStepperSpeed){
          currentAcceleration = currentAcceleration - 200;
        }
      }else{
        if(currentAcceleration < acceleration){
          currentAcceleration = currentAcceleration + 200;
        }
      //Serial.println(abs(stepperPosition-targetPosition));
    }
*/

    if (stepperPosition == targetPosition)
    {
      vTaskDelay(300 / portTICK_PERIOD_MS);
      //digitalWrite(enablePin, HIGH); //disable output FETs so stepper can cool
      vTaskDelay(300 / portTICK_PERIOD_MS);
      //currentAcceleration = acceleration;
    }
    else
    {
      digitalWrite(enablePin, LOW); //enable FETs for impending move
      vTaskDelay(1);                //Need a small delay here for outputs to stabalize

      if (stepperPosition < targetPosition)
      {
        if (lastDir == false)
        {
          //delayMicroseconds(currentAcceleration);
          vTaskDelay(100); //Stepper was running in opposite direction. Give it time to stop.
        }
        digitalWrite(dirPin, HIGH);
        digitalWrite(stepPin, HIGH);
        delayMicroseconds(acceleration);
        //vTaskDelay(1);
        digitalWrite(stepPin, LOW);
        stepperPosition++;
        lastDir = true;
      }
      else // must be (stepperPosition > targetPosition)
      {
        if (lastDir == true)
        {
          vTaskDelay(100); //Stepper was running in opposite direction. Give it time to stop.
          //currentAcceleration = acceleration;
        }
        digitalWrite(dirPin, LOW);
        digitalWrite(stepPin, HIGH);
        delayMicroseconds(acceleration);
        //vTaskDelay(1);
        digitalWrite(stepPin, LOW);
        stepperPosition--;
        lastDir = false;
      }
    }
  }
  Serial.println("Exited Motor Control Loop. That was Weird.");
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
    if (!digitalRead(shiftUpPin)) //double checking to make sure the interrupt wasn't triggered by emf
    {
      shifterPosition = (shifterPosition + userConfig.getShiftStep());
      Serial.println("Shift UP");
      Serial.println(shifterPosition);
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
    if (!digitalRead(shiftDownPin)) //double checking to make sure the interrupt wasn't triggered by emf
    {
      shifterPosition = (shifterPosition - userConfig.getShiftStep());
      Serial.println("Shift DOWN");
      Serial.println(shifterPosition);
    }
    else
    {
      lastDebounceTime = 0;
    } //Probably Triggered by EMF, reset the debounce
  }
}
