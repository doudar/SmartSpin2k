#include "ERG_Mode.h"
#include "SS2KLog.h"
#include "Main.h"

TaskHandle_t ErgTask;

void setupERG() {
  TaskHandle_t task_handle;
  SS2K_LOG(ERG_MODE_LOG_TAG, "Starting ERG Mode task...");
  xTaskCreatePinnedToCore(ergTaskLoop,   /* Task function. */
                          "ERGModeTask", /* name of task. */
                          2500,          /* Stack size of task*/
                          NULL,          /* parameter of the task */
                          1,             /* priority of the task*/
                          &ErgTask,      /* Task handle to keep track of created task */
                          0);            /* pin task to core 0 */

  SS2K_LOG(ERG_MODE_LOG_TAG, "ERG Mode task started");
}

void ergTaskLoop(void* pvParameters) {
  ErgMode ergMode;
  while (true) {
    vTaskDelay(ERG_MODE_DELAY / portTICK_PERIOD_MS);

    int newSetPoint             = rtConfig.getTargetWatts();
    bool isInErgMode            = rtConfig.getERGMode();
    bool hasConnectedPowerMeter = spinBLEClient.connectedPM;
    bool simulationRunning      = rtConfig.getSimulateTargetWatts();

    if (isInErgMode && (hasConnectedPowerMeter || simulationRunning)) {
      SS2K_LOG(ERG_MODE_LOG_TAG, "ComputeERG. Setpoint: %d", newSetPoint);
      ergMode.computErg(newSetPoint);
    } else {
      if (newSetPoint > 0) {
        SS2K_LOG(ERG_MODE_LOG_TAG, "ERG request but ERG off or no connected PM");
      }
    }

#ifdef DEBUG_STACK
    Serial.printf("ERG Task: %d \n", uxTaskGetStackHighWaterMark(ErgTask));
#endif  // DEBUG_STACK
  }
}

// as a note, Trainer Road sends 50w target whenever the app is connected.
void ErgMode::computErg(int newSetPoint) {
  static bool userIsPedaling = true;
  static int setPoint        = 0;
  float incline              = rtConfig.getIncline();
  float newIncline           = incline;
  int amountToChangeIncline  = 0;
  int wattChange             = rtConfig.getSimulatedWatts().value - setPoint;
  int cadance                = rtConfig.getSimulatedCad();
  bool isStepperRunning      = rtConfig.getStepperRunning();

  SS2K_LOG(ERG_MODE_LOG_TAG, "Incline = %f, SetPoint = %d, NewSetPoint = %d, WattChange = %d", incline, setPoint, newSetPoint, wattChange);
  if (isStepperRunning) {
    SS2K_LOG(ERG_MODE_LOG_TAG, "Stepper is running. Skip ERG computation");
  }

  if (newSetPoint > 0) {  // only update the value if new value is sent
    setPoint = newSetPoint;
  }
  if (setPoint < 50) {  // minumum setPoint
    setPoint = 50;
  }

  if (cadance <= 20) {
    if (!userIsPedaling) {  // Test so motor stop command only happens once.
      motorStop();          // release tension
      return;
    }
    userIsPedaling = false;
    rtConfig.setIncline(incline - userConfig.getShiftStep() * 2);
    // Cadence too low, nothing to do here
    return;
  }
  userIsPedaling = true;

  amountToChangeIncline = wattChange * userConfig.getERGSensitivity() * (75.0 / (double)cadance);
  if (abs(wattChange) < WATTS_PER_SHIFT) {
    // As the desired value gets closer, make smaller changes for a smoother experience
    amountToChangeIncline *= SUB_SHIFT_SCALE;
  }
  // limit to 10 shifts at a time
  if (abs(amountToChangeIncline) > userConfig.getShiftStep() * 2) {
    if (amountToChangeIncline > 5) {
      amountToChangeIncline = userConfig.getShiftStep() * 2;
    }
    if (amountToChangeIncline < -5) {
      amountToChangeIncline = -(userConfig.getShiftStep() * 2);
    }
  }

  // Reduce the amount per loop (don't try to oneshot it) and scale the movement the higher the watt target is as higher wattages require less knob movement.
  // amountToChangeIncline = amountToChangeIncline / ((userConfig.getSimulatedWatts() / 100) + .1);  // +.1 to eliminate possible divide by zero.
  newIncline = incline - amountToChangeIncline;
  rtConfig.setIncline(newIncline);
  SS2K_LOG(ERG_MODE_LOG_TAG, "newincline: %f", newIncline);
}
