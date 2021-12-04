#include "ERG_Mode.h"
#include "SS2KLog.h"
#include "Main.h"

TaskHandle_t ErgMode::_ergTask;

void ErgMode::setupERG() {
  SS2K_LOG(ERG_MODE_LOG_TAG, "Starting ERG Mode task...");
  xTaskCreatePinnedToCore(loop,          /* Task function. */
                          "ERGModeTask", /* name of task. */
                          1800,          /* Stack size of task*/
                          NULL,          /* parameter of the task */
                          1,             /* priority of the task*/
                          &_ergTask,     /* Task handle to keep track of created task */
                          1);            /* pin task to core 0 */

  SS2K_LOG(ERG_MODE_LOG_TAG, "ERG Mode task started");
}

void ErgMode::loop(void *pvParameters) {
  vTaskDelay(ERG_MODE_DELAY / portTICK_PERIOD_MS);

  //   int newSetPoint             = userConfig.getTargetWatts();
  //   bool isInErgMode            = userConfig.getERGMode();
  //   bool hasConnectedPowerMeter = spinBLEClient.connectedPM;

  //   if (isInErgMode && hasConnectedPowerMeter) {
  //     SS2K_LOG(ERG_MODE_LOG_TAG, "ComputeERG. Setpoint: %d", newSetPoint);
  //     ErgMode::computErg(newSetPoint);
  //   } else {
  // SS2K_LOG(ERG_MODE_LOG_TAG, "ERG request but ERG off or no connected PM");
  //   }
}

// as a note, Trainer Road sends 50w target whenever the app is connected.
void ErgMode::computErg(int newSetPoint) {
  static bool userIsPedaling = true;
  static int setPoint        = 0;
  float incline              = userConfig.getIncline();
  float newIncline           = incline;
  int amountToChangeIncline  = 0;
  int wattChange             = userConfig.getSimulatedWatts() - setPoint;
  int cadance                = userConfig.getSimulatedCad();

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
    userConfig.setIncline(incline - userConfig.getShiftStep() * 2);
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
  userConfig.setIncline(newIncline);
  SS2K_LOG(ERG_MODE_LOG_TAG, "newincline: %f", newIncline);
}
