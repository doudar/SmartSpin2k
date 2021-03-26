# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
### Added
- Added initial credits file.
- Added initial changelog.
- Enabled cpp-lint, pio check, and clang-format to enforce coding standards and catch errors.
- Added support for ruing pre-commit to run pre-push checks.
- Added github workflow on pull_request to validate changelog and coding standards.
- Add hyphens to Flywheel GATT UUIDs.
- Filter Flywheel advertisements by name.
- Added Insert Peloton 7 Flat .sldpart and .stl.
- Added logging library which supports levels

### Changed
- Fixed many issues exposed by the addition of cpp-lint, pio check, and clang-format.
- Fixed merge issues.
- Fixed Echelon licences.
- Fix Flywheel power/cadence decoding.
- Ignore zero heartrate reported from remote FTMS.
- Changed all logging calls to new logging library