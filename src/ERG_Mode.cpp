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
  Measurement newWatts        = rtConfig.getSimulatedWatts();
  float incline               = rtConfig.getCurrentIncline();
  float amountToChangeIncline = 0;
  float wattChange            = newSetPoint - newWatts.value;  // setpoint_form_trainer - current_power => Amount to increase or decrease incline
  int cadance                 = rtConfig.getSimulatedCad();

  if (watts.timestamp == newWatts.timestamp && this->setPoint == newSetPoint) {
    return;  // no new power measurement.
  }

  // set minimum SetPoint to 50 watt if trainer sends setpoints lower than 50 watt.
  if (newSetPoint < 50) {
    newSetPoint = 50;
  }

  SS2K_LOG(ERG_MODE_LOG_TAG, "Incline = %f, SetPoint = %d, NewSetPoint = %d, CurrentWatts = %d, Watts Change (NewSetPoint - CurrentWatts)= %f", incline, this->setPoint,
           newSetPoint, newWatts.value, wattChange);

  // store for next cycle for timestamp and value compare
  this->watts    = newWatts;
  this->setPoint = newSetPoint;

  if (cadance <= 20) {
    if (!this->engineStopped) {                              // Test so motor stop command only happens once.
      motorStop();                                           // release tension
      rtConfig.setTargetIncline(incline - WATTS_PER_SHIFT);  // release incline
      this->engineStopped = true;
    }
    return;  // Cadence too low, nothing to do here
  }
  this->engineStopped = false;

  amountToChangeIncline = wattChange * userConfig.getERGSensitivity();  // * (75.0 / (double)cadance);
  SS2K_LOG(ERG_MODE_LOG_TAG, "Set amountToChangeIncline to: %f", amountToChangeIncline);
  if (abs(wattChange) < WATTS_PER_SHIFT) {
    // As the desired value gets closer, make smaller changes for a smoother experience
    amountToChangeIncline *= SUB_SHIFT_SCALE;
    SS2K_LOG(ERG_MODE_LOG_TAG, "Watt change < %d watts. Correct amountToChangeIncline to: %f", WATTS_PER_SHIFT, amountToChangeIncline);
  }
  // limit to 10 shifts at a time
  if (abs(amountToChangeIncline) > WATTS_PER_SHIFT * 2) {
    if (amountToChangeIncline > 5) {
      amountToChangeIncline = WATTS_PER_SHIFT * 2;
    }
    if (amountToChangeIncline < -5) {
      amountToChangeIncline = -(WATTS_PER_SHIFT * 2);
    }
    SS2K_LOG(ERG_MODE_LOG_TAG, "Watt change > %d watts. Correct amountToChangeIncline to: %f", WATTS_PER_SHIFT * 2, amountToChangeIncline);
  }

  // Reduce the amount per loop (don't try to oneshot it) and scale the movement the higher the watt target is as higher wattages require less knob movement.
  float newIncline = incline + amountToChangeIncline;
  rtConfig.setTargetIncline(newIncline);
  SS2K_LOG(ERG_MODE_LOG_TAG, "newincline: %f", newIncline);
}
