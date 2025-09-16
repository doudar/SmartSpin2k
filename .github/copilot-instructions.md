# SmartSpin2k - ESP32 Smart Trainer Firmware

SmartSpin2k is an ESP32-based DIY smart trainer project that converts any spin bike into a connected fitness device compatible with Zwift, TrainerRoad, and other training apps. The firmware controls stepper motor resistance, handles BLE communication, serves a web interface, and manages sensor data.

Always reference these instructions first and fallback to search or bash commands only when you encounter unexpected information that does not match the info here.

## Working Effectively

### Bootstrap Environment
Install required tools and dependencies:
- `sudo apt-get update && sudo apt-get install -y build-essential git python3 python3-pip`
- `pip install platformio pre-commit`
- `pre-commit install --hook-type pre-push`

### Build the Firmware
- **CRITICAL**: Build takes 15-45 minutes depending on network connectivity. NEVER CANCEL. Set timeout to 60+ minutes.
- `pio run --environment release` -- builds ESP32 firmware. NEVER CANCEL: Build takes 15-45 minutes on first run due to platform/toolchain downloads.
- `pio run --target buildfs` -- builds filesystem. Takes 2-5 minutes.
- **Network Issues**: If platform downloads fail with HTTPClientError, this is due to firewall/network restrictions. The build cannot proceed without internet access to download ESP32 toolchain.

### Testing
- **CRITICAL**: Native tests take 5-15 minutes. NEVER CANCEL. Set timeout to 30+ minutes.
- `pio test --environment native` -- runs unit tests using Unity framework. NEVER CANCEL: Takes 5-15 minutes on first run.
- Tests validate sensor data parsing, power calculations, BLE communication, and stepper motor control.
- **Network Issues**: Native platform download may fail with HTTPClientError due to firewall restrictions.

### Code Quality and Validation
- `pre-commit run --all-files` -- runs license header insertion. Takes 1-2 minutes.
- `pio check -e debug` -- runs cppcheck static analysis on debug environment. Takes 2-5 minutes. **Network Issues**: May fail with HTTPClientError due to platform download restrictions.
- Python build scripts (always work):
  - `python git_tag_macro.py` -- generates firmware version from git tags
  - `python build_date_macro.py` -- generates build timestamp  
  - `python cert_updater.py` -- updates SSL certificates (may fail with network issues)

### Run the Application
- **Build First**: Always complete the bootstrap and build steps before attempting to run.
- The application runs on ESP32 hardware - cannot be executed in the sandbox environment.
- Web interface available at device IP on port 80 when running on hardware.
- BLE services broadcast as "SmartSpin2k" when running on hardware.

## Validation

### Manual Testing Scenarios
After making code changes, always validate:
1. **Build Validation**: Ensure `pio run --environment release` completes successfully.
2. **Test Validation**: Ensure `pio test --environment native` passes all Unity tests.
3. **Code Quality**: Run `pre-commit run --all-files` and fix any license header issues.
4. **BLE Service Changes**: When modifying BLE services, verify characteristic UUIDs match the CustomCharacteristic.md specification.
5. **Power Calculations**: When changing power table or ERG mode code, run tests in test_pt_lookup_*.cpp files.
6. **Sensor Data**: When modifying sensor parsing, validate with tests in test_*Data.cpp files.

### Critical Areas to Test
- **Power Table**: Always validate power lookup and resistance calculations after changes to Power_Table.cpp or PowerTable_Helpers.cpp
- **BLE Services**: Test characteristic read/write operations when modifying BLE_*_Service.cpp files
- **ERG Mode**: Validate resistance control when changing ERG_Mode.cpp
- **Stepper Control**: Test motor control when modifying stepper-related code in Main.cpp

## Common Tasks

### Repository Structure
Key directories and their purpose:
```
/src                 -- Main ESP32 firmware source code
/lib/SS2K/src        -- Core library with sensor parsing and data structures
/include             -- Header files and configuration
/test                -- Unity unit tests for native environment
/data                -- Web interface HTML/CSS files
/Hardware            -- 3D printing files and PCB designs
/.github/workflows   -- CI/CD pipeline definitions
```

### Important Files
- `platformio.ini` -- Build configuration for ESP32 and native environments
- `include/settings.h` -- Hardware pin definitions and configuration constants
- `CustomCharacteristic.md` -- BLE characteristic specification and usage
- `src/Main.cpp` -- Main firmware entry point and setup
- `lib/SS2K/src/sensors/` -- Sensor data parsing classes

### Build Dependencies
External libraries loaded automatically by PlatformIO:
- NimBLE-ESP32 for Bluetooth Low Energy
- TMCStepper for stepper motor control
- FastAccelStepper for smooth motor movement
- ArduinoJson for configuration and web API
- ArduinoWebsockets for real-time web communication

### Configuration
- Default device name: "SmartSpin2k"
- Default WiFi password: "password"
- Web interface served on port 80
- BLE service UUID: "77776277-7877-7774-4466-896665500000"
- Over-the-air update URL: configured in settings.h

### Hardware Compatibility
- ESP32 DevKit v1 board (primary target)
- TMC2209 stepper motor driver
- Custom PCB designs in Hardware/ directory
- Support for multiple bike mount configurations

### Known Issues and Limitations
- **Network Connectivity**: Platform and toolchain downloads may fail due to firewall restrictions. All build commands (`pio run`, `pio test`, `pio check`) require internet access on first run.
- **SSL Certificates**: cert_updater.py may fail to fetch current certificates due to network restrictions
- **Hardware Testing**: Cannot test actual motor control or BLE communication without physical hardware
- **Build Times**: Initial builds require internet access and take 15-45 minutes due to large platform downloads

### Troubleshooting Common Issues
- **HTTPClientError during build**: This indicates network/firewall restrictions preventing platform downloads. No workaround available in restricted environments.
- **Platform not found**: Run `pio platform install espressif32` to manually install the ESP32 platform (requires internet).
- **Test failures**: Ensure you're running tests in native environment: `pio test -e native`
- **SSL certificate warnings**: Update certificates with `python cert_updater.py` or manually update `include/cert.h`
- **Build flag errors**: The Python scripts in build_flags must execute successfully. Test them individually if build fails.

### Environment Verification
Before working on the project, verify your environment:
```bash
# Check tools are installed
which python3 pio pre-commit
# Verify project configuration
pio project config
# Test build scripts
python git_tag_macro.py && python build_date_macro.py
```

### Debugging Tips
- Use `pio device monitor` to view serial output when connected to ESP32 hardware
- Check `include/cert.h` if experiencing SSL errors during firmware updates
- Monitor memory usage with DEBUG_STACK enabled in settings.h
- BLE debugging available through web interface at `/develop.html`

Always run `pre-commit run --all-files` before completing changes to ensure code meets project standards.