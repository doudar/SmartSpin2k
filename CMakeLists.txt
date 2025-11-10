# ESP-IDF Project CMakeLists.txt
cmake_minimum_required(VERSION 3.16.0)

# Generate firmware version from git tags
execute_process(
    COMMAND python3 ${CMAKE_SOURCE_DIR}/git_tag_macro.py
    OUTPUT_VARIABLE FIRMWARE_VERSION_FLAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Generate build timestamp
execute_process(
    COMMAND python3 ${CMAKE_SOURCE_DIR}/build_date_macro.py
    OUTPUT_VARIABLE BUILD_TIMESTAMP_FLAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Add build flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FIRMWARE_VERSION_FLAG} ${BUILD_TIMESTAMP_FLAG}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FIRMWARE_VERSION_FLAG} ${BUILD_TIMESTAMP_FLAG}")

# Include ESP-IDF project configuration
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define the project
project(SmartSpin2k)