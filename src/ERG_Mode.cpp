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

    // collect bike charateristics
    // if (hasConnectedPowerMeter) {
    //   float incline = rtConfig.getCurrentIncline();
    //   int cadence   = rtConfig.getSimulatedCad();
    //   int power     = rtConfig.getSimulatedWatts().value;
    //   SS2K_LOG(ERG_MODE_LOG_TAG, "Bike characteristics: INC: %f; CAD: %d; PWR: %d", incline, cadence, power);
    // }
  }
}

// as a note, Trainer Road sends 50w target whenever the app is connected.
void ErgMode::computErg(int newSetPoint) {
  Measurement newWatts = rtConfig.getSimulatedWatts();
  float incline        = rtConfig.getCurrentIncline();
  int cadance          = rtConfig.getSimulatedCad();
  int wattChange       = newSetPoint - newWatts.value;  // setpoint_form_trainer - current_power => Amount to increase or decrease incline
  float diviation      = ((float)wattChange * 100.0) / ((float)newSetPoint);

  if (watts.timestamp == newWatts.timestamp && this->setPoint == newSetPoint) {
    return;  // no new power measurement.
  }

  // set minimum SetPoint to 50 watt if trainer sends setpoints lower than 50 watt.
  if (newSetPoint < 50) {
    newSetPoint = 50;
  }

  // reset cycle counter to start new erg calculation cycle
  if (this->setPoint != newSetPoint) {
    this->cycles = 0;
  }

  // store for next cycle for timestamp and value compare
  this->watts    = newWatts;
  this->setPoint = newSetPoint;

  bool isUserSpinning = this->_userIsSpinning(cadance, incline);
  if (!isUserSpinning) {
    return;
  }

  float factor        = abs(diviation) > 10 ? userConfig.getERGSensitivity() : userConfig.getERGSensitivity() / 2;
  float targetIncline = incline + (wattChange * factor);

  // SS2K_LOG(ERG_MODE_LOG_TAG, "Set amountToChangeIncline to: %f", amountToChangeIncline);

  // Minor Steps
  // if (abs(wattChange) < WATTS_PER_SHIFT) {
  //   // As the desired value gets closer, make smaller changes for a smoother experience
  //   amountToChangeIncline *= SUB_SHIFT_SCALE;
  //   SS2K_LOG(ERG_MODE_LOG_TAG, "Watt change < %d watts. Correct amountToChangeIncline to: %f", WATTS_PER_SHIFT, amountToChangeIncline);
  // }

  // // limit to 10 shifts at a time
  // if (abs(amountToChangeIncline) > WATTS_PER_SHIFT * 2) {
  //   if (amountToChangeIncline > 5) {
  //     amountToChangeIncline = WATTS_PER_SHIFT * 2;
  //   }
  //   if (amountToChangeIncline < -5) {
  //     amountToChangeIncline = -(WATTS_PER_SHIFT * 2);
  //   }
  //   SS2K_LOG(ERG_MODE_LOG_TAG, "Watt change > %d watts. Correct amountToChangeIncline to: %f", WATTS_PER_SHIFT * 2, amountToChangeIncline);
  // }

  // float newIncline = incline + amountToChangeIncline;
  rtConfig.setTargetIncline(targetIncline);
  SS2K_LOG(ERG_MODE_LOG_TAG,
           "Cycle=%d, " + "Incline = %f, " + "TargetIncline = %f, " + "SetPoint = %d, " + "NewSetPoint = %d, " + "Watts = (%lu, %d), " + "CurrentWatts = (%lu, %d), " +
               "Watts Change (NewSetPoint - CurrentWatts) = %d, " + "diviation = %f",
           this->cycles, incline, targetIncline, this->setPoint, newSetPoint, this->watts.timestamp, this->watts.value, newWatts.timestamp, newWatts.value, wattChange, diviation);
  this->cycles++;
}

bool ErgMode::_userIsSpinning(int cadance, float incline) {
  if (cadance <= 20) {
    if (!this->engineStopped) {                              // Test so motor stop command only happens once.
      motorStop();                                           // release tension
      rtConfig.setTargetIncline(incline - WATTS_PER_SHIFT);  // release incline
      this->engineStopped = true;
    }
    return false;  // Cadence too low, nothing to do here
  }

  this->engineStopped = false;
  return true;
}
