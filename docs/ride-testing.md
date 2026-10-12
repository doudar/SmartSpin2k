# Behavior and ride-performance testing

## What is implemented, and what it proves

1. **Production behavior tests** compile existing control functions or execute the
   actual UI JavaScript. Safety/protocol tests remain. Resistance tests compare
   response to target changes, measurement noise, saturation and timer wrap,
   rather than asserting an internal damping coefficient. Both web variants must
   independently preserve user selections and all shipped gearing presets;
   cosmetic/source formatting changes do not fail a byte-equality assertion.
2. **Accelerated packet-integrated ride experiments** compile production ERG,
   learning and motor functions from two firmware checkouts with the same native
   adapter. Standards-encoded CPS/CSC characteristic bytes traverse the actual
   production notification callback, bounded queue, collector and sensor decoders.
   FTMS request/start/target/stop packets traverse the production control-point
   processor. Only the sensor clock reads and peripheral APIs are replaced.
   The primary packet pipe rejects telemetry scalar inputs: watts/cadence are
   never directly assigned by its test adapter. Firmware changes resistance by
   emitted motor steps; only those steps reach the independent bike model.
3. **Optional real-BLE bench crosscheck** lives under `scripts/bike_sim`. A real
   SmartSpin2k runs unchanged firmware; Linux advertises a simulated sensor and
   sends normal FTMS workout commands, while the simulated bike reacts only to
   externally captured motor pulses/rotation. This needs a bench unit, BLE adapters
   and independent motor capture. It is not needed for accelerated packet tests.
   No physical bench validation has been performed in this workspace.

The synthetic link implements one selected peripheral profile, subscriptions,
connection/disconnection and bounded firmware queues. It does not emulate RF,
ATT/HCI timing, pairing, scan discovery, ESP32 task scheduling, or electrical
motor behavior. Production parser source is compiled unchanged except that its
host wall-clock reads use the test clock. The adapter checks known packet lengths
before invoking legacy parsers; this is a test safety guard, **not** evidence that
production rejects malformed packets. Tests record two existing CSC rollover
bugs as expected failures rather than altering generated packets to hide them.
The earlier direct-measurement component suite remains for compatibility, clearly
labeled, and does not supply the primary comparison's acceptance evidence.

A new simulator repository is a sensible eventual home for the bench tooling,
fixtures and hardware-independent evaluation. Keep the native firmware adapter
and focused behavior tests close to firmware. The independent simulator package owns the plant, protocol generators, scenarios
and evaluator; this firmware checkout supplies the thin native adapter. The
legacy integrated simulator remains for compatibility. No production `develop`
changes are required.

## Immutable baseline and repeatability

The develop baseline is pinned to
`082d1ee26ddb009d5132547087f77be03d1b0ab7`, not a moving branch. Use a clean checkout:

```sh
git worktree add --detach ../SmartSpin2k-baseline 082d1ee26ddb009d5132547087f77be03d1b0ab7
python -m pip install -r scripts/erg_sim/requirements.txt
python -B -m unittest discover -s test -p test_ride_performance.py
python -m scripts.erg_sim.compare --baseline-root ../SmartSpin2k-baseline
```

Both builds use the same adapter and deterministic plant, cadence plan, seed,
workout and acquisition schedules. The report records full firmware SHAs, a
source-content hash (so an uncommitted candidate cannot masquerade as its HEAD),
the harness hash, bike parameters and every per-seed result. Nothing is averaged
across seeds to hide an individual failure. Advancing the baseline is an explicit
reviewed change, never automatic after a regression.

Scenarios begin with a cold sensor connection and immediate app start; the
initial cadence must arrive from crank events, not a seeded firmware variable.
Startup effects remain scored. Scenarios include positive/negative steps (including 20 W), a target ramp,
cadence shifts, slow power, fast power/slow cadence, latency, deterministic
jitter and power/cadence outages. Each channel samples independently, preserves
acquired values during delay, and can publish repeated equal values as fresh
samples. The fitted bike's existing estimator/physical delay remains separate
from additional scenario delivery latency. These added rates, jitter and outage
windows are deliberate stress assumptions, not newly identified physical facts.
Every delivered frame is saved with acquisition/delivery time and immutable raw
payload in `sensor_packets.csv`; metadata reports accepted/processed/rejected
counts so a nonfunctional ingress cannot quietly pass.

Performance scoring uses the continuous effective plant output on an independent
fixed clock even during sensor outages. The fitted pole combines mechanical and
meter dynamics, so this is not independently calibrated mechanical ground truth.
A separate score uses the delivered, held sensor signal; both plant and delivered
signal comparisons must pass. Acquisition samples the continuously evolving
estimator at each channel deadline, independent of the legacy 1 Hz reporting
clock, with its own seeded noise process. Acquisition/delivery times are quantized
forward to the simulator integration tick and logged as the actual times.
Dropping bad samples cannot improve the grade. Reports include time-weighted MAE,
p95 error, overshoot/undershoot, steady-state error and variation, and contiguous
settling time inside ±20 W for five seconds. Full-ride errors retain every target
transition; steady-state and settling metrics show why an abrupt step differs
from sustained bad tracking. Tests intentionally inject constant bias, frozen
power, oscillation, delayed settling and missing data and require rejection.

### Provisional acceptance policy

The initial absolute budgets (MAE 25 W, p95 60 W, overshoot 150 W, steady RMS 35 W,
no unsettled target steps) are **provisional engineering choices**, fixed before
running either revision. They are not a rider-validated specification. The
relative gate separately catches deterioration beyond explicitly recorded small
allowances, including settling, even when overall MAE improves. Both baseline and
candidate may fail an absolute quality band; that is not evidence that this
branch introduced the problem. Do not silently widen bands to make CI green.

The new `ride-performance` workflow preserves traces, firmware logs, scores and
comparison metadata even on failure. Its packet-integrated performance job may be red
while a physical acceptance specification is being reviewed. It does not run or
claim the real-BLE bench gate on GitHub-hosted runners.

## Physical model limits and the next useful recording

The existing September 27 fit used the first 65% chronologically and evaluated
the remaining 35%; its holdout is a same-ride holdout, not independent validation.
The existing README records an ~11.21 W holdout MAE and a limited position/cadence
training envelope. Reuse those claims only at their actual scope. The model's
lag mixes mechanical response and power-estimator lag; noise includes rider/model
error. Learned bike-table profiles are scenarios, not proof of physical fidelity.

Use the [collectable capture procedure](https://github.com/doudar/smartspin2k-simulator/blob/main/docs/capture-procedure.md)
before requesting a ride. Normal firmware exposes status/command-processing logs,
not raw BLE callback arrival times, queue-loss counts or physical knob motion.
A short recording preflight must succeed before collecting a longer ride. The
procedure covers serial/UDP capture, settings provenance, secret redaction and
independent holdout labeling without requiring a firmware change.

## Known FTMS mode-transition defect

`test/test_ftms_mode_transitions.py` executes repeated and interrupted ERG, SIM,
inclination and resistance commands through the production queue processor.
Deliberate lost-mode and unsigned-downhill mutations must be detected.

One explicit expected failure preserves a desired behavior that current firmware
violates: after a 300 W ERG target and a SIM command, loss of both power/cadence
source flags makes a new 200 W ERG command return unsupported, but it still
switches the mode to ERG and retains the old 300 W target. `SetTargetPower` assigns
mode before checking source availability. This is a software-state reproduction,
not proof that a physical bike applies 300 W. No production fix is included.
Compiler failures and unrelated assertions are outside the expected-failure
wrapper; fixing the behavior will create an unexpected success until the marker
is removed. Transport ownership, malformed firmware packets and actual HTTP
settings persistence remain next-phase gaps.
