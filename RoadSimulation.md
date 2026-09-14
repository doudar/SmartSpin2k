# ESP32-S3 physics shifting

Physics shifting activates automatically in local simulation/inclination mode on
ESP32-S3 when the bike is homed and the learned power table can resolve positions.
There is no enable setting. Classic ESP32, unhomed bikes, and bikes without usable
table data retain the existing shift-step/incline-multiplier behavior. ERG and
resistance modes retain their existing controls. Connected Zwift/OpenBikeControl
shifting and external motor control take precedence over local physics.

The firmware-hosted Settings page accepts rider weight in kg and a comma-separated
list of effective gear ratios, easiest to hardest. Defaults are 75 kg and 24 ratios
from 50/34 chainrings with an 11-12-13-14-15-17-19-21-24-28-30-34 cassette, sorted by
ratio. This is a sequential virtual gear list, not independent front/rear shifting.
Custom lists support 2–26 entries, including 11/12/13-speed single-chainring and
22/24/26-combination double-chainring profiles. Duplicate ratios are allowed.

On first entering physics mode, the selected gear starts in the middle of the profile.
Subsequent mode changes retain the last physics gear for the current session.
`shifterPosition` becomes a 1-based gear number, clamped to the profile length.
Leaving local physics restores the prior legacy shift position unless an app owns
shifting. Changing a profile clamps the current gear to its new length. Profile
changes are validated completely and exchanged under a short critical section.

## Road load

The controller updates at most every 100 ms. It computes steady road resistance:

```
mass = riderWeightKg + 10 kg virtual bike
angle = atan(gradePercent / 100)
airspeed = loadSpeedMps + windSpeedMps
force = mass * g * (sin(angle) + Crr * cos(angle))
        + 0.5 * Cw * airspeed * abs(airspeed)
crankTorque = force * wheelRadius * gearRatio / 0.97
watts = crankTorque * crankAngularVelocity
```

Wheel circumference is 2.105 m; virtual bike mass and drivetrain efficiency are
fixed assumptions for this version. Positive wind is a headwind. Signed drag also
handles a tailwind faster than road speed. Negative total load is clamped to zero
because the magnetic brake cannot supply assistance.

The road speed used for aerodynamic load is derived from cadence and gear ratio.
It is held for one second after a ratio change, then filtered toward the new value
with a two-second time constant. This lets delayed cadence settle without an
instantaneous aerodynamic load jump. It is not a virtual momentum/freewheel model
and does not replace BLE speed measurements or app-reported speed.

An upshift adds a brief load bump; a downshift adds brief relief. Its amplitude is
half the fractional ratio change, limited to +/-5%, fading linearly to zero over
one second. Rapid shifts replace rather than accumulate the effect. This is a
shift sensation based on an assumption, not inferred acceleration or measured
inertia. It adds no braking when the requested road load is zero.

Requested watts are bounded to 4000 W and the configured nonzero maximum watts.
The homed power table maps watts/cadence to position. Zero load releases toward the
homed minimum. Each update limits requested movement to 100 ms of configured motor
travel; normal stepper travel clamps and existing guards still apply. Missing or
stale cadence (over three seconds), cadence below 20 RPM, or a failed lookup stops
further commanded movement. No derivative of cadence or fixed ERG target is used.
The learned table is the calibration; there is no additional live power feedback
trim in this version.

## FTMS inputs

Both targets validate and receive the complete seven-byte Set Indoor Bike
Simulation Parameters command (`0x11`):

| Bytes | Type | Meaning |
| --- | --- | --- |
| 1–2 | signed LE16 | Wind speed, 0.001 m/s |
| 3–4 | signed LE16 | Grade, 0.01 percent |
| 5 | unsigned byte | Crr, 0.0001 |
| 6 | unsigned byte | Cw, 0.01 kg/m |

Defaults are zero grade/wind, Crr 0.004, and Cw 0.51. Explicit zero coefficients
are honored; the protocol does not mark zero as absent. Invalid lengths return
FTMS Invalid Parameter without modifying simulation state. Existing FTMS status
notifications and forwarding retain the received bytes. Request Control and Reset
restore the road parameter defaults. Set Target Inclination updates grade using
its signed 0.1-percent units. Legacy custom incline retains its existing scaling.

Wire units follow the [Bluetooth SIG FTMS test suite](https://files.bluetooth.com/wp-content/uploads/dlm_uploads/2024/10/FTMS.TS_.p6.pdf).

## Companion-app contract

The companion app itself is not changed in this implementation.

Configuration JSON, persistence, and the chunked `BLE_allSettings` snapshot add:

| Field | Format |
| --- | --- |
| `riderWeightKg` | Number, 20–250 kg, default 75 |
| `gearRatios` | Array of 2–26 unsigned integers, ratio multiplied by 1000; each 500–6000, nondecreasing |
| `roadSimulationSupported` | Read-only capability boolean, true only on ESP32-S3; not persisted |

For example, `[1000,1500,2000,2500]` represents ratios 1.0, 1.5, 2.0, and 2.5.
Old saved configurations acquire defaults. Invalid new values retain the previous
value/default. The HTTP `/send_settings` endpoint accepts `riderWeightKg` as a
number and `gearRatios` as the JSON integer array; invalid requests return HTTP 400.
The web page displays ordinary decimal ratios and performs the wire conversion.

Custom characteristic IDs and byte-level examples are in
[CustomCharacteristic.md](CustomCharacteristic.md). Changes also generate custom
notifications on BLE/DirCon. BLE writes use the existing explicit save command
`0x18` for persistence; web saves already schedule persistence.

Runtime JSON adds `roadSimulationStatus`, `roadTargetWatts`, `roadLoadSpeedMps`,
`roadGradePercent`, `windSpeedMps`, `rollingResistance`, and `windResistance`.
Road-load speed is a controller input estimate, not a measured speed.

## Validation

Native tests exercise ratio/weight validation, 11–26 gear profiles, malformed and
signed FTMS packets, zero/maximum coefficients, gravity/rolling/aerodynamic load,
gear torque/power relationships, downhill load limits, delayed speed transitions,
and bounded shift effects including rapid shifts and timer rollover. Build both
firmware targets and both filesystem images. Hardware validation still needs
pedaling through shifts, grades, cadence dropouts, and minimum/maximum knob travel.
