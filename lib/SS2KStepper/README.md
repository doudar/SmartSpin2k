# SS2KStepper

SmartSpin2k's one-motor controller: five C++ source/header files, with no Arduino
calls. The public API contains position/relative moves, continuous homing,
acceleration/speed, soft/immediate stops, direction, and guarded active-low enable.
`LogicalStepper` outside this module preserves firmware quarter-step units while
this module counts sixteenth-step pulses.

`StepperPlan.h` is a portable trapezoidal planner. It produces approximately 2 ms
constant-frequency batches, with 20 ms queued lookahead. The 32-entry fixed queue
drains to a stopped output on underrun; planning then restarts from rest. Long
intervals use low-output pause entries. Direction changes have a 200 us setup
pause. The supported speed range is 1–200,000 pulses/s (up to 50,000 logical units/s).
There are no per-move heap allocations. A mutex serializes controller calls;
a separate spinlock protects task/ISR queue access. One 2048-byte FreeRTOS task
feeds the queue every 4 ms; the hardware supplies pulse timing.

The low-level pulse timing in `PulseQueue.cpp` is adapted from the MIT-licensed
FastAccelStepper 2.0.1 MCPWM/PCNT backend, commit
`f0fac8d6bb94273a83dced4ce1592a542c915082`; see `LICENSE.timing`.
The controller, planner, and bounded queue integration are local code. This is
not the upstream planner, multi-axis engine, Arduino layer, or RMT/I2S drivers.
MCPWM group 0/timer 0/operator 0 must remain exclusively owned by this module.
The PCNT handle-prefix/register implementation is guarded to ESP-IDF 5.5 and
classic ESP32/S3. Review it before upgrading the SDK or adding another MCPWM user.

RAM: one statically held controller/queue and mutex; IDF peripheral objects,
task TCB and 2048-byte stack are allocated once at initialization. The stack
budget is 952 bytes smaller than develop's 3000-byte allocation and 3952 bytes
smaller than upstream 2.0.1's 6000-byte allocation. Static RAM alone does not measure
the total heap saving. At cruise, about 500 queue interrupts and planner batches/s
are expected rather than an interrupt per pulse. Planner square roots and locking
add CPU work; cycle savings versus FastAccelStepper have not been measured.
As a planning estimate, several thousand CPU cycles per batch are plausible on
Xtensa (especially software double arithmetic); hardware profiling is required.
The slow periodic stack-watermark scan also has a small stack-size-dependent cost.

Host tests exercise real planner/controller code with a timed peripheral model:
`test_stepper_plan.py`, `test_stepper_controller.py`, `test_stepper_units.py`.
`test_stepper_position.py` injects PCNT rollover between production position-reader
register accesses. Position reads retry when the latched limit flag changes;
repeated selections of the same auto-enable policy preserve the idle countdown.
Estimated review-fix cost: 5–15 cycles for the policy comparison (unchanged calls
skip the tick read), tens of cycles and at most 4 extra stack bytes per uncontended
position read, no additional static RAM or heap. These are unmeasured estimates.
Electrical pulse width/count and interrupt latency still require hardware testing.

Validation on 2026-11-08: classic ESP32 and S3 release builds passed; 143 Python
host tests passed with two pre-existing expected failures. An S3 test at simulated
90 rpm shifted one gear down and back, returning the reported position exactly.
The task's minimum unused stack was 2176 of 3000 bytes (824-byte peak usage),
unchanged through both shifts; available heap recovered after the test. This is a
short motion/memory check, not full homing or electrical pulse-count qualification.

The subsequent 2048-byte budget leaves 1224 bytes above that observed peak; it
has not yet been profiled on hardware. Continuous moves reuse the same bounded
call chain and fixed queue, so stack usage does not accumulate with move count or
duration. Less common paths can still increase the observed peak; retain watermark
checks during reversals, stops, homing, and recovery before reducing it further.
