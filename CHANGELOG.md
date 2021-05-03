# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
### Added
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
- Add documentation to SensorData class.
- Enabled native testing.

### Changed
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

### Removed
- Deleted and ignored .pio folder which had been mistakenly committed.
