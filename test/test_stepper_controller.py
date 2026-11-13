"""Run the production controller against a timed pulse-queue/RTOS model."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LIB = ROOT / "lib/SS2KStepper"

RTOS = r'''
#pragma once
#include <cstdint>
#include <cassert>
using TickType_t = uint32_t;
using TaskHandle_t = void*;
struct StaticSemaphore_t { bool locked = false; };
using SemaphoreHandle_t = StaticSemaphore_t*;
inline uint32_t nowMs = 0, taskStack = 0;
constexpr int portMAX_DELAY = -1, configMAX_PRIORITIES = 25, pdPASS = 1;
#define pdMS_TO_TICKS(ms) (ms)
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* s) { return s; }
inline void xSemaphoreTake(SemaphoreHandle_t s, int) { assert(!s->locked); s->locked = true; }
inline void xSemaphoreGive(SemaphoreHandle_t s) { assert(s->locked); s->locked = false; }
inline uint32_t xTaskGetTickCount() { return nowMs; }
inline int uxTaskGetStackHighWaterMark(TaskHandle_t) { return 1000; }
inline int xTaskCreate(void(*)(void*), const char*, uint32_t bytes, void*, int, TaskHandle_t* task) { taskStack = bytes; *task = (void*)1; return pdPASS; }
void vTaskDelay(uint32_t);
'''

QUEUE = r'''
#pragma once
#include <deque>
#include <cstdint>
#include <cassert>
#include "freertos/FreeRTOS.h"
namespace ss2k_motor {
inline bool physicalEnabled = false;
class PulseQueue {
 public:
  static constexpr unsigned capacity = 32;
  struct Pulse { uint16_t ticks; uint8_t steps; bool forward; bool toggle; };
  std::deque<Pulse> pending;
  int32_t pos = 0;
  bool running = false, tailForward = true, physicalForward = true;
  uint32_t elapsed = 0;
  uint32_t pulses = 0;
  bool begin(uint8_t) { return true; }
  void release() {}
  bool push(uint16_t ticks, uint8_t steps, bool forward, bool toggle = false) {
    assert(ticks > 0);
    if (pending.size() == capacity) return false;
    pending.push_back({ticks, steps, forward, toggle}); tailForward = forward;
    return true;
  }
  bool start() { running = !pending.empty(); return running; }
  int32_t stop() { pending.clear(); elapsed = 0; running = false; tailForward = physicalForward; return pos; }
  int32_t position() { return pos; }
  void rebase(int32_t value) { assert(pending.empty()); pos = value; }
  bool busy() { return running || !pending.empty(); }
  bool direction() const { return tailForward; }
  void directionPin(uint8_t, bool) {}
  uint32_t queuedTicks() {
    uint32_t ticks = 0;
    for (auto p : pending) ticks += p.ticks * (p.steps ? p.steps : 1);
    return ticks;
  }
  void consume(uint32_t ticks) {
    while (running && ticks) {
      auto& p = pending.front();
      if (p.toggle) { physicalForward = !physicalForward; p.toggle = false; assert(!p.steps && p.ticks >= 3200); }
      uint32_t used = std::min(ticks, uint32_t(p.ticks - elapsed));
      ticks -= used; elapsed += used;
      if (elapsed != p.ticks) continue;
      elapsed = 0;
      if (p.steps) {
        assert(physicalEnabled && physicalForward == p.forward);
        ++pulses; pos += p.forward ? 1 : -1;
        if (--p.steps) continue;
      }
      pending.pop_front();
      if (pending.empty()) running = false;
    }
  }
};
}
'''

HARNESS = r'''
#include "SS2KStepper.h"
#include "LogicalStepper.h"
#include <cstdio>
SS2KStepper motor;
bool inhibited = false;
bool enablePin(uint8_t pin, uint8_t level) {
  assert(pin == 7);
  bool actual = inhibited || level;
  ss2k_motor::physicalEnabled = !actual;
  return actual;
}
void vTaskDelay(uint32_t ms) {
  nowMs += ms;
  motor.queue_.consume(ms * 16000);
  motor.service();
}
void finish(int32_t target) {
  unsigned loops = 0;
  while (motor.isRunning()) { vTaskDelay(4); assert(++loops < 100000); }
  assert(motor.getCurrentPosition() == target);
}
int main() {
  assert(motor.begin(5, enablePin));
  assert(taskStack == 2048);
  motor.setDirectionPin(6, true); motor.setEnablePin(7);
  motor.setAutoEnable(true);
  motor.moveTo(4000); finish(4000);
  motor.move(-1000, true); assert(motor.getCurrentPosition() == 3000);
  motor.moveTo(-4000); finish(-4000);
  motor.setCurrentPosition(0);
  assert(motor.setSpeedInHz(0) < 0 && motor.setSpeedInHz(StepperPlan::maxSpeed + 1) < 0);
  assert(motor.setAcceleration(0) < 0);
  // Mid-motion reversal and repeated maintenance dispatch.
  motor.moveTo(20000);
  for (int i = 0; i < 250; ++i) { motor.moveTo(20000); vTaskDelay(4); }
  motor.moveTo(-400); finish(-400);
  // Forced stops preserve partial logical steps and discard queued direction changes.
  motor.moveTo(20000);
  for (int i = 0; i < 75; ++i) vTaskDelay(4);
  motor.moveTo(-20000); vTaskDelay(4);
  int stopped = motor.getCurrentPosition();
  motor.forceStopAtCurrentPosition();
  assert(!motor.isRunning());
  vTaskDelay(40); assert(motor.getCurrentPosition() == stopped);
  motor.moveTo(-401); finish(-401);
  // A task stall must drain to a stopped output, then restart from rest.
  motor.moveTo(4000); vTaskDelay(4);
  motor.queue_.consume(16000000); nowMs += 1000;
  motor.service(); finish(4000);
  // Low speeds split one interval across multiple pause commands.
  motor.setSpeedInHz(4); motor.moveTo(4003); finish(4003);
  motor.setSpeedInHz(14000);
  // Auto-disable expires, a subsequent move re-enables. Manual cadence mode
  // must never retain a stale automatic-disable countdown.
  motor.setDelayToDisable(10); vTaskDelay(20); assert(!ss2k_motor::physicalEnabled);
  motor.moveTo(3900); finish(3900); assert(ss2k_motor::physicalEnabled);
  motor.setAutoEnable(false); vTaskDelay(40); assert(ss2k_motor::physicalEnabled);
  motor.setAutoEnable(true);
  vTaskDelay(4); assert(ss2k_motor::physicalEnabled); // A policy transition starts a fresh timeout.
  for (int i = 0; i < 100; ++i) { motor.setAutoEnable(true); vTaskDelay(4); }
  assert(!ss2k_motor::physicalEnabled); // Repeated maintenance calls must not postpone disable.
  motor.moveTo(3901); finish(3901); assert(ss2k_motor::physicalEnabled);
  for (int i = 0; i < 100; ++i) { motor.setAutoEnable(true); vTaskDelay(4); }
  assert(!ss2k_motor::physicalEnabled);
  motor.moveTo(3900); finish(3900);
  motor.setAutoEnable(false);
  for (int i = 0; i < 100; ++i) { motor.setAutoEnable(false); vTaskDelay(4); }
  assert(ss2k_motor::physicalEnabled);
  // Tick wrap must not extend the transition's idle timeout.
  nowMs = UINT32_MAX - 5;
  motor.setAutoEnable(true); vTaskDelay(4); assert(ss2k_motor::physicalEnabled);
  motor.setAutoEnable(true); vTaskDelay(8); assert(!ss2k_motor::physicalEnabled);
  inhibited = true; motor.moveTo(5000); vTaskDelay(4);
  assert(!motor.isRunning() && motor.getCurrentPosition() == 3900);
  inhibited = false; motor.moveTo(4000); finish(4000);
  motor.runBackward(); for (int i = 0; i < 50; ++i) vTaskDelay(4);
  motor.stopMove(); while (motor.isRunning()) vTaskDelay(4);
  stopped = motor.getCurrentPosition(); vTaskDelay(40);
  assert(stopped == motor.getCurrentPosition());
  // Logical units stay at quarter steps; hardware motion stays at sixteenth steps.
  LogicalStepper<SS2KStepper> logical;
  logical.attach(&motor); logical.setCurrentPosition(0);
  logical.moveTo(1210, true); assert(motor.getCurrentPosition() == 4840);
  logical.move(-1210, true); assert(motor.getCurrentPosition() == 0);
  motor.setCurrentPosition(INT32_MAX - 4);
  assert(motor.move(5) == -127 && !motor.isRunning());
  std::printf("Controller timing, queue, enable, stop, and logical-unit tests passed.\n");
}
'''


class ControllerTests(unittest.TestCase):
    def test_controller_with_timed_peripheral(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "freertos").mkdir()
            (path / "freertos/FreeRTOS.h").write_text(RTOS, encoding="utf-8")
            for name in ("task.h", "semphr.h"):
                (path / "freertos" / name).write_text('#include "FreeRTOS.h"\n', encoding="utf-8")
            (path / "PulseQueue.h").write_text(QUEUE, encoding="utf-8")
            # Expose private state only to this harness; execute unmodified production methods.
            (path / "SS2KStepper.h").write_text((LIB / "SS2KStepper.h").read_text().replace(" private:", " public:"), encoding="utf-8")
            shutil.copy(LIB / "SS2KStepper.cpp", path)
            shutil.copy(LIB / "StepperPlan.h", path)
            (path / "main.cpp").write_text(HARNESS, encoding="utf-8")
            exe = path / "controller.exe"
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(path), "-I", str(ROOT / "include"),
                            str(path / "SS2KStepper.cpp"), str(path / "main.cpp"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
