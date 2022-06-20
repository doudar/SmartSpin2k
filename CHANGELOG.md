# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

### Changed
 - updated CA for OTA updates
 - Converted filesystem from SPIFFS to LittleFS


### Hardware
 - added chamfer to screw posts in case body (direct mount mod)

## [2.2.8]

### Added
- Added screenshot for wiki main page
- Added functions to start and stop WiFi and Http server.
- Added Additional logging to the custom characteristic.
- Added option to enable/disable UDP logging. Default is disabled.
- Added Wiki links to most SS2K pages. [see #314](https://github.com/doudar/SmartSpin2k/issues/314)
- Added WebSockets for logging [see #173](https://github.com/doudar/SmartSpin2k/issues/173)
- Reworked logging to run log-appender outside the worker task (task no longer blocked by logger traffic).
- WebsocketsAppender can handle multiple (up to 4) clients. Status.html will reconnect if connection to websockets server is disconnected.

### Changed
- Refactored ERG.
- Reset to Default must be confirmed [see #51](https://github.com/doudar/SmartSpin2k/issues/51)
- Update Firmware: Upload dialog accepts .bin, .html and .css files. [see #98](https://github.com/doudar/SmartSpin2k/issues/98)
- Removed conflicting secondary BLE indicate when a shift was preformed via the custom characteristic.
- Default stepper power is now used on reset to defaults. 
- Refactored Main and HTTP Server.
- Changed from hard coding to Enums in BLEServer.
- Added simulateWatts to ERG mode internal check.
- Increased BLE Stack(s) and reduced ERG stack. 
- Disabled shifter ISR while ERG is running. 
- Fixed possible infinite loop in ERG when stepper never reached target position due to being past min or max position.
- When UDP logging is enabled, html will no longer request logging info. 
- Increased remote server minimum packet delay to 325ms and max to 700ms. 
- Updated Arduino_esp32 to the latest 2.0.2 version.
- Fixed all libraries to static releases.
- Reduced max_connect_retries from 10 to 3.
- Increased max_scan_retries from 1 to 2. 
- Now only send notifications for subscribed characteristics. 
- Increased JSON size for userConfig (hopefully fix config saving issues). 
- Changed LOGE messages in spiffs logging to regular LOG messages so they will display via network logging. 
- Complete BLE Client connection code rebase. 

### Fixed
- bluetoothscanner.html now lists fitness machine services in the PM list. 
- Fixed bug in external control. 

### Hardware
- Added Ultra Short Case mod which should allow as little as ~40mm from knob center to head tube. 
- Revised shifter for easier printing.  Updated printing instructions.
- moved original shifter design into Archive directory


#### Direct mount Mod
- IC4 Mod renamed to Direct Mount Mod.  Several directories have changed.
- bike mount and arm added for Echelon Connect Sport
- Arm design revised for added stiffness
- Case, arm and bike mount separated into individual CAD files for easier edits.
- Arm and bike mount re-drawn in CAD.  It should be much easier to create designs for new bikes now.
- Added direct mount for Revmaster and Peloton bikes
- New insert for Startrac

## [1.12.30]

### Added
- Added userConfig shifterDir to change direction of shifters in software to compensate for wiring
- Added userConfig StepperDir to change direction of stepper in software to compensate for wiring
- Added backend and html for shifter and stepper directions. 
- Added parameters for auto homing.

### Changed
- Fixed a couple bugs in PowerTables
- Fixed BLE Scanner webpage not displaying devices.
- Corrected a check in the FTMS write control point indication. 
- readme copy change
- added bridging improvements for screw holes - in cad but missing in STL

### Hardware
- 

#### IC4 Mod
- 

## [1.12.26]

### Added
- Added Webpage for Shifting.
- Added /shift server on backend.
- Split userConfig into userConfig and rtConfig.
- Added ERG testing to btsimulator.html
- Broke out ERG computation into it's own task.
- Added image for wiki.
- Replaced existing shifter housing with new and improved 2 in 1 revision

### Changed
- Adjusted the order of "Submit" "Reboot" and "Reset to Defaults" on the settings page. 
- Adjusted the setting webpage so "reset to defaults" is harder to accidentally press. 
- Increased the amount of free stack by removing the default Arduino loop();
- Updated /shift server on to rtConfig.
- Fixed redeclaring global targetposition in moveStepper().
- Renamed Settings page "Submit" button to "Save Setting"

### Hardware
- Increased hex head and nut size to 13mm.
- Increased depth on Knob Cup 2mm so a thicker nut can be used. 
- Added assembly .gif images.

#### IC4 Mod
- NEW: Hex bolt mod for 40t gear and matching ic4 cup/mount. This is a drop-in replacement for the plastic printed gear + cup/holder combination. This adds a lot of strength
- Renamed directory to something more apparent.
- Removed need for support material from case.
- Thicker slide design which removes need for washers.
- Slightly shorter slide - should allow more flexibility for ss2k placement on IC4.
- Tighter tolerances on case and bike mount for slide fitting. It should be a tight enough to prevent accidental removal
- Slightly larger diameter holes for m3 screws used in case assembly. Screws should be much easier to insert
- Additional tolerance for m5 fitting.
- Combined knob cup & knob insert for schwinn to reduce amount of plastic needed for the schwinn.
- Including a revised 11t stl - A bit more clearance on inner diameter the drive shaft on my steppers.

## [1.12.2]

### Added
- Firmware update will now download only spiffs files if missing without updating the firmware.
- New UDP logger by @MarkusSchneider .
- Added custom IC4 build and mount by @eMadman .

### BugFixes
- Power Correction factor now minimum .5 maximum 2.5 and added checks to stay within limits. 
- 404 now redirects to index file handler.
- settings_processor now checks shiftsteps field to determine if it's on the main settings page.

## [1.11.24]

### Added
- Moved FTMS callback decoding outside of the callback.
- Revamped the way notify buffer works as it was causing a memory leak.
- BLE Custom Characteristic motor driver calls now apply settings received.
- Motor current now automatically scales if ESP32 temp starts getting too high. 
- Added comments after compiler #endif Statements to make it easier to see what the partner #if statement is.
- Added BLE_syncMode to support syncing shifterPosition with bikes that also report their resistance level. 
- Added git tag to prevent branch from downgrading to the last release. 
- Added Hardware Version 2.0. 
- MCWPWM for stepper control.
- Erg Sensitivity control added.
- Function to stop motor and release tension if the user stops pedaling in ERG mode.  
- Received BLE is now buffered and then processed. 
- Added Fitness Machine supported inclination range characteristic.
- Additional unit tests. 

### Changed
- Renamed BLE_stepperPosition to BLE_targetPosition to clarify the variable it controls. 
- Increased BLE communications task to 3500 stack.
- Fixed recurring debugging line when driver was at normal temp.
- Fixed length of returnValue on custom BLE bool read requests. 

## [1.6.19] - 2021-6-19

### Added
- Initial implementation of the custom characteristic. 
- Added additional FTMS characteristics and some refactoring of shared variables
- Added GZipped jQuery to fix non WAN connected manual updates.
- Pin arduino-esp32 package to version 1.0.6 to fix build issue
- Added + - Buttons to sliders. 
- Added firmware checklist to "~/" for PR and release candidate testing.
- Added README.md to "~/Hardware/*" that provides help for the files contained within. 
- Added BakerEchelonStrap to "~/Hardware/Mounts/".
- Added positive retention clip to "~/Hardware/Mounts".
- Added Logan clip to "~/Hardware/Mounts".
- Added experimental rigid mounting strap. * Fixed width to 65mm. 
- Add images for video links in Wiki Build How To.
- Added webhook for simulated cadence. 
- Add image for video link in Build How To
- Added images for video links in Wiki Build How To
- Added XL (Extra Long) Mounting strap for Echelon.
- Added Insert Peloton 7 Flat V2 .sldpart and .stl.
- Added initial credits file.
- Added initial changelog.
- Enabled cpp-lint, pio check, and clang-format to enforce coding standards and catch errors.
- Added support for ruing pre-commit to run pre-push checks.
- Added github workflow on pull_request to validate changelog and coding standards.
- Add hyphens to Flywheel GATT UUIDs.
- Filter Flywheel advertisements by name.
- Added unit tests for CyclePowerData.cpp
- Add documentation to SensorData class.
- Enabled native testing.
- Added logging library which supports levels.

### Changed
- Moved Vin to the correct side on the ESP32 connection diagram. 
- Power Correction Factor minimum value is now .5
- Made Revmaster insert slightly smaller. 
- Fixed minor spulling errurs. 
- Reorganized hardware library into per part subfolders.
- Updater shifter cover to version 9.
- Fixed missing strap loops on non-pcb case.
- Power Correction Factor slider now updates correctly. 
- Removed unused http onServer calls.
- Repaired btsimulator.html
- Shortened HR characteristic to 2 bytes (Polar OH1 format)
- Increased ShiftStep UI slider range.
- Replaced DoublePower setting with PowerCorrectionFactor setting.
- Reverted bytes_to_u16 macro. 
- Erg mode tweak. 
- Added another test for Flywheel BLE name.
- Updated Echelon Insert
- Fixed many issues exposed by the addition of cpp-lint, pio check, and clang-format.
- Fixed merge issues.
- Fixed Echelon licenses.
- Fix Flywheel power/cadence decoding.
- Ignore zero heart rate reported from remote FTMS.
- Fix Assimoa Uno stuck cadence.
- Started extract non-arduino code into a cross-platform library.
- Changed all logging calls to new logging library.

### Removed
- Deleted and ignored .pio folder which had been mistakenly committed.

*1.3.21
* SS2K BLE Server now accepts more than one simultaneous connection (you can not connect SS2K to both Zwift and another app simultaneously)
* Echelon bike is now supported
* SmartSpin2K.local more accessible with different browsers (fixed certain MDNS dropouts)
* Flywheel bike support built in (still untested)
* Backend (client) completely revamped to allow more device decoders, better stability, and faster network speeds.
* Lots of FTMS server and client polishing
* Added testing for decoders
* Versioning now comes from releases
* NimBLE library included
* Increased total max connections to 6 devices
* Refined debugging logs

*1.2.15
* Fixed BLE cadence when accumulated torque values are present
* Lowered memory footprint
 
*1.2.6
* Added limited Telegram BLE debugging information for development. No sensitive information is sent back. I can make this telegram info available as a private group (in Telegram) if anyone is interested in seeing it. This was added because there are a couple BLE devices that don't seem to conform to the standard protocol and we need more information about them to get them to work properly. 
* Internal web UI links now use IP address instead of the local DNS name for compatibility with certain routers. 
* Added a favorite icon (favicon.ico) for browser compatibility.
* Fixed an BLE bug which would occasionally cause a crash on scanning. 
* Changed priority of subroutines and optimized task memory footprint. 
* Streamlined the WiFi connect sequence.  

*0.1.1.22
* Power meter will now switch to the most recently connected and disconnect the other.
* Double power option in the Bluetooth scanner webpage.
* Bluetooth scanning now happens via a flag set in the client task.
* Backend of the Bluetooth scanning page revamped.
* Removed dedicated HTTP server callback for the Bluetooth scanner.

*0.1.1.11
* All metrics now zero when correct BLE !connected.
* Added HR->PWR off/auto switch.
* WiFi starts up faster.
* BLE now connects device on save.
* BLE Scans are less error prone.
* MDNS Fix for certain android browsers.
* and probably lots of other stuff. 

*0.1.1.2 A new binary version is out in OTAUpdates. Units should automatically update to this newest version.

* Changes:
* WiFi Fallback to AP mode is now 10 seconds.
* WiFi AP mode Fallback SSID is now device name (MDNS name), and the password is whatever you have set.
* ERG Mode slightly more aggressive.
* Stealthchop 2 now selectable in settings.
* Holding both shifters at boot resets the unit to defaults and erases filesystem. (firmware remains intact)
* Holding both shifters for 3 seconds after boot preforms a BLE device scan/reconnect.

* Bugfixes:
* Automatic Updates setting switch now works :)
