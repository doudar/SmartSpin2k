# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- image for wiki

### Changed
- Increased hex head and nut size to 13mm.
- Increased depth on Knob Cup 2mm so a thicker nut can be used. 

### Hardware
#### IC4 Mod
- NEW: Hex bolt mod for 40t gear and matching ic4 cup/mount. This is a drop-in replacement for the plastic printed gear + cup/holder combination.  Adds a lot of strength
- rename directory to something more apparent
- Removed need for support material from case
- thicker slide design.  Removes need for washers
- Slightly shorter slide - should allow more flexibility for ss2k placement on IC4.
- tighter tolerances on case and bike mount for slide fitting.  It should be a tight enough to prevent accidental removal
- slightly larger diameter holes for m3 screws used in case assembly.  Screws should be much easier to insert
- additional tolerance for m5 fitting
- combined knob cup & knob insert for schwinn to reduce amount of plastic needed for the schwinn.
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
