#include "Main.h"
#include <Arduino.h>
#include <SPIFFS.h>

bool lastDir = true; //Stepper Last Direction
bool changeRadioState = false;
// Debounce Setup
// the following variables are unsigned longs because the time, measured in
// milliseconds, will quickly become a bigger number than can be stored in an int.
unsigned long lastDebounceTime = 0; // the last time the output pin was toggled
unsigned long debounceDelay = 1000; // the debounce time; increase if the output flickers

//vTaskDelay timer Delay
int letSomthingElseRun = 200;

//Stepper Speed - Lower is faster
int maxStepperSpeed = 600;

// Define output pins
const int radioPin = 27; //Should be 27 - Occasionally I switch for testing
const int shiftUpPin = 21; //Recently changed from 19 te enable all three stepper pins to be in a row for easy cable manufacture
const int shiftDownPin = 19;
const int enablePin = 18;
const int stepPin = 17;
const int dirPin = 16;
const int ledPin = 2; //Blue LED on the ESP32

// Default size for the shifter step
const int shiftStep = 400;
volatile int shifterPosition = 0;
volatile int stepperPosition = 0;

float displayValue = 0.0;

//Setup a task so the stepper will run on a different core thean the main code to prevent studdering
TaskHandle_t moveStepperTask;
//TaskHandle_t deBounceTask; //debounce our button presses using a standalone timer

//*************************Load The Config*********************************
userParameters config;
//userParameters *configptr = &config;
///////////////////////////////////////////////////////BEGIN SETUP/////////////////////////////////////

void setup()
{

  // Serial port for debugging purposes
  Serial.begin(512000);

  // Initialize SPIFFS
  Serial.println("Mounting Filesystem");
  if (!SPIFFS.begin(true))
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  //Load Config
  config.loadFromSPIFFS();
  config.printFile(); //Print config contents to serial
  config.saveToSPIFFS();

  Serial.println("Configuring Hardware Pins");
  pinMode(radioPin, INPUT_PULLUP);
  pinMode(shiftUpPin, INPUT_PULLUP);   // Push-Button with input Pullup
  pinMode(shiftDownPin, INPUT_PULLUP); // Push-Button with input Pullup
  pinMode(ledPin, OUTPUT);
  pinMode(enablePin, OUTPUT);
  pinMode(dirPin, OUTPUT);  // Stepper Direction Pin
  pinMode(stepPin, OUTPUT); // Stepper Step Pin
  digitalWrite(enablePin, HIGH); //Should be called a disable Pin - High Disables FETs
  digitalWrite(dirPin, LOW);
  digitalWrite(stepPin, LOW);
  digitalWrite(ledPin, LOW);

  Serial.println("Creating Interrupts");
  //Setup Interrups so shifters work at anytime
  attachInterrupt(digitalPinToInterrupt(shiftUpPin), shiftUp, FALLING);
  attachInterrupt(digitalPinToInterrupt(shiftDownPin), shiftDown, FALLING);
  attachInterrupt(digitalPinToInterrupt(radioPin), changeRadioStateButton, FALLING);

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

  /************************************************StartingBLE Server***********************/
  if (config.getWifiOn())
  {
    Serial.println("Starting Config wifi on");
    startWifi();
    startHttpServer();
    digitalWrite(ledPin, LOW);
  }
  else
  {
    Serial.println("Starting Config wifi off");
    BLEserverScan(); //Scan for Known BLE Servers
    digitalWrite(ledPin, HIGH);
  }

  //BLEserverScan(); //Scan for Known BLE Servers
  startBLEServer(); //Start our own BLE server
}

void loop()
{

  BLENotify();
  vTaskDelay(500 / portTICK_RATE_MS); //guessing it's good to have task delays seperating client & Server?
  if (!config.getWifiOn())
  {
    bleClient();
  }
  if (displayValue != config.getIncline() / (float)1000)
  {
    Serial.println("Target Incline:");
    displayValue = config.getIncline() / (float)1000;
    Serial.println(displayValue);
  }

  if (changeRadioState)
  {
    switchRadioState();
  }

  // Serial.println("Move Stepper High Water Mark:");
  // Serial.println(uxTaskGetStackHighWaterMark(moveStepperTask));

  //  Serial.println("BLE Stopping");
  // btStop();
  vTaskDelay(500 / portTICK_RATE_MS);
  // Serial.println("BLE Starting");
  // btStart();
  //startBLEServer();
  //vTaskDelay(4000/portTICK_RATE_MS);
}

//Switching the radios on and off after services are defined causes crashes so instead of reconfiguring the services and then downing
//the interface, we're just going to update the configuration, save it and then reboot. For Now. Hopefully this can be sorted out in the future.
void switchRadioState()
{
  if (config.getWifiOn())
  { //wifi is currently on, turn it off, turn BT client on.
    Serial.println("User Pressed Radio Button to turn WiFi Off");
    config.setWifiOn(false);
    config.saveToSPIFFS();
    delay(100);
    Serial.println("rebooting...");
    ESP.restart();
  }
  else
  {
    Serial.println("User Pressed Radio Button to turn WiFi On");
    config.setWifiOn(true); //wifi is currently off, turn it on, turn BT client off.
    config.saveToSPIFFS();
    delay(100);
    Serial.println("rebooting...");
    ESP.restart();
  }
}

void moveStepper(void *pvParameters)
{
  //int acceleration = 3000;
  //int currentAcceleration = acceleration;
  //bool accelerating = true;
  int targetPosition = 0;

  while (1)
  {

    targetPosition = shifterPosition + (config.getIncline() * config.getInclineMultiplier());
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
      digitalWrite(enablePin, HIGH); //disable output FETs so stepper can cool
      vTaskDelay(600 / portTICK_PERIOD_MS);
      //currentAcceleration = acceleration;
    }
    else
    {
      digitalWrite(enablePin, LOW); //enable FETs for impending move

      if (stepperPosition < targetPosition)
      {
        if (lastDir == false)
        {
          //delayMicroseconds(currentAcceleration);
          vTaskDelay(100);
          //vTaskDelay(200);
          //currentAcceleration = acceleration;
        }
        digitalWrite(dirPin, HIGH);
        digitalWrite(stepPin, HIGH);
        //delayMicroseconds (currentAcceleration);
        vTaskDelay(1);
        digitalWrite(stepPin, LOW);
        stepperPosition++;
        lastDir = true;
        //Serial.println("Clockwise");
      }
      else // must be (stepperPosition > targetPosition)
      {
        if (lastDir == true)
        {
          vTaskDelay(100);
          //currentAcceleration = acceleration;
        }
        digitalWrite(dirPin, LOW);
        digitalWrite(stepPin, HIGH);
        //delayMicroseconds (currentAcceleration);
        vTaskDelay(1);
        digitalWrite(stepPin, LOW);
        stepperPosition--;
        lastDir = false;
      }
      //delayMicroseconds(currentAcceleration); //low delay
      vTaskDelay(1);
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
    shifterPosition = (shifterPosition + config.getShiftStep());
    Serial.println("Shift UP");
    Serial.println(shifterPosition);
  }
}

void IRAM_ATTR shiftDown() //Handle the shift down interrupt
{
  if (deBounce())
  {
    shifterPosition = (shifterPosition - config.getShiftStep());
    Serial.println("Shift DOWN");
    Serial.println(shifterPosition);
  }
}

void IRAM_ATTR changeRadioStateButton() //Handle the change radio state interrupt
{
  if (deBounce())
  {
    Serial.println("changeRadioStateButton() Was Called");
    changeRadioState = true; //if I try to call switchRadioState() directly in this interrupt it causes a crash
  }
}
