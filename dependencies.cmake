# External Dependencies Management for SmartSpin2k
# This file manages external Arduino libraries that are not available in ESP-IDF component registry

include(FetchContent)

# Disable FetchContent warnings
set(FETCHCONTENT_QUIET OFF)

message(STATUS "Fetching external dependencies...")

# esp-nimble-cpp - Custom NimBLE C++ wrapper
FetchContent_Declare(
    esp-nimble-cpp
    GIT_REPOSITORY https://github.com/doudar/esp-nimble-cpp.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)

# TMCStepper - Trinamic stepper motor driver library
FetchContent_Declare(
    TMCStepper
    GIT_REPOSITORY https://github.com/teemuatlut/TMCStepper.git
    GIT_TAG v0.7.3
    GIT_SHALLOW TRUE
)

# ArduinoJson - JSON library
FetchContent_Declare(
    ArduinoJson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.3.1
    GIT_SHALLOW TRUE
)

# FastAccelStepper - High-performance stepper motor library
FetchContent_Declare(
    FastAccelStepper
    GIT_REPOSITORY https://github.com/doudar/FastAccelStepper.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)

# ArduinoWebsockets - WebSocket library
FetchContent_Declare(
    ArduinoWebsockets
    GIT_REPOSITORY https://github.com/doudar/ArduinoWebsockets.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)

# Make all dependencies available
FetchContent_MakeAvailable(
    esp-nimble-cpp
    TMCStepper
    ArduinoJson
    FastAccelStepper
    ArduinoWebsockets
)

message(STATUS "External dependencies fetched successfully")
