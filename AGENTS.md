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
- `test/`: Unity tests for native PlatformIO environment.
- `data/`: web interface assets for classic ESP32 filesystem images.
- `data_s3/`: ESP32-S3 filesystem assets; initially mirrors `data/` but may grow independently.
- `.github/copilot-instructions.md`: older agent/build notes that may still be useful.

## Build And Test

PlatformIO is the expected entry point.

- Build firmware: `pio run --environment release`
- Build ESP32-S3 firmware: `pio run --environment S3release`
- Build filesystem: `pio run --target buildfs`
- Run native tests: `pio test --environment native`
- Static analysis: `pio check -e debug`
- Pre-commit checks: `pre-commit run --all-files`

`pio` is installed at `/Users/anthonydoud/.platformio/penv/bin/pio` when it is not on `PATH`; use that absolute command rather than treating a missing shell alias as an unavailable test environment. Native tests are expected to run locally.

S3 firmware and filesystem builds use `S3firmware.bin` and `S3littlefs.bin` as their native PlatformIO output/upload names. They also create `S3partitions.bin` and `S3bootloader.bin` copies for releases; the generic partition and bootloader intermediates remain because PlatformIO's flash uploader depends on those names.
The GitHub release archive includes firmware, merged factory, LittleFS, partition-table, and bootloader binaries for both classic ESP32 and ESP32-S3 targets.
GitHub Actions exports `SS2K_FIRMWARE_VERSION` from the date-based release tag before invoking PlatformIO. `git_tag_macro.py` requires that override in Actions so published firmware never receives a `git describe` commit suffix; local builds retain branch/commit version details.
The release workflow runs `cert_updater.py` once before firmware builds. Local PlatformIO builds use the checked-in `include/cert.h` and do not perform network-dependent certificate updates.

Filesystem builds stage deterministic gzip copies of every HTML/CSS source file under the environment build directory. They also refresh the checked-in `.gz` companions and `list.json` in `data/` or `data_s3/`, which are consumed by repository-based automatic OTA updates.

Important timing/network notes:

- PlatformIO firmware/filesystem builds and USB flashing are supported from the Codex environment. Local builds normally finish in under five minutes.
- A directly attached ESP32-S3 exposes its USB CDC serial port as `/dev/cu.usbmodem*` (currently `/dev/cu.usbmodem21101`). A debug build (`S3debug`, with `__DEBUG__` and `SERIAL_CUSTOM_CHARACTERISTIC`) can be observed directly with `/Users/anthonydoud/.platformio/penv/bin/pio device monitor -p /dev/cu.usbmodem21101`; confirm the present port with `ls /dev/cu.usb*` first. Serial monitoring is read-only, while uploading a debug build is an explicit device mutation and should only be done when the task authorizes it.
- When testing attached hardware, do not switch the development machine's WiFi connection to the SmartSpin2k access point: that network has no internet access, while builds and tooling may require internet service. Communicate with the device over USB unless the user explicitly directs otherwise. Preserve the device's stored WiFi/LittleFS/NVS settings by preferring application-partition-only flashing (S3 app offset `0x60000`).
- First PlatformIO builds/tests may download missing ESP32 platforms and toolchains and therefore take longer than normal.
- In restricted environments, PlatformIO can fail on network downloads. If that happens, report it rather than trying to fake validation.
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
6. Complete WiFi station connection or AP fallback, synchronize the clock when online, and repair missing web files before starting BLE.
7. Configure GPIO pins.
8. Initialize LED state; commanded-reboot quiet mode uses RTC memory so true power cycles still show startup blink behavior.
9. Configure TMC/FastAccelStepper via `SS2K::setupTMCStepperDriver()`.
10. Register log appenders.
11. Start BLE via `setupBLE()`.
12. Start web server and DirCon.
13. Create `SS2K::maintenanceLoop` task.

`SS2K::maintenanceLoop()` is the main cooperative loop. It roughly does:

- Every `BLE_NOTIFY_DELAY`: `BLECommunications()`, flush logs, websocket loop.
- If not updating and not in spindown: `ss2k->moveStepper()`, `ss2k->FTMSModeShiftModifier()`, `ergMode->runERG()`.
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

Used for `rtConfig->watts`, `hr`, `cad`, `batt`, and `resistance`.

Be careful: timestamp equality is used by ERG code to skip already processed watt samples. If adding setters or bypassing setters, update behavior can silently break.

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
- Scan delay state.
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
- IC Bike resistance is blacklisted due to non-standard behavior.
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
- `SpinBLEClient::connectToServer()`: creates fresh BLE client, connects, sets slot state, removes duplicates.
- `subscribeToAllNotifications()`: subscribes to notify/indicate characteristics for supported services.
- `SpinBLEClient::postConnect()`: completes service subscriptions, reads FTMS resistance range, starts FTMS training where needed, drains notification queues. Notification setup discovers only the characteristics the firmware consumes so large remote GATT tables do not exhaust the classic ESP32 heap. HID is the exception because remotes can expose multiple Report characteristics with the same UUID.
- `SpinBLEClient::checkBLEReconnect()`: sets `doScan` when configured devices are missing.
- `SpinBLEClient::adevName2UniqueName()`: stable names for saved device preferences. Public/static random addresses get address suffix; private random addresses prefer manufacturer-data suffix or base name.

`BLEServices::SUPPORTED_SERVICES` maps service UUIDs to the characteristic UUIDs this firmware expects. If adding sensor support, update this list, `SensorDataFactory`, and tests if parsing is involved.

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
  - Simulation mode: target is `shifterPosition * shiftStep + targetIncline * inclineMultiplier`.
- If `syncMode`, stops movement and sets current stepper position to target.
- Applies Peloton/resistance safety nudges and min/max step clamps.
- Calls `stepper->moveTo(targetPosition)`.
- Enables outputs only when cadence is present; otherwise auto-enable is used.
- Detects runtime stepper direction changes and updates the direction pin after current motion stops.

`_resistanceMove()` has two modes:

- Simulated resistance: maps 0-100 percent target resistance into known min/max step range. If no reliable range exists, it falls back by setting `watts.target` and switching to ERG mode.
- Real resistance: compares `resistance.target` and `resistance.value`, then increments `targetIncline` using larger moves for big errors and smaller moves for near-target corrections.

Homing:

- `goHome(false)` finds minimum/home only. Startup can use this.
- `goHome(true)` performs a full spindown/homing and saves `hMin/hMax`.
- If a real FTMS resistance-reporting device is connected, `_findFTMSHome()` homes by driving to reported min/max resistance.
- Otherwise `_findEndStop()` uses TMC StallGuard, with repeated taps and drift detection.
- Homing aborts when shifter position changes.
- Homing changes driver current/speed/StealthChop and must restore normal driver setup.

Stepper safety:

- Homed devices clamp to `rtConfig->minStep/maxStep`.
- Unhomed devices use provisional defaults unless power-table/resistance updates refine limits.
- FastAccelStepper pulse generation is initialized independently of TMC UART detection, so the firmware remains safe when the physical driver is absent. Runtime stepper-setting methods must still tolerate null driver/stepper pointers in case peripheral allocation fails.
- Do not bypass `moveStepper()` target clamping for ordinary control paths.

## ERG Mode

Primary files: `include/ERG_Mode.h`, `src/ERG_Mode.cpp`.

`ErgMode::runERG()` is called from the main maintenance loop. It:

- Waits for delayed stepper movement after large power-table seeks.
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
- Falls back to `_inSetpointState()` proportional control.
- Writes the new target to `rtConfig->targetIncline`.

`_setPointChangeState()`:

- Chooses increasing/decreasing mode.
- Looks up a position near the target watts/cadence with a PID window offset.
- Rejects table results that move the wrong way or become negative while homed.
- Adds delay based on step distance and configured stepper speed.

`_inSetpointState()`:

- Uses proportional-only control with `ERGSensitivity`.
- Keeps the original strict `lookupSlope()` for table validation. ERG uses `lookupErgSlope()`, which may use a near-edge measured segment only when the cadence-bounding rows agree and each segment has endpoint headroom; it never extrapolates beyond measured data.
- Blends a trusted ERG table gain 50/50 with the watt-scheduled fallback gain and bounds raw table gain to 0.5-1.25x fallback before blending. This deliberately favors stable convergence over aggressive corrections. Fallback log lines include the rejected-slope reason.
- Scales gain by watt error size.
- Caps movement by stepper speed and `ERG_MODE_DELAY`.

## Power Table

Primary files: `include/Power_Table.h`, `include/PowerTable_Helpers.h`, `src/Power_Table.cpp`, `src/PowerTable_Helpers.cpp`.

Purpose:

- Learn mapping between watts, cadence, and stepper target position.
- Use that mapping for ERG setpoint jumps and optional power estimation (`pTab4Pwr`).
- Infer min/max stepper limits when not using homing or real resistance feedback.

Data structures:

- `PowerEntry`: raw sample with watts, cadence, target position, resistance, reading count.
- `PowerBuffer`: fixed `POWER_SAMPLES` sample buffer used before committing a table entry.
- `TableEntry`: compact stored table cell: `int16_t targetPosition`, `int8_t readings`.
- `PTData`: `POWERTABLE_CAD_SIZE` x `POWERTABLE_WATT_SIZE` table.
- `PTHelpers`: indexing, measured-point interpolation/extrapolation, cleaning, and monotonic enforcement.

Table dimensions/constants are in `include/settings.h`:

- Watts increment: `POWERTABLE_WATT_INCREMENT`.
- Cadence starts at `MINIMUM_TABLE_CAD`, increment `POWERTABLE_CAD_INCREMENT`.
- Positions are divided by `TABLE_DIVISOR` when stored to save memory.
- `INT16_MIN` marks empty table positions.
- `readings == 1` means inferred/low-confidence; human/real readings are generally `2+`.

Flow:

1. `PowerTable::processPowerValue()` accepts sane cadence/watts and stable position samples.
2. A full `PowerBuffer` is averaged in `PowerTable::newEntry()`.
3. `PTHelpers::calculateIndex()` maps watts/cadence to table indexes.
4. `PTHelpers::enterData()` averages the cell, rejects monotonic violations, and runs PAVA-style monotonic correction across measured entries.
5. `lookup()` locally interpolates or extrapolates measured watt/position pairs, using equal-torque cadence scaling, and returns a full-scale position.
6. `lookupWatts()` numerically inverts the cadence-blended forward `lookup()` surface, preserves exact measured anchors/plateau midpoints, and applies a monotonic cadence envelope for `pTab4Pwr`. Estimated power is bounded to the FTMS 4000 W maximum.

Persistence:

- `_manageSaveState()` loads/saves `POWER_TABLE_FILENAME`.
- Saving/loading requires `rtConfig->homed`.
- File format starts with `TABLE_VERSION`, saved reading quality, and saved homed state, then table entries.
- `_save()` refuses to save with no valid readings.
- `reset()` clears table, resets homing settings, and rewrites/attempts save.

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

- Start/stop WiFi (`startWifi()`, `stopWifi()`).
- WiFi startup is deliberately linear: wait for the configured station up to the connection timeout, fall back to AP mode if needed, and only then continue firmware initialization.
- Serve LittleFS web assets and built-in OTA pages.
- Before BLE starts, boot checks the local `list.json`. If every listed asset exists, no TLS connection is created. `HTTP_Server::syncWebServerFiles()` is repair-only and runs synchronously when the local manifest is missing/invalid or a listed asset is absent and station internet is available.
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
- Stepper UART initialization drives TX high for 20 ms before starting hardware UART. Ordinary setup and power updates use TMCStepper's `test_connection()` with one idle-high recovery attempt. Initial OTP handling additionally requires the stricter CRC-valid/progressing `IFCNT` check; if that fails, ordinary setup continues but irreversible OTP access is skipped. If `OTP_IHOLD` is verified as unprogrammed, firmware programs byte 2/bit 5 for the 9% standalone hold-current default, while incompatible existing OTP values are never modified.
- Many BLE and motor changes cannot be fully validated without hardware.

## Search Tips

Useful commands:

- List software files: `rg --files -g '!Hardware/**' -g '!hardware/**'`
- Find symbols: `rg -n "symbolName" src include lib/SS2K test`
- Find BLE UUID use: `rg -n "UUID|SERVICE|CHARACTERISTIC" include src lib/SS2K`
- Find config variables: `rg -n "getName|setName|BLE_name|jsonKey" include src data`
- Find FTMS behavior: `rg -n "FitnessMachineControlPointProcedure|SetTargetPower|SetIndoorBikeSimulationParameters" src include`

Prefer `rg` over slower recursive tools.
