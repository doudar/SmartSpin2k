// Copyright (C) 2026 Anthony Doud & Joel Baranick
// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "StepperPlan.h"
#include "PulseQueue.h"
#include <freertos/semphr.h>
#include <freertos/task.h>

// Only the single-axis operations consumed by SmartSpin2k. No Arduino dependency.
class SS2KStepper {
 public:
  static constexpr uint32_t stackBytes = 3000;
  bool begin(uint8_t stepPin, bool (*enablePinCallback)(uint8_t, uint8_t));
  int32_t getCurrentPosition();
  void setCurrentPosition(int32_t value);
  int8_t moveTo(int32_t value, bool blocking = false);
  int8_t move(int32_t distance, bool blocking = false);
  int8_t runForward();
  int8_t runBackward();
  void stopMove();
  void forceStop();
  void forceStopAtCurrentPosition();
  bool isRunning();
  int8_t setSpeedInHz(uint32_t speed);
  uint32_t getSpeedInMilliHz();
  int8_t setAcceleration(int32_t acceleration);
  void setDirectionPin(uint8_t pin, bool forward);
  void setEnablePin(uint8_t pin);
  bool enableOutputs();
  bool disableOutputs();
  void setAutoEnable(bool value);
  void setDelayToDisable(uint16_t milliseconds);
  const char* driverTypeString() const { return "SS2K_MCPWM_PCNT"; }
  int stackFree() const { return task_ ? static_cast<int>(uxTaskGetStackHighWaterMark(task_)) : -1; }

 private:
  static void taskEntry(void* arg);
  void service();
  bool enable(bool value);
  bool moving();
  void abort();
  int8_t continuous(bool forward);
  class Lock {
   public:
    explicit Lock(SS2KStepper& motor) : mutex_(motor.mutex_) { xSemaphoreTake(mutex_, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(mutex_); }
   private:
    SemaphoreHandle_t mutex_;
  };
  ss2k_motor::PulseQueue queue_;
  StepperPlan plan_;
  StepperPlan::Batch pending_ = {};
  uint32_t remainingTicks_ = 0;
  bool pendingDirection_ = false;
  bool (*enableCallback_)(uint8_t, uint8_t) = nullptr;
  uint8_t enablePin_ = 255;
  bool autoEnable_ = true, enabled_ = false, initialized_ = false;
  bool needEnableDelay_ = false, wasQueued_ = false;
  uint16_t disableDelay_ = 65535;
  TickType_t lastActive_ = 0;
  StaticSemaphore_t mutexStorage_;
  SemaphoreHandle_t mutex_ = xSemaphoreCreateMutexStatic(&mutexStorage_);
  TaskHandle_t task_ = nullptr;
};
