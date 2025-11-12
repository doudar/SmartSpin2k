# Building SmartSpin2k with ESP-IDF

This project uses ESP-IDF (Espressif IoT Development Framework) with Arduino as a component for building the SmartSpin2k firmware.

## Prerequisites

1. **ESP-IDF v5.4 or later**
   - Install ESP-IDF following the [official installation guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)
   - Make sure to install the ESP32 toolchain: `./install.sh esp32`
   - Source the export script: `. $HOME/esp/esp-idf/export.sh` (or wherever you installed ESP-IDF)

2. **Python 3.8+**
   - Required for ESP-IDF build system

3. **CMake 3.16+**
   - Required for ESP-IDF build system

4. **Ninja build system** (recommended)
   - Install via package manager: `sudo apt-get install ninja-build` (Linux) or `brew install ninja` (macOS)

## Setting Up External Dependencies

The project uses several external Arduino libraries that need to be added to the `components/` directory:

### Option 1: Manual Clone (Recommended for Development)

```bash
cd components

# Clone esp-nimble-cpp (Custom NimBLE C++ wrapper)
git clone https://github.com/doudar/esp-nimble-cpp.git

# Clone TMCStepper (v0.7.3 - Trinamic stepper motor driver)
git clone --branch v0.7.3 https://github.com/teemuatlut/TMCStepper.git

# Clone ArduinoJson (v7.3.1 - JSON library)
git clone --branch v7.3.1 https://github.com/bblanchon/ArduinoJson.git

# Clone FastAccelStepper (High-performance stepper motor library)
git clone https://github.com/doudar/FastAccelStepper.git

# Clone ArduinoWebsockets (WebSocket library)
git clone https://github.com/doudar/ArduinoWebsockets.git
```

### Option 2: Using Git Submodules (Recommended for Production)

```bash
# Add as submodules
git submodule add https://github.com/doudar/esp-nimble-cpp.git components/esp-nimble-cpp
git submodule add -b v0.7.3 https://github.com/teemuatlut/TMCStepper.git components/TMCStepper
git submodule add -b v7.3.1 https://github.com/bblanchon/ArduinoJson.git components/ArduinoJson
git submodule add https://github.com/doudar/FastAccelStepper.git components/FastAccelStepper
git submodule add https://github.com/doudar/ArduinoWebsockets.git components/ArduinoWebsockets

# Initialize and update submodules
git submodule update --init --recursive
```

## Configuring the Build

### 1. Set Target (ESP32)

```bash
idf.py set-target esp32
```

### 2. Configure Project (Optional)

```bash
idf.py menuconfig
```

Key configuration options (already set in `sdkconfig.defaults`):
- Arduino components are selectively enabled to manage IRAM constraints
- Partition table: `partitions.csv` (2MB flash with OTA support)
- Compiler optimization: Size (`-Os`)
- FreeRTOS tick rate: 1000 Hz

## Building

### Full Build

```bash
idf.py build
```

### Build Firmware Only

```bash
idf.py app
```

### Build Filesystem (LittleFS)

```bash
idf.py littlefs-create-partition-image
```

## Flashing

### Flash Everything (bootloader, partitions, app)

```bash
idf.py flash
```

### Flash App Only

```bash
idf.py app-flash
```

### Monitor Serial Output

```bash
idf.py monitor
```

### Flash and Monitor

```bash
idf.py flash monitor
```

## Build Artifacts

After a successful build, the following files will be created in the `build/` directory:

- `SmartSpin2k.bin` - Main application binary
- `bootloader/bootloader.bin` - Bootloader binary
- `partition_table/partition-table.bin` - Partition table
- `littlefs.bin` - LittleFS filesystem image (if built)

## Cleaning

### Clean Build Artifacts

```bash
idf.py fullclean
```

## Troubleshooting

### Component Not Found

If you get errors about missing components, ensure all external dependencies are cloned into the `components/` directory.

### IRAM Overflow

The ESP32 has limited IRAM. The project uses selective Arduino component compilation to manage this. If you encounter IRAM overflow errors:

1. Check `sdkconfig.defaults` for Arduino component selection
2. Disable unnecessary Arduino components via `idf.py menuconfig` → Component config → Arduino Configuration

### Flash Size Issues

This project requires 2MB flash minimum. If you have a 4MB module, you can update the partition table in `partitions.csv` to utilize the additional space.

## VSCode Integration

### Install ESP-IDF Extension

1. Install the [ESP-IDF extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension) for VSCode
2. Configure the extension to point to your ESP-IDF installation
3. Use the extension's build, flash, and monitor commands

### Configuration

The extension should automatically detect the `CMakeLists.txt` and configure the project.

## Advanced Build Options

### Custom Build Flags

Edit `main/CMakeLists.txt` to add custom compile flags:

```cmake
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++17
    -DCUSTOM_FLAG=1
)
```

### Build Version Information

The build system automatically generates version information from git tags:
- `git_tag_macro.py` - Generates firmware version from git tags
- `build_date_macro.py` - Generates build timestamp

These scripts are called automatically during the build process.

## Migrating from PlatformIO

If you were previously using PlatformIO:

1. **Configuration**: Most settings from `platformio.ini` have been migrated to `sdkconfig.defaults`
2. **Libraries**: External dependencies that were managed by PlatformIO are now in `components/`
3. **Source Files**: `src/` → `main/`, `lib/` → `components/`
4. **Build Commands**: `pio run` → `idf.py build`, `pio run -t upload` → `idf.py flash`

## Further Reading

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/index.html)
- [ESP-IDF Build System](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html)
- [Arduino-ESP32 as ESP-IDF Component](https://docs.espressif.com/projects/arduino-esp32/en/latest/esp-idf_component.html)
