# SmartSpin2k Agent Notes

These notes are for coding agents working in this repository. They are intentionally focused on firmware/software behavior and ignore the physical design files in `Hardware/` or `hardware/`.

Keep this file current while doing code work when it would help future agents: prune notes that become wrong, and add important discoveries. Many tasks will not need an `AGENTS.md` edit. Keep it concise and project-essential rather than comprehensive.

## Project Shape

SmartSpin2k is ESP32 firmware for converting a spin bike into a BLE smart trainer. It:

- Reads local shifter buttons and optional Peloton aux serial data.
- Acts as a BLE client for power meters, cadence sensors, heart-rate monitors, FTMS trainers, HID remotes, Echelon, Flywheel, and Peloton-like data.
- Acts as a BLE server exposing cycling power, cycling speed/cadence, heart rate, FTMS, device info, firmware update, and SmartSpin2k custom characteristics.
- Serves a web UI from `data/` over WiFi/LittleFS.
- Runs a DirCon TCP bridge for BLE-like service discovery, reads, writes, and notifications over WiFi.
- Drives a TMC2209/FastAccelStepper motor to change resistance.

Primary software directories:

- `src/`: main firmware modules.
- `include/`: headers, settings, UUIDs, BLE data structures, board pins.
- `lib/SS2K/`: core sensor parsing library used by firmware and native tests.
- `lib/ArduinoCompat/`: native-test compatibility shims.
- `test/`: Unity tests for the pioarduino native environment.
- `data/`: web interface assets for classic ESP32 filesystem images.
- `data_s3/`: ESP32-S3 filesystem assets; initially mirrors `data/` but may grow independently.
- `.github/copilot-instructions.md`: older agent/build notes that may still be useful.

## Build And Test

pioarduino Core is the expected build tool. Install the pinned version from the repository root with `python -m pip install -r requirements-ci.txt` in a Python virtual environment (CI uses Python 3.11). When migrating an environment that has upstream `platformio` installed, uninstall that package before installing pioarduino because both distributions provide the same Python modules and CLI entry points. For VS Code, use the `pioarduino.pioarduino-ide` extension recommended by this repository.

The commands remain `pio` and `platformio`; configuration remains in `platformio.ini` and tool packages remain under `.platformio`. These compatibility names must not be renamed to `pioarduino`.

- Build firmware: `pio run --environment release`
- Build ESP32-S3 firmware: `pio run --environment S3release`
- Build filesystem: `pio run --target buildfs`
- Run native tests: `pio test --environment native`
- Run motor integration regressions (requires `g++` on PATH): `python -B -m unittest discover -s test -p "test_*.py"`. These exercise production homing/gear orchestration and TMC recovery with fake peripherals.
- Static analysis: `pio check -e debug`
- Pre-commit checks: `pre-commit run --all-files`

If `pio` is not on `PATH`, check the active virtual environment or the IDE-managed Core: `~/.platformio/penv/bin/pio` on macOS/Linux, or `$env:USERPROFILE\.platformio\penv\Scripts\pio.exe` on Windows. Use the absolute executable when available. Verify the selected Python environment with `python -m pip show pioarduino`; `pio --version` alone does not identify which distribution owns the command. If the launcher points to a missing or inaccessible Python executable, report that environment problem. Native tests are expected to run locally.

S3 firmware and filesystem builds use `S3firmware.bin` and `S3littlefs.bin` as their native pioarduino output/upload names. They also create `S3partitions.bin` and `S3bootloader.bin` copies for releases; the generic partition and bootloader intermediates remain because pioarduino's flash uploader depends on those names.
The GitHub release archive includes firmware, merged factory, LittleFS, partition-table, and bootloader binaries for both classic ESP32 and ESP32-S3 targets.
GitHub Actions exports `SS2K_FIRMWARE_VERSION` from the date-based release tag before invoking pioarduino. `git_tag_macro.py` requires that override in Actions so published firmware never receives a `git describe` commit suffix; local builds retain branch/commit version details.
The release workflow runs `cert_updater.py` once before firmware builds. Local pioarduino builds use the checked-in `include/cert.h` and do not perform network-dependent certificate updates.
CI installs `pioarduino==6.1.19` from `requirements-ci.txt`; it still provides the `platformio` and `pio` commands. Use this fork's SCons 4.8.1 with pioarduino 55.03.311; upstream PlatformIO's newer SCons 4.11.1 conflicts with this platform's tool installation. CI cache restore prefixes include the requirements hash to avoid mixing Core/tool versions.

Windows builds use `scripts/windows_ldgen.py` to launch ESP-IDF's linker-script generator directly, bypassing cmd.exe's 8191-character command limit. The fragment list can exceed that limit with long package paths; other build commands keep the normal SCons launcher.

Concurrent firmware builds in one checkout race on `managed_components/` and `.pio/`. When other agents are building, use an isolated checkout or source snapshot with its own generated dependencies and build outputs; do not clean their shared build directories.

Filesystem builds stage deterministic gzip copies of every HTML/CSS source file under the environment build directory. They also refresh the checked-in `.gz` companions and `list.json` in `data/` or `data_s3/`, which are consumed by repository-based automatic OTA updates.

Important timing/network notes:

- pioarduino firmware/filesystem builds and USB flashing are supported from the Codex environment when the local toolchain is available. Local builds normally finish in under five minutes.
- Locate an attached ESP32-S3's current USB CDC port with `pio device list` (typically `/dev/cu.usbmodem*` on macOS or `COM*` on Windows). A debug build (`S3debug`, with `__DEBUG__` and `SERIAL_CUSTOM_CHARACTERISTIC`) can be observed with `pio device monitor -p <port>`. Serial monitoring is read-only, while uploading a debug build is an explicit device mutation and should only be done when the task authorizes it.
- When testing attached hardware, do not switch the development machine's WiFi connection to the SmartSpin2k access point: that network has no internet access, while builds and tooling may require internet service. Communicate with the device over USB unless the user explicitly directs otherwise. Preserve the device's stored WiFi/LittleFS/NVS settings by preferring application-partition-only flashing (S3 app offset `0x60000`).
- First pioarduino builds/tests may download missing ESP32 platforms and toolchains and therefore take longer than normal.
- In restricted environments, pioarduino can fail on network downloads. If that happens, report it rather than trying to fake validation.
- The firmware itself cannot be fully run without ESP32 hardware, BLE devices, and a stepper driver.

Native tests cover sensor parsing, BLE device-name stability logic, firmware-update protocol handling, and power-table/ERG replay flows. When changing:

- Native Arduino types/timing come from the repository-owned `lib/ArduinoCompat`; the suite does not use ArduinoFake. Keep this shim and test filesystem setup portable across Apple Clang/POSIX and Windows native toolchains.
- Multi-byte protocol fields use `lib/SS2K/include/ByteUtils.h`, which wraps the platform `os/endian.h` implementation and adds explicit signed helpers such as `get_le16s()`/`put_le32s()`. ArduinoCompat supplies `os/endian.h` for native tests; do not add another endian implementation.
- `test/data/active_ride_log.txt` is the single real-world fixture for power-table and ERG tests. Each test replays it independently through `test/test_data_helpers.h`; generated tables and audit reports belong under ignored `test/output/`.

- `src/Power_Table.cpp` or `src/PowerTable_Helpers.cpp`, run `pio test -e native`.
- `lib/SS2K/src/sensors/*`, run the native tests for sensor parsing.
- BLE/FTMS code, build firmware and reason carefully about characteristic payload formats; many paths need hardware/manual validation.

Formatting:

- `.clang-format` is based on Google style, keeps include order, uses 180 column limit, and aligns consecutive macros/assignments.
- Existing C++ is mixed Arduino/ESP-IDF style. Prefer local patterns over broad refactors.

## Global State Model

Most runtime state is global and initialized in `src/Main.cpp`:

- `SS2K* ss2k`: top-level controller state and stepper/shifter behavior.
- `userParameters* userConfig`: persisted user settings loaded from LittleFS config.
- `RuntimeParameters* rtConfig`: live measurements, targets, mode, limits, and transient state.
- `ErgMode* ergMode`: ERG controller.
- `PowerTable* powerTable`: learned watts/cadence/position table.
- `SpinBLEClient spinBLEClient`: BLE client manager and connected sensor slots.
- `SpinBLEServer spinBLEServer`: BLE server manager and server write queue.
- `Board currentBoard`: selected at boot by analog hardware revision voltage.
- `UdpAppender`, `WebSocketAppender`, `BleAppender`: log sinks registered with `LogHandler`.

Because the firmware uses FreeRTOS tasks and callbacks, treat these globals as shared state. Many setters have side effects through timestamps, saved config, BLE notifications, or stepper behavior.

## Main Runtime Loop

Boot entry is `app_main()` in `src/Main.cpp`.

Boot sequence:

1. Initialize Arduino/Serial.
2. Detect hardware revision using the revision pin and ADC values in `include/boards.h`.
3. Start stepper serial and optional aux serial for Peloton.
4. Mount LittleFS.
5. Load and re-save `userConfig`.
6. Start configured WiFi in STA mode without waiting; only unconfigured devices start an AP. Check for missing web files before BLE.
7. Configure GPIO pins.
8. Initialize LED state; commanded-reboot quiet mode uses RTC memory so true power cycles still show startup blink behavior.
9. Configure TMC/FastAccelStepper via `SS2K::setupTMCStepperDriver()`.
10. Register log appenders.
11. Start BLE via `setupBLE()`.
12. Start the web server. The maintenance loop starts mDNS and DirCon after WiFi has an IP address.
13. Create `SS2K::maintenanceLoop` task.

`SS2K::maintenanceLoop()` is the main cooperative loop. It roughly does:

- Every `BLE_NOTIFY_DELAY`: `BLECommunications()`, flush logs, websocket loop.
- If not updating and not in spindown: `ss2k->FTMSModeShiftModifier()`, `ss2k->moveStepper()`, `ergMode->runERG()`.
- Periodically poll Peloton aux serial via `txSerial()`.
- Always handle local shifter button state.
- Notify changed custom-characteristic values via `BLE_ss2kCustomCharacteristic::parseNemit()`.
- Update HTTP clients and DirCon.
- Update LED status/diagnostics.
- Slow stepper near Peloton resistance limits when unhomed.
- Handle reboot/default-reset/save flags.
- Every roughly 6 seconds, log status and reboot after 30 minutes of inactivity.

Do not introduce long blocking work into `maintenanceLoop()` unless the existing code already does so for a hardware procedure like homing.

## Core Data Structures

### `Measurement`

Defined in `include/SmartSpin_parameters.h`.

Fields:

- `simulate`: whether this measurement is simulated rather than real sensor data.
- `value`: current measured/simulated value.
- `target`: requested target value.
- `min`, `max`: bounds used mostly for resistance ranges.
- `timestamp`: updated by `setSimulate()`, `setValue()`, and `setTarget()`.
- `valueTimestamp`: updated only by `setValue()`, including repeated equal values. `getValueSample()` returns value, value timestamp, and simulation flag together under a shared mutex. Resistance publishers use `setValue(value, simulated)` to update the source flag with the value.

Used for `rtConfig->watts`, `hr`, `cad`, `batt`, and `resistance`.

ERG deduplication uses the value-only timestamp plus target equality, so repeated target/config writes cannot turn held power into another correction. If adding setters or bypassing setters, preserve this distinction.

### `RuntimeParameters`

Live state. Key fields:

- `targetIncline`: internal target used by ERG, resistance mode, and simulation mode before becoming `ss2k->targetPosition`.
- `simulatedSpeed`: optional speed from sensors or custom characteristic.
- `FTMSMode`: current mode/opcode from FTMS control point.
- `shifterPosition`: logical gear/shift count.
- `homed`: whether reliable min/max stepper positions are known.
- `minStep`, `maxStep`: movement limits for stepper target clamping.
- `minResistance`, `maxResistance`: external/real resistance bounds.
- `simTargetWatts`: custom-characteristic target-watts simulation flag.
- `bleLogEnabled`: allows BLE log access through custom characteristic.

`rtConfig` is the live truth for sensor measurements and control modes. BLE server notifications read from it. Stepper and ERG code write to it.

### `userParameters`

Persisted user config. Key fields:

- Firmware/device/WiFi: update URL, device name, auto update, SSID/password.
- Stepper tuning: `shiftStep`, `stepperPower`, `stepperSpeed`, `stepperDir`, `stealthChop`, `homingSensitivity`.
- Control tuning: `inclineMultiplier`, `powerCorrectionFactor`, `ERGSensitivity`.
- ERG/power table bounds: `minWatts`, `maxWatts`, `pTab4Pwr`, `hMin`, `hMax`.
- BLE preferences: connected power meter, heart monitor, remote, found devices.
- Logging: UDP logging enabled.

Persistence lives in `src/SmartSpin_parameters.cpp` with JSON serialization to `configFILENAME` in LittleFS. For new config fields, update defaults, JSON output, save, load, and custom characteristic handling if the app needs it.

### `SS2K`

Top-level controller in `include/Main.h`.

Private state:

- Button debounce and current button states.
- `lastShifterPosition`: previous logical shift position for delta detection.
- `targetPosition` and `currentPosition`: actual stepper positions.

Public flags:

- `stepperIsRunning`
- `externalControl`: bypasses normal target computation in `moveStepper()`.
- `syncMode`: forces current stepper position to match target.
- `pelotonIsConnected`
- `rebootFlag`, `saveFlag`, `resetDefaultsFlag`, `resetPowerTableFlag`, `isUpdating`

Important functions:

- `maintenanceLoop()`: main task.
- `moveStepper()`: computes/clamps/sends stepper target.
- `_resistanceMove()`: converts resistance target into stepper target or fallback ERG target.
- `FTMSModeShiftModifier()`: remaps shifter changes depending on FTMS mode.
- `handleShiftButtons()`: debounced local button input.
- `goHome()`, `_findEndStop()`, `_findFTMSHome()`: homing procedures.
- `setupTMCStepperDriver()`, `updateStepperPower()`, `updateStealthChop()`, `updateStepperSpeed()`: motor driver configuration.
- `txSerial()`, `rxSerial()`, `pelotonConnected()`: Peloton aux serial integration.
- `setLEDEnabled()`, `updateLED()`: main LED state and diagnostic blink/pulse behavior.

## Control Flow: Sensors To Stepper

The common path is:

1. BLE client receives notification in `notifyCB()` (`src/BLE_Client.cpp`).
2. Notification is enqueued into the matching `SpinBLEAdvertisedDevice`.
3. `SpinBLEClient::postConnect()`/client task drains queued data and calls `collectAndSet()`.
4. `collectAndSet()` (`src/SensorCollector.cpp`) uses `SensorDataFactory` to decode bytes.
5. Decoded values update `rtConfig` measurements unless simulation or config rules say to ignore them.
6. `ergMode->runERG()` may update `rtConfig->targetIncline`.
7. `SS2K::moveStepper()` turns mode/state into `ss2k->targetPosition` and calls `stepper->moveTo()`.
8. BLE server services read `rtConfig` and notify connected apps.

Important gates in `collectAndSet()`:

- Heart rate zeros are ignored until 10 consecutive zero-ish readings.
- Cadence accepts 1-249 RPM, otherwise sets cadence to 0.
- Power is multiplied by `userConfig->getPowerCorrectionFactor()` and accepted from 1-2999 W.
- Peloton cadence/power can be ignored when an external BLE power meter is configured.
- If `userConfig->getPTab4Pwr()` is true, real sensor power does not overwrite watts because watts are derived from the power table.
- Resistance is currently accepted only from Grupetto devices; other sensor names are filtered out.
- Real resistance clears `rtConfig->resistance.simulate`.

## BLE Client

Primary files: `include/BLE_Common.h`, `src/BLE_Client.cpp`, `src/BLE_Common.cpp`.

`SpinBLEClient` owns:

- Connection flags: `connectedPM`, `connectedHRM`, `connectedCD`, `connectedCT`, `connectedSpeed`, `connectedRemote`.
- Scan flag: `doScan`.
- CSC cumulative values reused by server-side CSC/Cycling Power notifications.
- `myBLEDevices[NUM_BLE_DEVICES]`: slots for connected/discovered devices.

`SpinBLEAdvertisedDevice` stores:

- `advertisedDevice`, `peerAddress`, `uniqueName`.
- `connectedClientID`, service/characteristic UUIDs.
- Type flags: HRM, PM, CSC, CT, remote.
- `doConnect`, `isPostConnected`, `lastDataUpdateTime`.
- FreeRTOS queue for notification payloads.

Key functions:

- `SpinBLEClient::start()`: creates BLE client task and configures scanning.
- `ScanCallbacks::onResult()`: filters supported devices, updates `foundDevices`, sets slots to connect when user config matches.
- Scan results for current config apps are streamed one device at a time on custom-characteristic ID `0x32`, with begin/device/end records and per-peer MTU fragmentation. The legacy `foundDevices` JSON remains capped at 480 bytes for older apps; do not make it unbounded again.
- `SpinBLEClient::connectToServer()`: creates fresh BLE client, connects, sets slot state, removes duplicates.
- Device slots own immutable advertisement snapshots; read them through `getAdvertisement()` so disconnect callbacks cannot free a snapshot in use. Never retain a raw pointer from `onResult()`, because NimBLE deletes scan results on the next non-continuation scan. Pending connections take priority over starting another scan. `test/test_ble_scan_lifetime.py` covers snapshot lifetime, concurrent resets, and that scheduling boundary.
- `subscribeToAllNotifications()`: subscribes to notify/indicate characteristics for supported services.
- `SpinBLEClient::postConnect()`: completes service subscriptions, reads FTMS resistance range, starts FTMS training where needed, drains notification queues. Notification setup discovers only the characteristics the firmware consumes so large remote GATT tables do not exhaust the classic ESP32 heap. HID is the exception because remotes can expose multiple Report characteristics with the same UUID.
- `SpinBLEClient::checkBLEReconnect()`: sets `doScan` when configured devices are missing.
- `SpinBLEClient::adevName2UniqueName()`: stable names for saved device preferences. Public/static random addresses get address suffix; private random addresses prefer manufacturer-data suffix or base name.

`BLEServices::SUPPORTED_SERVICES` maps service UUIDs to the characteristic UUIDs this firmware expects. If adding sensor support, update this list, `SensorDataFactory`, and tests if parsing is involved.
The service table has one shared definition in `src/BLE_Common.cpp`; keep it out of the header to avoid allocating a separate vector and service-name strings in every translation unit.

## Sensor Parsing Library

Primary files: `lib/SS2K/include/sensors/*`, `lib/SS2K/src/sensors/*`.

`SensorData` is the abstract interface:

- Capability flags: `hasHeartRate()`, `hasCadence()`, `hasPower()`, `hasSpeed()`, `hasResistance()`.
- Getters return real values or sentinel values (`INT_MIN`, `nanf("")`) when absent.
- `decode(uint8_t* data, size_t length)` stores parsed fields.

`SensorDataFactory` caches parser instances by `(characteristicUUID, uniqueName)` so stateful parsers keep previous cumulative counters. It returns:

- `CyclePowerData` for Cycling Power Measurement.
- `HeartRateData` for Heart Rate.
- `FitnessMachineIndoorBikeData` for FTMS Indoor Bike Data.
- `FlywheelData` for Flywheel UART.
- `EchelonData` for Echelon data.
- `PelotonData` for Peloton aux serial payloads.
- `CscSensorData` for Cycling Speed/Cadence.
- `NullData` for unknown characteristic UUIDs.

Stateful parser caution:

- Cycling power and CSC cadence/speed require previous cumulative revolution/event-time state. Do not replace cached objects with one-shot parsing unless you account for that.

## BLE Server And FTMS

Primary files: `src/BLE_Server.cpp`, `src/BLE_Fitness_Machine_Service.cpp`, service-specific `src/BLE_*_Service.cpp`.

`startBLEServer()` creates the BLE server and starts services:

- Cycling Speed/Cadence
- Cycling Power
- Heart
- Fitness Machine
- SmartSpin2k custom characteristic
- Device Information
- BLE firmware update

The primary BLE advertisement carries the current WiFi IPv4 address in versioned SmartSpin2k manufacturer data.
The device name and 128-bit SmartSpin2k service UUID are kept in the scan response to stay within the legacy advertisement size limit.
IP changes rebuild the complete advertisement and scan-response payloads before advertising restarts; NimBLE's manufacturer-data setter appends fields and must not be used alone to replace the previous IP.

Zwift/OpenBikeControl services exist but are currently commented out in regular BLE advertising/setup; DirCon and the source files still matter.

`SpinBLEServer::update()` refreshes wheel/crank revolution counters, then calls service `update()` methods. The FTMS service also processes pending writes.

`MyCharacteristicCallbacks::onWrite()` queues FTMS control-point writes in `spinBLEServer.writeCache`. `BLE_Fitness_Machine_Service::processFTMSWrite()` consumes that queue.

FTMS control point behavior:

- `RequestControl`: success, clears watt target and simulated target watts.
- `Reset`: success, status reset/idle.
- `SetTargetInclination`: sets FTMS mode and `rtConfig->targetIncline`.
- `SetTargetResistanceLevel`: sets FTMS mode and `rtConfig->resistance.target`, clamping to min/max.
- `SetTargetPower`: sets FTMS mode, sets `rtConfig->watts.target`, optionally forwards corrected target to a connected FTMS trainer.
- `SetIndoorBikeSimulationParameters`: sets simulation mode incline and forwards to connected FTMS device.
- `SpinDownControl`: sets `spinBLEServer.spinDownFlag = 2`; BLE client task later calls `ss2k->goHome(true)` once cadence is present.
- `StartOrResume`, `StopOrPause`, `SetTargetedCadence`: update FTMS status/training status.

`BLE_Fitness_Machine_Service::update()` sends Indoor Bike Data from `rtConfig` and notifies DirCon. It computes speed from `rtConfig->simulatedSpeed` if available, otherwise from server power-based speed estimate. If resistance is not recently real, it simulates resistance from stepper position.

## Custom SmartSpin2k Characteristic

Primary files: `include/BLE_Custom_Characteristic.h`, `src/BLE_Custom_Characteristic.cpp`, `CustomCharacteristic.md`.

Protocol:

- `cc_read` reads a variable.
- `cc_write` writes a variable.
- Responses generally start with `cc_success` or `cc_error`, followed by the variable id and bytes/string.
- Reading `BLE_allSettings` returns a versioned, MTU-sized sequence of indications whose payloads concatenate into the JSON from `userConfig->returnJSON()`.

The giant switch in `BLE_ss2kCustomCharacteristic::process()` maps variable IDs to `userConfig`, `rtConfig`, and `ss2k` fields. Examples:

- Firmware URL, device name, WiFi SSID/password.
- Simulated watts/cadence/heart rate/speed.
- Simulate flags.
- FTMS mode and target watts.
- Shift step, shifter position, min/max brake watts.
- Stepper power/speed/direction/stealthChop.
- External control and sync mode.
- Save/reboot/reset flags.
- Power table row transfer.
- Homing min/max/sensitivity.
- `pTab4Pwr`, UDP logging, BLE logging.

`parseNemit()` compares current config/runtime values against static old copies and sends one notification per call for changed values. Some changes, like `hMin`/`hMax`, trigger `userConfig->saveToLittleFS()`. Turning `pTab4Pwr` on sets `spinBLEServer.spinDownFlag = 1` to trigger homing.

When adding a custom characteristic variable:

1. Add/confirm the ID in `include/BLE_Custom_Characteristic.h`.
2. Update `process()` read/write behavior.
3. Update `parseNemit()` if clients need change notifications.
4. Update `CustomCharacteristic.md`.
5. Preserve byte order conventions used nearby.

## Stepper And Resistance Control

Primary files: `include/Stepper.h`, `src/Stepper.cpp`.

Globals:

- `HardwareSerial stepperSerial(2)`
- `TMC2209Stepper driver`
- `FastAccelStepperEngine engine`
- `FastAccelStepper* stepper`

`SS2K::moveStepper()` is the key function:

- Updates `ss2k->stepperIsRunning` and `ss2k->currentPosition`.
- If `externalControl` is false:
  - ERG mode (`SetTargetPower`): `targetPosition = rtConfig->targetIncline`, with optional guardrails to avoid moving opposite the watt error.
  - Resistance mode (`SetTargetResistanceLevel`): calls `_resistanceMove()`.
  - Simulation mode: local gearing uses median-normalized ratio offsets plus `targetIncline * inclineMultiplier`; external/app-owned paths retain their existing controls.
- If `syncMode`, stops movement and sets current stepper position to target.
- Applies Peloton/resistance safety nudges and min/max step clamps.
- Calls `stepper->moveTo(targetPosition)`.
- Enables outputs only when cadence is present; otherwise auto-enable is used.
- Detects runtime stepper direction changes and updates the direction pin after current motion stops.

`_resistanceMove()` has two modes:

- Simulated resistance: maps 0-100 percent target resistance into known min/max step range. If no reliable range exists, it falls back by setting `watts.target` and switching to ERG mode.
- Real resistance: uses shared `include/ResistanceControl.h`. Exact target equality holds position. Adaptive derivative braking adds 0.5 s per confirmed crossing beyond the +/-2 level noise band, capped at 2.5 s; resets for a new target or ten seconds without control updates. Fresh unchanged feedback clears velocity; held reports older than 1.5 s stop braking. D only reduces approach movement, never drives away from target. Normal-mode D changes are logged; no persistent setting is added.

Homing:

- `goHome(false)` finds minimum/home only. Startup can use this.
- `goHome(true)` performs a full spindown/homing and saves `hMin/hMax`.
- If a real FTMS resistance-reporting device is connected, `_findFTMSHome()` calibrates from interior resistance transitions.
- Otherwise `_findEndStop()` uses TMC StallGuard, with repeated taps and drift detection.
- FTMS homing requires the Grupetto 0-100 scale, accepting advertised limits of 0/1 through 99/100. `include/FtmsHoming.h` measures transitions into 10 and 2 (upper: 90 and 98), averages steps per level across their eight-level separation, and extrapolates two levels. The far anchor uses an 80-step bracket because its error contributes only 0.25x to the result; the near anchor keeps a 20-step bracket. Probes grow from 150 to at most 600 steps on an unchanged level, with up to 3000 steps of plateau travel before no-progress failure; settling pauses alone must not reject a wide level 4. They use fast moves followed by full stationary confirmation; continuous travel still slows near each anchor. It approaches bracket probes from the interior to take up backlash. Stationary flipping across the requested adjacent pair is also a valid boundary position. A completed bracket is accepted even if the sensor skips the exact integer; do not add a final return-and-confirm move. These are virtual endpoints, not guaranteed physical stops; repeat full homing and relearn the power table when changing from the older timer-based origin. A missing or invalid FTMS PTAB map promotes startup to full calibration; valid maps recover coordinates from the same downward R51-to-R50 crossing used during calibration, within the overall safety deadline.
- FTMS endpoint approaches share the live resistance controller, capped at 1500 steps/s and 300 near the target (correction retries can reduce it). Observe feedback during the dwell: unchanged levels can confirm after one stationary second for the initial endpoint observation, with a fresh report required. Measurements used to locate a boundary retain a two-second acquisition guard: 1.3-second delayed sensor reports produced an 88-step origin error with a universal one-second dwell. Changed values restart their confirmation window; a back-and-forth across adjacent levels spanning a second is also accepted. Only the requested pair identifies that anchor; unrelated adjacent jitter is not homing success. It reads `rtConfig->resistance.getValueSample()` and its value-only timestamp, rejecting simulated data; the legacy timestamp also changes on target writes. Fresh but unsettled observations use a bounded average after five seconds; noise is not a settling failure. Delayed crossings and wrong-way reports can retry within one shared 120-second deadline per endpoint. A no-progress retry requires a net response beyond two-level noise. Stop before reversing/retrying. Missing real feedback, motor errors, cancellation, or sustained lack of resistance response stop homing. One adjacent pair of jitter must not prolong motor travel without progress. Native tests cover noisy anchors, shifted crossings, delayed feedback, legacy limits, and bounded failure. Bracket widths are software tolerances, not physical repeatability with analog noise.
- Read the homing clock after the sensor snapshot: a report published between the two reads otherwise underflows unsigned age checks. Only progress toward the target resets the no-progress timer. Any failed/aborted FTMS or mechanical home enters runtime-only `homingFallback`: stop, rebase the current position to zero, select Unlimited gear 0 and simulation mode, reset provisional travel limits, and learn a fresh RAM power table. Keep thermal/UART protection active; never latch normal motor movement solely because homing failed. Saved PTAB, bounds, ratio profile and pTab4Pwr preference survive; table-based power is temporarily bypassed so real watts can build the runtime table. Brake-watt limits are estimated even with real FTMS resistance feedback. A successful retry clears the override and restores configured gearing. Every search exit stops the motor; failure logs include the cause, resistance, age and position.
- Companion calibration widgets parse homing log phrases: preserve `Starting FTMS Homing`, `Homing to Min/Max Resistance... Current: ... Target: ...`, `Min position found`, `Max Position found: <steps>`, and `Homing procedure complete`. FTMS progress reports the active 10/2/90/98 endpoint target, then 67/58/50/33 during map sampling (58 stages the shared middle reference). `SpinDown_StopPedaling` (0x04) means minimum found / maximum search to the app; never emit it during the minimum search. Emit the maximum range and completion only after successful calibration/save. Keep `Homing aborted by user.` and `Homing timed out!` for their specific verdicts; `FTMS Homing timed out` is treated as a legacy nonterminal warning by the app.
- FTMS calibration stores stationary outer samples near R33/R67 and the downward R51-to-R50 crossing (R50.5) in a 32-byte FTM3 PTAB trailer. Full calibration and startup share the middle reference search: stage near R58, approach from above using only completed moves and settled feedback, then bracket to 80 steps. Bracket probes back off five estimated levels (capped at 6000 steps) to take up play. Only a downward approach can accept adjacent 50/51 jitter. Every reference observation requires at least two stopped seconds and fresh confirmation; noisy outer samples can average the latest two readings at five seconds. Monotonic adjacent changes restart confirmation. Map/startup moves use the shared resistance controller with measured travel gain and a 6000-step bound; final endpoint probes retain their two-second acquisition guard. FTM1/FTM2 metadata triggers one-time full recalibration, preserving saved watts during automatic migration. Match bike name, motor direction, zero minimum and maximum. Startup measures the same fixed crossing even when already in the middle, then subtracts its saved coordinate from the measured bracket midpoint; never substitute the final probe position. Successful FTMS spindown returns its procedure opcode to simulation and zero incline before selecting the gear; otherwise all ratio groupsets are bypassed and the recovered position becomes terrain. Successful homing clears `ftmsSimulationOffset` and sets the absolute starting-gear target (Unlimited: `8 * shiftStep`; ratios: the selected one-third gear's ratio offset). The maintenance motor loop applies that target after homing restores its safety policy; only subsequent ride-time synchronization adds an offset. `syncFtmsPosition()` rebases the complete coordinate offset at most once per minute: require 10 seconds of stationary feedback (adjacent-level jitter allowed), or 30 seconds for offsets above three local deadbands. Only applied corrections start the cooldown; driver-lock interruptions reset confirmation, not that timer. Ride-time estimates allow at most five levels of extension around the sparse samples, bounded to R30–70, with increased uncertainty; startup always uses the fixed crossing. Pause watts collection while `ftmsPositionUncertain`, then discard pending samples via `positionEpoch` on correction. Periodic sync-check logs report why correction is waiting. See `FtmsStartupHoming.md` and native/metadata/gearing integration tests.
- Homing aborts when shifter position changes.
- Homing changes driver current/speed/StealthChop and must restore normal driver setup. Always select FastAccelStepper automatic enable on homing entry, even when not inhibited. Switching to manual enable during cadence does not clear an old automatic-disable countdown; it can expire during calibration dwells, leaving software steps advancing with EN off. Automatic homing moves refresh/re-enable outputs; the existing safety pause restores thermal interlocks on every exit. `test/test_homing_enable.py` covers long dwells and retry after expiration.

Stepper safety:

- Homed devices clamp to `rtConfig->minStep/maxStep`.
- Unhomed devices use provisional defaults unless power-table/resistance updates refine limits.
- FastAccelStepper pulse generation is initialized independently of TMC UART detection, so the firmware remains safe when the physical driver is absent. Runtime stepper-setting methods must still tolerate null driver/stepper pointers in case peripheral allocation fails.
- Do not bypass `moveStepper()` target clamping for ordinary control paths.
- Thermal/UART safety runs every 10 seconds in maintenance, including during updates, but pauses for the entire `goHome()` scope (including early exits). StallGuard homing keeps its original moves, reads, current settings and between-tap restores: no added thermal derating, EN/queue resets, IFCNT checks or thermal safety aborts. The enable callback passes through homing requests. The maintenance safety check takes a nonblocking driver lock; the homing pause uses that lock only at entry/exit, never during movement.
- At homing scope exit, synchronously restore the saved current limits and motor interlock under the driver lock. Close the enable bypass atomically with its restored policy and stop queued motion for existing inhibits too; a previously latched inhibit is not a new transition. Advance the existing TMC cooldown deadline without clearing it or waiting for the next poll. Healthy return-to-zero moves remain queued.
- Outside homing, EN stays high until startup setup can read the chip and advance IFCNT (no exact write-count/byte-count requirements). Once configured, failed status reads only log/retry; they do not disable or invalidate the driver. Confirmed driver resets trigger reconfiguration. Current is written on setup, settings changes or thermal-limit changes, not every poll. `include/ThermalSafety.h` owns the tested policies: TMC T120 halves requested current and disables EN after 30 seconds without cooling; OT disables immediately, and only valid clear temperature flags release the latch. S3 radios reduce at 70 C (WiFi 8.5 dBm/modem sleep, BLE minimum -24 dBm including active links); motor current tapers from 100% at 70 C to 50% at 80 C, with EN high above 80 C until <=78 C. Radios restore below 68 C. Temperature sensor failure inhibits the motor. S3 logs `T=%dC` every 10 seconds outside homing. Limits never change saved user current; the stricter current limit wins.

## Virtual Gearing

`lib/SS2K/include/VirtualGearing.h` maps sorted ratio arrays to integer motor offsets. `shiftStep` is the distance for the median positive adjacent ratio gap; even medians average the middle two gaps. Gear 1 has zero offset; duplicates share offsets, all-identical profiles stay at zero, and absolute calculation avoids rounding drift. `src/VirtualGearing.cpp` adds the existing terrain incline offset. Both boards use this in local simulation/inclination modes without weight, cadence, calibration/trust gating, or timed effects. Full targets pass through `moveStepper()` travel clamps. ERG, resistance, external control and app-owned shifting retain their own behavior. Unlimited is the default: an empty ratio array gives fixed `shifterPosition * shiftStep` offsets, no logical gear bounds, and start position 0 while unhomed or 8 after successful homing. Bounded profiles start at `max(1, gearCount / 3)` rounded down (24 gears: 8; 12/13 gears: 4) at both startup and homing, and clamp to the profile count. FTMS and mechanical homing both apply the selected starting gear from the calibrated zero, not from the final search position. Homing owns both the successful start-gear reset and the Unlimited gear-0 fallback after failure/abort; the BLE caller must not reset gears a second time. Other control modes retain their legacy reset. Cache only accepted local gears after travel checks so a rejected Unlimited shift cannot return after a mode change. Motor travel guards apply in both modes. Median calculation happens before the profile's short publication lock.

`userConfig.gearRatios` persists an empty array for Unlimited (default), or 2–26 sorted uint16 ratios in thousandths. Existing saved profiles are preserved. BLE count 0 (`02 34 00`) selects Unlimited; indexed reads then return an error. Custom ID `0x34`, HTTP settings, and both web asset trees expose it. The web groupset dropdown maps road/MTB/gravel presets to arrays; unmatched arrays are retained. Full 26-gear BLE writes need MTU >=58; metadata/indexed reads fit MTU 23. The experimental weight ID 0x33 is retired. See `VirtualGearing.md` and `CustomCharacteristic.md`. Run native tests and both firmware/filesystem builds for changes.

## ERG Mode

Primary files: `include/ERG_Mode.h`, `src/ERG_Mode.cpp`.

`moveStepper()` calls `prepareMode()` before interpreting an ERG target: carry the actual motor position into `targetIncline` so a SIM grade cannot become an unintended motor command. Reset transient controller state on mode changes.

`ErgMode::runERG()` is called from the main maintenance loop. It:

- Waits for stepper completion and power acquisition after conservative table seeks or proportional corrections with more than 50 W error.
- Saves power table after delayed `saveFlag`.
- Loads power table once per session.
- Adds live power/cadence/position samples to the power table when cadence exists and `pTab4Pwr` is false.
- Calls `computeErg()` when FTMS mode is target power and a power meter or simulation is active.
- Periodically updates stepper min/max from power table.
- Handles power-table reset flag.
- If `pTab4Pwr` is true, estimates watts from cadence and current stepper position using `PowerTable::lookupWatts()`.

`computeErg()`:

- Keeps ERG active and lowers its target to `userConfig->minWatts` if cadence is below `MIN_ERG_CADENCE`.
- Raises target to `userConfig->minWatts` when apps request too little.
- Skips if the same watt timestamp/target was already processed or current watts are negative.
- For large setpoint changes, tries `_setPointChangeState()` using the power table when homed.
- Trusted forward seeks permit extrapolated watts/cadence when the local forward surface increases and the position fits calibrated travel; they do not require measured-bin bounds or a valid local derivative. They also handle small target changes and maintenance cadence changes of at least 3 RPM. Cadence maintenance uses position differences to preserve the feedback correction already learned. Cadence updates cannot extend a seek beyond ten seconds. Ordinary crossings settle; excessive overshoot hands off immediately. Stale power stops the seek at the existing 1.5-second threshold, but transfers pending motion/acquisition into a feedback wait before another correction. Seek timeouts preserve that acquisition too. Cadence compensation retains the original table-position delta and refuses a move that compounds a power error over 20 W. Applied feedback corrections/acquisition refresh the cadence reference to avoid compensating twice.
- Falls back to `_inSetpointState()` proportional control.
- Writes the new target to `rtConfig->targetIncline`.
- While homed with a real power meter, value-only power timestamps validate the table against actual watts plus/minus `ERG_MODE_PID_WINDOW`, including extrapolated points. Negative evidence requires stationary acquisition time; seek overshoot or timeout alone is not negative evidence. Confidence gains/loses one point per eligible observation, trusts at 16, caps at 24 and revokes at 8. Persistent stationary mismatch still revokes trust.
- `ERG_GUARDRAILS` is disabled by default; seek direction, travel bounds, overshoot handling, and timeouts live in the ERG controller instead of the stepper loop.

`_setPointChangeState()`:

- Chooses increasing/decreasing mode.
- Uses a trusted direct forward lookup, otherwise a relative forward-table correction. The older watt/cadence-offset seek is only a fallback when the relative lookup cannot establish a useful direction.
- Rejects table results that move the wrong way or become negative while homed.
- Feedback waits require actual motor completion and a fresh value sample at least 2.5 seconds later. An absent or still-approaching response can extend this to the bounded five-second feedback deadline; excessive overshoot releases it immediately. Movement and overall timeouts are bounded; timeouts consume held power so repeated target writes cannot restart corrections. New targets, mode changes and stopped cadence cancel waits. Housekeeping continues. A seek or feedback wait under 100 steps does not by itself block collection; track the whole command/actual-position range, including retargets, rather than remaining travel. Substantial acquisition still excludes delayed power from learning/confidence.

`_inSetpointState()`:

- For errors above 20 W, uses the forward position difference between measured/anticipated and requested watts, including sparse-row interpolation and extrapolation. At sensitivity 5 the correction fraction is 1 when trusted and 0.5 otherwise, bounded by motor speed/travel and followed by acquisition waiting. A two-second projection of fresh power trend only reduces corrections already approaching target; a fixed residual has no new dead band. Smaller errors or unusable forward differences retain proportional control with `ERGSensitivity`.
- Keeps the original strict `lookupSlope()` for table validation. ERG uses `lookupErgSlope()`, which may use a near-edge measured segment only when the cadence-bounding rows agree and each segment has endpoint headroom; it never extrapolates beyond measured data.
- Blends a trusted ERG table gain 50/50 with the watt-scheduled fallback gain and bounds raw table gain to 0.5-1.25x fallback before blending. This deliberately favors stable convergence over aggressive corrections. Fallback log lines include the rejected-slope reason.
- Scales gain by watt error size.
- Caps movement by stepper speed and `ERG_MODE_DELAY`.
- Fallback errors over 50 W use the same movement/feedback wait when no usable table difference exists. `python -B -m unittest discover -s test -p test_erg_feedback.py` exercises production orchestration and forward lookup with fake peripherals: sparse 60/100 RPM rows, requests beyond recorded watts, 1 Hz delayed power, cadence changes, trust retention/revocation, and stale/time-out paths. Simulated settling is not hardware validation; unexpected plant changes can still exceed the ten-second target.

## Power Table

Primary files: `include/Power_Table.h`, `include/PowerTable_Helpers.h`, `src/Power_Table.cpp`, `src/PowerTable_Helpers.cpp`.

Purpose:

- Learn mapping between watts, cadence, and stepper target position.
- Use that mapping for ERG setpoint jumps and optional power estimation (`pTab4Pwr`).
- Infer min/max stepper limits when not using homing or real resistance feedback, and always after homing failure.

Data structures:

- `PowerEntry`: raw sample with watts, cadence, target position, resistance, reading count.
- `PowerBuffer`: fixed `POWER_SAMPLES` sample buffer used before committing a table entry.
- `TableEntry`: stored fields `int16_t targetPosition`, `int8_t readings`, plus runtime-only fractional fitted position and publication marker. Serialize the two stored fields explicitly; never serialize the struct.
- `PTData`: `POWERTABLE_CAD_SIZE` x `POWERTABLE_WATT_SIZE` table.
- `PTHelpers`: indexing, measured-point interpolation/extrapolation, cleaning, and monotonic enforcement.

Table dimensions/constants are in `include/settings.h`:

- Watts increment: `POWERTABLE_WATT_INCREMENT`.
- Cadence starts at `MINIMUM_TABLE_CAD`, increment `POWERTABLE_CAD_INCREMENT`.
- Positions are divided by `TABLE_DIVISOR` when stored to save memory.
- `INT16_MIN` marks empty table positions.
- `readings == 1` means inferred/low-confidence; human/real readings are generally `2+`.

Flow:

1. `PowerTable::processPowerValue()` consumes each real value timestamp once (target writes do not count), including repeated equal watts. Three fresh reports, at least 750 ms apart, form a window after a two-second acquisition guard. Reports/gaps over 1.5 seconds, substantial seeks/feedback waits, stops, simulated/table-derived watts, uncertain FTMS coordinates, or position-epoch changes reset acquisition. Position span is at most 100 full steps, cadence span 3 RPM, power span max(20 W, 15% of midpoint). Small motor corrections within the span are allowed; substantial pending travel blocks learning. Call collection even when learning is disabled so old partial windows cannot survive. Collection-reset diagnostics identify discarded windows; cumulative small movements must still fit the 100-step observation span.
2. A full `PowerBuffer` is averaged in `PowerTable::newEntry()`.
3. `newEntry()` normalizes watts to the cadence row using equal torque, then adjusts position to the watt-bin center using the local forward slope. Without a slope, retain one off-grid observation per row until a separated stable observation supports a positive slope; do not relabel raw off-grid positions. Pending anchors are runtime-only and invalidated on load/import/coordinate changes.
4. `PTHelpers::enterData()` retains fractional estimates with at most four observations of historical weight (independent of the persisted reliability cap of 20), then applies weighted monotonic fitting to populated rows/columns. The prior is the previous fitted surface: consistent conflicting evidence moves neighbors together without vetoes or deleting anchors. Empty cells stay empty; every changed cadence row is notified. Load/import/reset invalidate runtime estimates. `test/test_ftms_sync_collection.py` exercises production collection, normalization, and fitting with synthetic fresh reports.
5. `lookup()` locally interpolates or extrapolates measured watt/position pairs, using equal-torque cadence scaling, and returns a full-scale position.
6. `lookupWatts()` numerically inverts the cadence-blended forward `lookup()` surface, preserves exact measured anchors/plateau midpoints, and applies a monotonic cadence envelope for `pTab4Pwr`. Estimated power is bounded to the FTMS 4000 W maximum.

Persistence:

- `_manageSaveState()` loads/saves `POWER_TABLE_FILENAME`.
- Watts-table saving/loading requires `rtConfig->homed`; FTMS metadata is read separately before homing.
- File format starts with `TABLE_VERSION`, saved reading quality, and saved homed state, then table entries.
- `_save()` refuses empty saves unless valid FTMS calibration metadata exists; complete files replace saves through a temporary-file rename.
- `reset()` is an explicit destructive reset. Homing searches use `clearRuntime()` to discard RAM coordinates without changing saved PTAB/settings. Full mechanical recalibration resets the saved table only after both endpoints validate (unless pTab4Pwr is selected); successful startup recovery may then reload saved watts. Failed sessions cannot load/save watts. Negative relative positions are valid runtime samples. FTMS finalization loads legacy watts with `_manageSaveState(false, false)` so only its final atomic save can replace the old PTAB.

Caution:

- Runtime lookup uses only entries with `readings >= 2`; legacy inferred cells with `readings == 1` are ignored.
- Power-table and homing logic are tightly linked now. Loading/saving without homing is intentionally blocked.

## DirCon

Primary files: `include/DirConManager.h`, `src/DirConManager.cpp`, `include/DirConMessage.h`, `src/DirConMessage.cpp`.

DirCon exposes BLE-like services over TCP:

- Starts a WiFi server on `DIRCON_TCP_PORT`.
- Publishes MDNS service and BLE service UUID TXT records.
- Handles discover-services, discover-characteristics, read, write, enable-notifications, and unsolicited notification messages.
- Services register with `DirConManager::registerService()`.
- FTMS registers a write handler in `BLE_Fitness_Machine_Service::setupService()` so DirCon writes to the FTMS control point run the same control logic as BLE writes.
- The SmartSpin2k custom service registers a write handler so its request/response protocol also works over DirCon; changed-value notifications and chunked all-settings snapshots are mirrored over TCP.
- BLE server updates call `DirConManager::notifyCharacteristic()` so TCP clients receive corresponding updates.

DirCon uses static buffers and fixed client/subscription arrays. Be cautious with dynamic allocation and payload sizes.
Outbound DirCon frames use bounded per-client queues and are drained with non-blocking socket sends only from `DirConManager::update()`. Keep responses and notifications on that path so slow or vanished TCP peers cannot block firmware tasks or interleave frames.
DirCon notification broadcasts check for an active subscribed recipient before encoding, avoiding unnecessary dynamic allocations during BLE scans.

## HTTP/Web UI

Primary files: `src/HTTP_Server_Basic.cpp`, `include/HTTP_Server_Basic.h`, `data/*`.

Responsibilities:

- Start/stop WiFi (`startWifi()`, `stopWifi()`) and poll connectivity in `updateWifi()` from the existing maintenance task. Configured devices remain in STA mode. Auto-reconnect is disabled so failed station attempts are paced: up to three app-initiated attempts 15 seconds apart, then a 60-second pause before another burst. The Arduino core still performs one unconditional first-connect retry after a disconnect. AP mode is reserved for an unconfigured SSID. mDNS and DirCon start only after an IP is available and stop on station loss. Clock sync is polled without a startup wait. No additional task stack is allocated.
- Serve LittleFS web assets and built-in OTA pages.
- Before BLE starts, boot checks the local `list.json`. If every listed asset exists, no TLS connection is created and WiFi startup does not wait. Only when files are missing, `HTTP_Server::syncWebServerFiles()` waits up to ten seconds for the station and repairs synchronously before BLE starts. If the station remains unavailable, normal startup continues in STA mode.
- Browser-uploaded firmware uses the low-level ESP-IDF OTA API, and filesystem images stream directly to the LittleFS partition with sector-at-a-time erases. Neither path uses Arduino `Update` or its 4 KiB heap allocation on memory-constrained classic ESP32 builds. Filesystem uploads must exactly match the partition size; arbitrary file uploads are rejected.
- Web filesystem repair fetches the remote `list.json`, downloads only missing assets, and installs the fetched manifest only after repair succeeds. It never performs boot-time version upgrades or prunes existing files.
- Downloads use bounded HTTP/TLS timeouts and temporary files so partial assets are never served. Repair completes before BLE allocation/scanning, avoiding their peak internal-RAM loads overlapping on classic ESP32.
- Settings JSON/API behavior through `settingsProcessor()`.
- Periodic web client update through `webClientUpdate()`.
- BLE scanner page support.

Web UI changes usually need matching firmware handlers when adding settings. Config fields are not automatically exposed unless `settingsProcessor()` and/or the custom BLE characteristic know about them.

## Logging

Primary files: `src/SS2KLog.cpp`, `include/SS2KLog.h`, `src/*Appender.cpp`.

Use `SS2K_LOG*` macros rather than raw `Serial.printf` unless matching nearby code or during very low-level diagnostics. Log sinks:

- Serial/log handler.
- UDP when enabled.
- WebSocket.
- BLE custom logging through `BleAppender`.

`DEBUG_BLE_TX_RX`, `CUSTOM_CHAR_DEBUG`, `DEBUG_POWERTABLE`, `DEBUG_DIRCON_MESSAGES`, and `DEBUG_STACK` in `settings.h` unlock extra diagnostics. Be aware that verbose BLE/Peloton logging can be noisy or timing-sensitive.

## Configuration Constants

`include/settings.h` is central. It contains:

- Firmware update URLs and LittleFS filenames.
- Device/WiFi defaults.
- Stepper power, speed, acceleration, travel, and driver tuning.
- ERG constants and compile-time feature flags.
- Power-table sizes, increments, and quality constants.
- BLE timing, reconnect, stack, and buffer sizes.
- Peloton aux serial constants.
- Homing thresholds and sensitivity defaults.

Board-specific pin mappings, driver sense resistance, current scaling, revision detection values, homing capability, and homing-sensitivity scaling live in `include/boards.h`.

When changing behavior, prefer adjusting named constants instead of scattering magic numbers.

## Common Change Patterns

Adding a new sensor parser:

1. Add `SensorData` subclass in `lib/SS2K/include/sensors` and `lib/SS2K/src/sensors`.
2. Add service/characteristic UUIDs to `include/Constants.h` or `include/BLE_Definitions.h` as appropriate.
3. Add the service/characteristic to `BLEServices::SUPPORTED_SERVICES`.
4. Add factory branch in `SensorDataFactory::getSensorData()`.
5. Add native tests for parser behavior.
6. Make sure `collectAndSet()` accepts or intentionally ignores the new values.

Adding a persisted setting:

1. Add field/getter/setter/default in `userParameters`.
2. Update `setDefaults()`, `returnJSON()`, `saveToLittleFS()`, `loadFromLittleFS()`.
3. Add custom characteristic read/write/notify if the app controls it.
4. Add HTTP settings support if the web UI controls it.
5. Consider whether a live change needs immediate side effects, like updating stepper speed/power.

Changing ERG or resistance behavior:

1. Trace the relevant FTMS opcode in `processFTMSWrite()`.
2. Follow how `rtConfig` fields are updated.
3. Check `FTMSModeShiftModifier()` for shifter remapping side effects.
4. Check `ErgMode::runERG()` and `computeErg()`.
5. Check final clamps in `SS2K::moveStepper()`.
6. Run native tests and note any hardware validation needed.

Changing BLE server characteristics:

1. Update the service setup.
2. Update callback/write handling.
3. Update DirCon registration/notification if TCP clients should see it.
4. Update `CustomCharacteristic.md` or relevant docs.
5. Keep BLE packet sizes and little-endian encoding consistent.

## Known Pitfalls

- `Hardware/` is physical design material and should be ignored for firmware/software analysis unless explicitly requested.
- Some paths and folder names differ by case (`Hardware` vs `hardware`); use both excludes in searches.
- `compile_commands.json`, `.pio/`, `managed_components/`, map files, and generated logs are large/noisy.
- LED behavior is owned by `SS2K`/`src/Main.cpp`, not BLE common code. Its reboot inhibit flag is `RTC_DATA_ATTR`, which should survive `ESP.restart()` but clear on power loss.
- `SensorDataFactory` intentionally caches parser objects per unique device/characteristic.
- `Measurement` timestamps matter for ERG deduplication.
- `PowerTable` stores positions divided by `TABLE_DIVISOR`; lookup returns full-scale positions.
- `PowerTable` persistence requires homing.
- Saved BLE device identifiers are matched case-insensitively because NimBLE address formatting has changed between lowercase and uppercase across library versions.
- `SpinBLEAdvertisedDevice::reset()` updates global connected flags before clearing local flags.
- BLE address randomization is handled specially in `adevName2UniqueName()`; saved names depend on this behavior.
- `spinBLEServer.writeCache` is shared by BLE writes and DirCon writes.
- `spinDownFlag` is a state machine trigger, not just a bool: `1` means home/startup-ish, `2+` means full spindown/homing.
- `externalControl` bypasses normal target calculation but final state can still be affected by sync/clamping code.
- Firmware OTA paths validate the incoming `esp_image_header_t` chip ID before starting flash writes; filesystem images are intentionally exempt from application-image validation.
- BLE firmware OTA uses a length-aware versioned protocol documented in `BLEFirmwareUpdateProtocol.md`. It accepts variable data chunk sizes through writes with or without response, incrementally verifies CRC-32, and reports phase/error/byte-count status only through the firmware service control characteristic. Apps must wait for `Updating` before sending data; `Preparing` releases sensor links and erases the inactive partition outside the NimBLE callback. The server requests an ATT MTU exchange on connection and retries at OTA START, but transfers remain valid at MTU 23. Failed, aborted, disconnected, or 30-second-stalled transfers abort the inactive OTA handle and schedule a reboot; the boot partition is not changed until verification succeeds.
- Stepper UART initialization drives TX high for 20 ms before starting hardware UART. Outside homing, setup/recovery checks CRC-valid `IOIN.VERSION == 0x21` with one idle-high recovery attempt, then checks that setup writes advance IFCNT. Runtime polling has no IFCNT gate. Do not infer UART failure from zero `DRV_STATUS`; status must remain independent of connectivity. Homing retains its original `test_connection()` check. Safety events use always-enabled SS2K_LOG so release builds retain failure reasons. SmartSpin2k pins the doudar/TMCStepper fork to a tested commit; the library accepts valid zero-CRC replies (including IFCNT=174) and rejects missing/corrupt replies. The library owns its UART regression in tests/test_uart_read.py. Run python -B -m unittest discover -s test -p test_tmc_recovery.py for firmware polling/recovery regression tests. Initial OTP handling retains `test_connection()` and a separate CRC-valid/progressing `IFCNT` check; if that fails, irreversible OTP access is skipped. If `OTP_IHOLD` is verified as unprogrammed, firmware programs byte 2/bit 5 for the 9% standalone hold-current default, while incompatible existing OTP values are never modified.
- Many BLE and motor changes cannot be fully validated without hardware.

## Search Tips

Useful commands:

- List software files: `rg --files -g '!Hardware/**' -g '!hardware/**'`
- Find symbols: `rg -n "symbolName" src include lib/SS2K test`
- Find BLE UUID use: `rg -n "UUID|SERVICE|CHARACTERISTIC" include src lib/SS2K`
- Find config variables: `rg -n "getName|setName|BLE_name|jsonKey" include src data`
- Find FTMS behavior: `rg -n "FitnessMachineControlPointProcedure|SetTargetPower|SetIndoorBikeSimulationParameters" src include`

Prefer `rg` over slower recursive tools.
