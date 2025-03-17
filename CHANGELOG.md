# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

### Changed

### Hardware


## [25.1.28]

### Added

### Changed
-PID loop for ERG mode instead of P loop.

### Hardware


## [25.1.19]

### Added

### Changed
- Bugfix in ERG Mode
- Bugfix for spamming log messages when using Peloton and not homed. 

### Hardware


## [25.1.12]

### Added

### Changed
- All New HTML Files!

### Hardware


## [25.1.10]

### Added

### Changed
- Added checks for IC SE Bike Connection. 

### Hardware


## [24.12.8]

### Added

### Changed

### Hardware
- Added Dmasun bike
- Added Equinox Soul Cycle
- Added Sole SB700
- added bike mount for Joroto X2 and any other bike with hex shape front tube
- replaced old inserts for Joroto X2 with new 60.5

## [24.12.7]

### Added

### Changed
- Fixes homing not being removed after powertable reset.
- Shifting will always abort homing, even if homing hasn't been preformed yet. 

### Hardware

## [24.11.25]

### Added

### Changed

### Hardware
- Added rubber band holder to Peloton mount.
- Decreased Peloton insert size slightly. 
- Added rubber band holder to IC4/C6 mount.
- Added Sunny B1805 Bike. 

## [24.11.16]

### Added

### Changed

### Hardware
- Decreased tolerances around bearings and gears.


## [24.11.10]

### Added

### Changed
- Multiple Homing refinements.
- Working with resistance mode on QZ & Peloton
- PowerTable Import via Custom Characteristic fixed. 
- Check for cadence (before homing) so that we don't home when nobody is around.
- Don't depower the stepper if there is cadence. 

### Hardware
- Added Sunny B1805 insert. 

## [24.11.7]

### Added

### Changed
- Homing refinements.
- Resistance shifting improvement.
- Reduced Peloton logging to 1/sec.

### Hardware

## [24.11.5]

### Added
- Knob homing if calibrate trainer is selected in an app.

### Changed
- Added backing off of the stop before we test to prevent runaway grinding during homing. 
- User can abort homing by pressing shifter. 

### Hardware

## [24.10.30]

### Added

- Added pass through shifting in both ERG and SIM mode.
- Refined and added BLE custom characteristics for upcoming configuration app.
- Added CSC Service to BLE server.
- Added Yosuda-007C.
- Updated wiki banner.
- Added automatic update of Changelog sections on pull request to develop. 
- Added support for the Zwift gear display.

### Changed

- Amend always option to git describe.
- Updated communications overview picture.
- Updated kit purchasing links.
- MIN_ERG_CADENCE created and changed from 20 to 30.
- Fixed DNS server in AP mode.
- Fixed an issue with IC4 and variants not displaying device name in Bluetooth scanner. Fixes #500.
- Switched from using Power Table to a Torque Table for better compensation in cad variations.
- added test for invalid Peloton data to keep stepper from running away without resistance information.
- Fixed a bug with Trainer Day and rapid ERG sending.
- Many updates and bug fixes which enable the Config App to communicate with SmartSpin2k.
- Scanned devices no longer saved to filesystem. The new scanning method would keep snowballing them otherwise.
- increased MTU for android.
- Updated WiFi connection setup.
- Firmware no longer updates if only the html files need to be loaded.
- BLE scans blocked during firmware upgrade.
- Increased the default incline multiplier to 5.
- Added more robust activity monitoring and reboot every 30 minutes if there is no activity.
- Updated all references of SmartSkin2K to SmartSpin2k for consistency.
- Fixed bug where BT scanner "Loading" wouldn't disappear if "NONE" and "NONE" were selected.
- Fixed Bug where ERG setpoint state wasn't going to the positive control loop correctly.
- updated arm length readme for JLL IC400
- Added yokeWidth table to bike mount readme for bikes that use the OpenSCAD yoke.
- improved OpenSCAD for yoke to add roundness to the curve.
- Refactored BLE_Server into separate files for each BLE Service.
- Fixed bug in CPS service for Wahoo app (and probably others).
- Depreciated the SPIFFS->LittleFS upgrader.
- Increased ERG mode sensitivity.
- Removed extra logging when loading table.
- Prevent table returns from going in the wrong direction.
- Fixed bug with stepper speed not updating.
- Removed driver temp checking. It's not accurate on the ESP32.
- Peloton resistance limit enhancements.
- Continue updating power metrics to other clients if one client disconnects.
- Freed 19k of ram by consolidating tasks and using timers instead of delays.
- Updated baud rate to 115200 to ensure compatibility with other ESP32 variants.
- Added a final test to check if ERG mode has commanded a move in the proper direction.
- Aligned the values between the config app and web interface.
- Added ability to send target watts through the custom characteristic. 
- Added a final test to check if ERG mode has commanded a move in the proper direction.
- Cleaned up targetPosition to make it easier to understand. 

### Hardware

- added Yesoul S3.
- Wire diameter reduced from 7.2mm to 6.0mm on the window passthrough to accommodate the latest batch of cables.
- Changed reference to M4 bolt to M5 Bolt in the construction instructions pdf.
- Increased right side case mounting hole to 5.5mm so the bolt slides in easier.
- Added Pooboo and York SB300 Bikes.
- Increased size of the arm hardware holes by .25mm.
- Added Spinning L7 bike.
- Added Yosuda bike.
- Added Peloton low profile (for slammed bars) bike mount by @chaloney
- Updated CAD for the case to work flawlessly with small tweaks to motor height.
- Removed some free play in the IC4 insert.
- Added IC Bike SE
- Removed some free play in the IC4 insert.
- Added Bowflex Velocore bike.
- Added another Y cable picture for Peloton.
- Moved wire guard up 1 mm.
- Added JLL-IC400.
- Tightened up tolerances on the case.
- Increased gear spacing by .1mm
- Reduced bearing clearance by .15mm
- Added Stryde Bike.
- Added Life Fitness ICG8.

## [23.6.28]

### Added

- new photos for wiki
- Added battery monitoring of BLE devices by @Flo100. Implemented BLE HID shifting.
- Added table for arm lengths.

### Changed

- Disregard Peloton serial power and cadence if user has a BLE power Meter selected.
- Filesystem no longer updates when auto-update is unchecked.
- Holding shifter buttons on boot now erases LittleFS as well as resetting settings.
- Fixed bug where "none" hr still scanned. Credit to @xpectnil for discovering.
- Simplified Platform Packages to work better with newest version of PlatformIO.
- Fixed broken images in wiki.
- Valid files displayed on OTA page.
- Increased heap for more reliable OTA updates.

### Hardware

- Tweaks to IC4 bike mount
- directory cleanup
- tweaks to echelon bike mount
- Revised an old shifter cover for more options.
- Updated arm folder to procedurally generated arms ov various lengths.
- Updated C7 bike mount to use hook style arms.
- Updated PCB switch placement.
- Updated PCB Inductor.
- Updated PCB Motor Connector.
- Updated PCB Back Side Silkscreen Layer.
- Added fixed length arms.
- Added R3 assembly instructions.
- Added back a modified version of the single button shifter.
- Changed Logo font and position.
- Increased material around the top screw hole.
- Made shifter plugs slightly smaller.
- Increased diameter of shifter strain relief.

## [23.1.22]

### Added

- Added blocking for shifts above or below min/max setpoints.
- Added Peloton serial decoder to sensor factory.
- Added blocking for shifts above or below min/max set points.
- Added power scaler for new board.
- Added Main Index link to develop.html.
- Added feature to automatically reconnect BLE devices if both are specified.
- Added ftms passthrough. FTMS messages from the client app are now passed to a connected FTMS device.
- Added resistance capture to Echelon.
- Added Resistance capture to Flywheel.
- Added Resistance Capture to Peloton.
- Added Resistance capture to FTMS.
- Added scanning when devices are not connected.
- Added ability to set travel limits based on resistance feedback from a bike.
- Added shifting in ERG mode (changes watt target).
- Added shifting in resistance mode (changes resistance target.)

### Changed

- PowerTable values are now adjusted to 90 RPM cad on input.
- PowerTable entries are now validated against previous entries.
- Changes to default settings for better ride-ability. Raised incline multiplier and erg sensitivity, increased incline multiplier and max brake watts.
- Fixed a bug in the new cadence compensation where an int should have been a float.
- Fixed broken pre-commit on my local dev machine.
- Moved serial checking to own function.
- Reduced verbosity of ERG logging.
- Fixed instance of BLE PM dropdown not being saved correctly.
- Moved post connect handling to the ble communication loop. (improves startup stability)
- Fixed bug submitted by @flo100 where MIN_WATTS in ERG should have been userConfig.getMinWatts();
- FTMS resistance mode now changes the attached bike resistance with feedback. (i.e. setting resistance to 50 with a Peloton attached will set 50 on the Peloton)
- Refactored rtConfig to use more measurement class.
- Increased stepper speed when a Peloton is connected. (very light resistance)
- Updated libraries to latest

### Hardware

- Removed duplicate directory in direct mount folder.
- New case for the new PCB :)
- Revised directory structure in /hardware
- updated Bowflex C7 mount for improved usability
- updated Echelon knob insert for durability
- Peloton bike mount updated for improved usability

## [22.10.8]

### Added

- Automatic build script for github.
- Added dependabot.yml
- Added changelog merge automation.
- Added StreamFit
- Added developer tools html.
- Added automatic board revision detection.
- Added THROTTLE_TEMP to settings.h. The internal ESP32 temperature at which to reduce driver current.

### Changed

- Fixed a few compile issues for case sensitive operating systems.
- Release is now the default build option.
- New release is automatically created on pull request merge.
- Fixed HR in the hidden btsimulator.html
- Enabled CORS for doudar/StreamFit.
- Re-arranged index.html.
- restored link to bluetooth scanner.
- Reverted conditional variable initialization in powertable lookup function.
- Simplified cadence compensation in powertable lookup.
- Fixed issue where you couldn't set a ERG target less than 50W (MIN_WATTS wasn't being respected.)
- Increased the BLE active scan window.
- BLE scan page now shows previous scan results.
- BLE scan page duplicates bug fixed.
- BLE scan page dropdowns default to devices found during scan.
- Increased THROTTLE_TEMP from 72c to 85c.

### Hardware

- Ultra Short Direct Mount case for use on bikes with limited space between knob and head tube
- Direct mount and arm for Bowflex C7 - for use with Ultra Short Direct Mount

### Hardware

- Minor improvements to tolerances for direct mount mod
- created peloton-specific arm for direct mount use. IC4 model is usable, but a bit short.
- modified short case to include chamfers and fillets at the screw posts to improve thin wall printability in superslicer
- beefier arm for direct mount
- NEW: Direct Mount short case for bikes with reduced clearance in front of knob.
- NEW: Bolt through short case for direct mount use with Generic Bike http://smile.amazon.com/gp/product/B07S3YWSNM
- NEW: Direct mount for Life Fitness IC7

### Hardware

- Added new case design for upcoming integraded SMT PCB.
- Added Initial KiCAD PCB Commit.

## [2.7.9]

### Added

- Added comment when files are written to LittleFS.
- Added comment when firmware starts to update.
- Added setting for minWatts.
- Can now update LittleFS via update page.
- Removed dependency on jQuery. (Saves 30k in filesystem)

### Changed

- Driver Over Temp logging fixed.
- Updated Libraries to newest versions.
- Disabled setting of min/maxWatts if minWatts/maxWatts is 0.
- Added a check to workaround a bug where a powertable pair member was zero.
- Fixed a bug where a powertable pair could be returned that was larger than the powertable size.
- Changes to default settings.
- Fixed scanning memory leak.
- Scans continuously unless all devices are connected or set "none"

### Hardware

## [2.6.26]

### Added

- Added functions for automatic settings conversion from SPIFFS
-

### Changed

- updated CA for OTA updates
- Converted filesystem from SPIFFS to LittleFS
- Fixed endianness for ftmsPowerRange and ftmsPowerRange.

### Hardware

- added chamfer to screw posts in case body (direct mount mod)
- minor tweak to shifter cable retainer.

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
- Revised shifter for easier printing. Updated printing instructions.
- moved original shifter design into Archive directory

#### Direct mount Mod

- IC4 Mod renamed to Direct Mount Mod. Several directories have changed.
- bike mount and arm added for Echelon Connect Sport
- Arm design revised for added stiffness
- Case, arm and bike mount separated into individual CAD files for easier edits.
- Arm and bike mount re-drawn in CAD. It should be much easier to create designs for new bikes now.
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
- Added README.md to "~/Hardware/\*" that provides help for the files contained within.
- Added BakerEchelonStrap to "~/Hardware/Mounts/".
- Added positive retention clip to "~/Hardware/Mounts".
- Added Logan clip to "~/Hardware/Mounts".
- Added experimental rigid mounting strap. \* Fixed width to 65mm.
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

\*1.3.21

- SS2K BLE Server now accepts more than one simultaneous connection (you can not connect SS2K to both Zwift and another app simultaneously)
- Echelon bike is now supported
- SmartSpin2k.local more accessible with different browsers (fixed certain MDNS dropouts)
- Flywheel bike support built in (still untested)
- Backend (client) completely revamped to allow more device decoders, better stability, and faster network speeds.
- Lots of FTMS server and client polishing
- Added testing for decoders
- Versioning now comes from releases
- NimBLE library included
- Increased total max connections to 6 devices
- Refined debugging logs

\*1.2.15

- Fixed BLE cadence when accumulated torque values are present
- Lowered memory footprint

\*1.2.6

- Added limited Telegram BLE debugging information for development. No sensitive information is sent back. I can make this telegram info available as a private group (in Telegram) if anyone is interested in seeing it. This was added because there are a couple BLE devices that don't seem to conform to the standard protocol and we need more information about them to get them to work properly.
- Internal web UI links now use IP address instead of the local DNS name for compatibility with certain routers.
- Added a favorite icon (favicon.ico) for browser compatibility.
- Fixed an BLE bug which would occasionally cause a crash on scanning.
- Changed priority of subroutines and optimized task memory footprint.
- Streamlined the WiFi connect sequence.

\*0.1.1.22

- Power meter will now switch to the most recently connected and disconnect the other.
- Double power option in the Bluetooth scanner webpage.
- Bluetooth scanning now happens via a flag set in the client task.
- Backend of the Bluetooth scanning page revamped.
- Removed dedicated HTTP server callback for the Bluetooth scanner.

\*0.1.1.11

- All metrics now zero when correct BLE !connected.
- Added HR->PWR off/auto switch.
- WiFi starts up faster.
- BLE now connects device on save.
- BLE Scans are less error prone.
- MDNS Fix for certain android browsers.
- and probably lots of other stuff.

\*0.1.1.2 A new binary version is out in OTAUpdates. Units should automatically update to this newest version.

- Changes:
- WiFi Fallback to AP mode is now 10 seconds.
- WiFi AP mode Fallback SSID is now device name (MDNS name), and the password is whatever you have set.
- ERG Mode slightly more aggressive.
- StealthChop 2 now selectable in settings.
- Holding both shifters at boot resets the unit to defaults and erases filesystem. (firmware remains intact)
- Holding both shifters for 3 seconds after boot preforms a BLE device scan/reconnect.

- Bugfixes:
- Automatic Updates setting switch now works :)
