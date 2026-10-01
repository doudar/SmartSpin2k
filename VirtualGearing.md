# Ratio-based virtual gearing

Local simulation/inclination modes use the selected groupset on both ESP32 and
ESP32-S3. There is no weight setting, road-load physics, power-table readiness
check, cadence gate, or timed shift effect. ERG and resistance controls retain
their existing behavior; external motor control and app-owned virtual shifting
retain precedence.

## Unlimited (default)

An empty `gearTeeth` array (`[]`) with `gearPreset: 0` selects Unlimited, the default for new/reset
settings and older configurations without a gear profile. Each shift moves by
`shiftStep`, and the logical shift position can go positive or negative without
a groupset limit. Startup assigns the existing knob position to gear 8 by setting
the stepper coordinate to `8 * shiftStep`, without moving the motor. Homing, when
configured, replaces this provisional coordinate. After successful homing the
starting gear is 8, with a physical target of `8 * shiftStep` above calibrated zero.
FTMS homing moves directly from the recovered position to that target through
normal motor control; it does not assign gear 8 to the search endpoint.
Saved homing bounds alone do not count as a successful home this boot.
Motor travel limits remain active. Ratio-only test profiles are no longer supported;
configurations without a tooth profile or named preset use Unlimited.

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
gears. Startup assigns the existing knob position that gear's ratio offset without
moving the motor. Normal control then applies that gear's ratio offset from calibrated zero
after either FTMS or mechanical homing. This is one third of the gear count,
not one third of total motor travel. Homing clears any prior ride-time drift
offset; later stationary FTMS corrections retain their coordinate-only behavior.
Successful FTMS spindown also clears its procedure opcode to simulation mode and
zeros incline before selecting the gear. Otherwise local ratio gearing is
bypassed and the recovered position can incorrectly become a terrain offset.
Regression cases include Unlimited (8), Road (8), MTB (4), Gravel (4), Mixed
Terrain 1x24 (8), and All-Rounder (8).
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
- Mixed Terrain 1x24: built-in 24-gear mixed-terrain ratio table (0.75–5.49),
  starting at gear 8 (1.68).
- All-Rounder: 48/35T | 10–33T, 24 sorted tooth combinations. The cassette
  is 10, 11, 12, 13, 14, 15, 17, 19, 21, 24, 28, 33T.

Tooth-based groupsets store 2–26 unique tooth pairs as `front * 100 + rear`: `5332` means
53x32. Both tooth counts must be 1–99 and the rounded ratio must remain 500–6000
in thousandths. Firmware sorts by that ratio, breaking equal-ratio ties by the
packed pair. Ratios are derived once for the existing motor mapping, so shift
spacing and startup behavior remain unchanged. Equal ratios from different tooth
pairs remain distinct gears with the same motor offset.

Persistence and config/all-settings responses include `gearPreset` and
`gearTeeth`. Preset 0 uses the tooth array (empty means Unlimited); preset 1 selects
the built-in Mixed Terrain 1x24 ratios and stores an empty tooth array. Mixed
Terrain 1x24 has no real tooth combinations; the profile represents a single
24-gear rear axis without inventing teeth. No arbitrary or legacy ratio-array input is
supported. Validation and median calculation finish before publication under a
short critical section. Both bounded formats use the same motor mapping above.

HTTP `/send_settings` accepts `gearTeeth` as a packed-pair JSON array and
`gearPreset` as 0 or 1, and `shiftStep` through its existing field. Submit either
`gearTeeth` or `gearPreset` in one request, not both. A tooth-array write exits
the built-in preset. Invalid profiles return HTTP 400 without
replacing the current profile. Old saved rider weight fields are ignored and
disappear on resave. The experimental rider-weight custom ID 0x33 is retired.

Custom characteristic 0x34 provides metadata/indexed reads and atomic tooth-pair
profile writes; 0x35 selects/reads the named preset. Tooth reads return an error
while Mixed Terrain is selected. See [CustomCharacteristic.md](CustomCharacteristic.md) for byte
formats. To select Unlimited, write `02 34 00`; metadata reads return `80 34 00`,
and indexed reads return an error because there are no stored pairs.
A full 26-gear write requires MTU 58; metadata and indexed reads fit MTU 23.
Save BLE/DirCon changes with command `02 18`; web settings save automatically.

Incoming FTMS simulation commands still require seven bytes; inclination commands
require three bytes. Local movement uses the received grade and ignores wind/drag
coefficients. Forwarding to an FTMS bike uses the current combined gear/terrain
target translated to grade, retaining the other received simulation fields.
