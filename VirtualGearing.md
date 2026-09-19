# Ratio-based virtual gearing

Local simulation/inclination modes use the selected groupset on both ESP32 and
ESP32-S3. There is no weight setting, road-load physics, power-table readiness
check, cadence gate, or timed shift effect. ERG and resistance controls retain
their existing behavior; external motor control and app-owned virtual shifting
retain precedence.

## Unlimited (default)

An empty `gearRatios` array (`[]`) selects Unlimited, the default for new/reset
settings and older configurations without a gear profile. Each shift moves by
`shiftStep`, and the logical shift position can go positive or negative without
a groupset limit. The start position is 0 while unhomed and 8 after successful
homing, with a physical target of `8 * shiftStep` above the calibrated zero.
FTMS homing moves directly from the recovered position to that target through
normal motor control; it does not assign gear 8 to the search endpoint.
Saved homing bounds alone do not count as a successful home this boot.
Motor travel limits remain active. An existing saved groupset is retained when upgrading.

```
gearOffset = shifterPosition * shiftStep
```

## Mapping bounded ratios to steps

Let `medianGap` be the median of the positive differences between adjacent sorted
gear ratios. For an even number of gaps, average the two middle gaps.

```
gearOffset = round(shiftStep * (ratio[selectedGear] - ratio[firstGear]) / medianGap)
targetPosition = gearOffset + targetIncline * inclineMultiplier
```

`shiftStep` (Shift Amount in the web UI) is the motor distance for a median-sized
ratio gap. For ratios 1.0, 1.1, 1.3, 1.6 and Shift Amount 1200, the median gap is
0.2; successive moves are 600, 1200, and 1800 steps. Absolute offsets are 0, 600,
1800, and 3600 steps. Computing from the first gear prevents accumulated rounding
drift when shifting repeatedly.

Gear 1 has zero shift offset.
Gear numbers are bounded to 1 through the profile length, independent of hardware
travel. A profile change from 22 to 13 gears clamps gear 22 to gear 13. Mode
changes retain the last local gear for the session. Startup and homing use
`max(1, gearCount / 3)`, rounded down: gear 8 for 24 gears, or gear 4 for 12/13
gears. Normal control then applies that gear's ratio offset from calibrated zero
after either FTMS or mechanical homing. This is one third of the gear count,
not one third of total motor travel. Homing clears any prior ride-time drift
offset; later stationary FTMS corrections retain their coordinate-only behavior.
Duplicate ratios share an offset and zero gaps do not count toward the
median. An all-identical profile has zero shift offset in every gear.

Ratio differences and the cached median use integers. Offset calculation uses
64-bit intermediates and saturates before conversion to stepper positions. Each
change commands a complete target; FastAccelStepper manages acceleration. Normal
homed/provisional travel limits and hardware guards still apply. Profiles are not
stretched to fill the physical travel; large gaps or steep terrain may hit a
travel limit before the last gear.

Terrain remains additive using the existing incline multiplier. Changing gear
while stopped changes the brake target immediately, as with the original step
shifting. Sensor delay and power-table trust do not affect shifting. Power tables
remain in use for ERG and optional power estimation.

## Configuration and firmware API

The firmware-hosted groupset selector provides:

- Unlimited: fixed Shift Amount spacing, no logical gear bounds (default).
- Standard Road Compact: 50/34T | 11–34T, 24 sorted ratios.
- MTB 1x12 – Wide Range: 32T | 10–52T, 12 ratios.
- Gravel 1x13 – Optimized XPLR: 42T | 10–46T, 13 ratios.

Custom arrays remain selectable as “Current custom groupset” and are preserved
when saving other settings. A bounded profile is 2–26 sorted uint16 ratios, scaled by
1000, each from 500 to 6000. `userConfig.gearRatios` persists this compact array;
no preset names or descriptions are stored. Validation and median calculation
finish before the complete profile is published under a short critical section.

HTTP `/send_settings` accepts `gearRatios` as a JSON integer array and `shiftStep`
through its existing field. Invalid profiles return HTTP 400 without replacing
the current profile. Old saved rider weight fields are ignored and disappear on
resave. The experimental rider-weight custom ID 0x33 is retired, not reused.

Custom characteristic 0x34 retains metadata/indexed reads and atomic profile
writes. See [CustomCharacteristic.md](CustomCharacteristic.md) for byte formats.
To select Unlimited, write `02 34 00`; metadata reads return `80 34 00`, and
indexed reads return an error because there are no stored ratios.
A full 26-gear write requires MTU 58; metadata and indexed reads fit MTU 23.
Save BLE/DirCon changes with command `02 18`; web settings save automatically.

Incoming FTMS simulation commands still require seven bytes; inclination commands
require three bytes. Local movement uses the received grade and ignores wind/drag
coefficients. Forwarding to an FTMS bike uses the current combined gear/terrain
target translated to grade, retaining the other received simulation fields.
