# Migration from PlatformIO to ESP-IDF

This document explains the migration of SmartSpin2k from PlatformIO to native ESP-IDF with CMake.

## Why Migrate?

1. **Direct ESP-IDF Access**: Use the latest ESP-IDF features without waiting for PlatformIO updates
2. **Better Component Management**: ESP-IDF's component system provides better dependency management
3. **Improved Build Control**: More control over build options and optimizations
4. **Official Tooling**: Use Espressif's official tools and documentation
5. **Reduced Complexity**: Eliminate the PlatformIO abstraction layer

## What Changed?

### Project Structure

**Before (PlatformIO):**
```
SmartSpin2k/
├── platformio.ini
├── src/                 # Main source code
├── lib/                 # Internal libraries
│   ├── SS2K/
│   └── ArduinoCompat/
├── include/             # Headers
└── data/                # Filesystem data
```

**After (ESP-IDF):**
```
SmartSpin2k/
├── CMakeLists.txt       # Root build file
├── sdkconfig.defaults   # ESP-IDF configuration
├── partitions.csv       # Partition table
├── main/                # Main application component
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── *.cpp
├── components/          # Components
│   ├── SS2K/
│   ├── ArduinoCompat/
│   ├── esp-nimble-cpp/  # External (not in repo)
│   ├── TMCStepper/      # External (not in repo)
│   ├── ArduinoJson/     # External (not in repo)
│   ├── FastAccelStepper/# External (not in repo)
│   └── ArduinoWebsockets/ # External (not in repo)
├── include/             # Global headers
└── data/                # Filesystem data
```

### Build System

**Before:**
```bash
pio run -e release            # Build
pio run -t upload             # Flash
pio run -t buildfs            # Build filesystem
```

**After:**
```bash
idf.py build                  # Build
idf.py flash                  # Flash
idf.py littlefs-create-partition-image littlefs data  # Build filesystem
```

### Configuration

| **PlatformIO (platformio.ini)** | **ESP-IDF (sdkconfig.defaults)** |
|----------------------------------|-----------------------------------|
| `board = esp32doit-devkit-v1` | `CONFIG_IDF_TARGET="esp32"` |
| `framework = arduino, espidf` | Arduino via `idf_component.yml` |
| `build_flags = -DFOO=1` | CMake: `target_compile_definitions()` |
| `lib_deps = ...` | Components in `components/` dir |
| `board_build.partitions = min_spiffs.csv` | `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"` |

### Dependencies

**Before:**
- Managed by `lib_deps` in `platformio.ini`
- Automatically downloaded by PlatformIO

**After:**
- Internal components: `components/SS2K/`, `components/ArduinoCompat/`
- External components: Cloned into `components/` via `setup_dependencies.sh`
- Arduino-ESP32: Managed by ESP-IDF Component Manager via `idf_component.yml`

## Migration Steps

### 1. Install ESP-IDF

Follow the [ESP-IDF installation guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html):

```bash
# Clone ESP-IDF
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git

# Install tools
cd ~/esp/esp-idf
./install.sh esp32

# Set up environment (add to ~/.bashrc or ~/.zshrc)
alias get_idf='. $HOME/esp/esp-idf/export.sh'
```

### 2. Set Up the Project

```bash
# Clone the repository
git clone https://github.com/doudar/SmartSpin2k.git
cd SmartSpin2k

# Setup external dependencies
./setup_dependencies.sh

# Activate ESP-IDF environment
get_idf

# Set target
idf.py set-target esp32
```

### 3. Build the Project

```bash
# Full build
idf.py build

# Flash to device
idf.py flash

# Monitor serial output
idf.py monitor

# Or flash and monitor in one command
idf.py flash monitor
```

## Removed Files

After successful migration and testing, the following PlatformIO-specific files can be removed:

- `platformio.ini` - PlatformIO configuration
- `.pio/` directory - PlatformIO build artifacts (already in `.gitignore`)
- `dependencies.lock` - PlatformIO dependency lock (kept for reference, not used)

## Configuration Mapping

### Arduino Component Selection

The project uses Arduino selective compilation to manage ESP32's limited IRAM. This configuration is now in `sdkconfig.defaults`:

```
CONFIG_ARDUINO_SELECTIVE_COMPILATION=y
CONFIG_ARDUINO_SELECTIVE_SPI=y
CONFIG_ARDUINO_SELECTIVE_Wire=y
CONFIG_ARDUINO_SELECTIVE_EEPROM=y
CONFIG_ARDUINO_SELECTIVE_Update=y
CONFIG_ARDUINO_SELECTIVE_FS=y
CONFIG_ARDUINO_SELECTIVE_LittleFS=y
CONFIG_ARDUINO_SELECTIVE_Network=y
CONFIG_ARDUINO_SELECTIVE_WebServer=y
CONFIG_ARDUINO_SELECTIVE_WiFi=y
CONFIG_ARDUINO_SELECTIVE_BLE=y
# ... etc
```

### Build Flags

**PlatformIO:**
```ini
build_flags =
    !python git_tag_macro.py
    !python build_date_macro.py
    -std=gnu++17
```

**ESP-IDF:**
These are now integrated into `CMakeLists.txt`:
```cmake
execute_process(COMMAND python3 git_tag_macro.py ...)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17)
```

### Partition Table

The partition table (`min_spiffs.csv` → `partitions.csv`) remains mostly the same:
- Changed partition name from `spiffs` to `littlefs` for clarity
- Same layout: 2MB flash with OTA support

## VSCode Integration

### PlatformIO Extension → ESP-IDF Extension

1. **Uninstall**: PlatformIO IDE extension
2. **Install**: [ESP-IDF extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)
3. **Configure**: Point to your ESP-IDF installation
4. **Build**: Use extension buttons or `idf.py` commands

### Tasks and Launch Configurations

The ESP-IDF extension provides:
- Build, flash, and monitor commands
- Debugging support
- Component configuration (menuconfig)
- Device management

## Troubleshooting

### Issue: Components Not Found

**Solution:** Run `./setup_dependencies.sh` to clone external dependencies.

### Issue: Arduino Headers Not Found

**Solution:** Ensure `arduino` is listed in `REQUIRES` in `main/CMakeLists.txt` and `main/idf_component.yml` includes the Arduino dependency.

### Issue: IRAM Overflow

**Solution:** The project carefully selects Arduino components to fit in IRAM. If you add new features:
1. Run `idf.py menuconfig`
2. Navigate to Component config → Arduino Configuration
3. Disable unnecessary Arduino components

### Issue: Build Fails with "IDF_PATH not set"

**Solution:** Source the ESP-IDF environment:
```bash
. $HOME/esp/esp-idf/export.sh
```

### Issue: Python Scripts Fail

**Solution:** Ensure Python 3.8+ is installed and the scripts have execute permissions:
```bash
chmod +x git_tag_macro.py build_date_macro.py cert_updater.py
```

## CI/CD Changes

The GitHub Actions workflow (`.github/workflows/build.yml`) now:
1. Uses `espressif/esp-idf-ci-action@v1` instead of PlatformIO
2. Runs `setup_dependencies.sh` to fetch external components
3. Uses `idf.py build` instead of `pio run`
4. Creates artifacts from `build/` directory instead of `.pio/build/`

## Advantages of ESP-IDF

1. **Latest Features**: Access to newest ESP-IDF features immediately
2. **Better Documentation**: Official Espressif documentation applies directly
3. **Component System**: Cleaner dependency management
4. **Build Performance**: CMake/Ninja is generally faster than PlatformIO's build system
5. **Debugging**: Better integration with ESP-IDF debugging tools
6. **Configuration**: More granular control via `menuconfig`

## Testing Checklist

Before removing `platformio.ini`, verify:

- [ ] Firmware builds successfully with `idf.py build`
- [ ] Firmware flashes and runs on hardware
- [ ] Web interface is accessible (LittleFS filesystem working)
- [ ] Bluetooth Low Energy communication works
- [ ] Stepper motor control functions correctly
- [ ] OTA updates work
- [ ] All hardware features tested and working
- [ ] CI/CD pipeline builds successfully

## Rollback

If you need to revert to PlatformIO:

1. Checkout the last commit before migration
2. Or restore `platformio.ini` from git history
3. Move `main/` back to `src/`
4. Move `components/SS2K/` and `components/ArduinoCompat/` back to `lib/`

## Additional Resources

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/index.html)
- [ESP-IDF Build System](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html)
- [Arduino-ESP32 as ESP-IDF Component](https://docs.espressif.com/projects/arduino-esp32/en/latest/esp-idf_component.html)
- [ESP-IDF Component Manager](https://docs.espressif.com/projects/idf-component-manager/en/latest/)

## Questions?

For issues or questions about the migration:
1. Check [BUILDING.md](BUILDING.md) for build instructions
2. Review this migration guide
3. Open an issue on GitHub
4. Join the [SmartSpin2k Facebook Group](https://www.facebook.com/groups/716297469953492/)
