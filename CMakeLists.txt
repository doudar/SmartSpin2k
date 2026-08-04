cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(IDF_TARGET STREQUAL "esp32s3")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.s3.defaults")
endif()
# Keep component-manager locks target-specific so ESP32 and ESP32-S3 builds can
# coexist without rewriting one another's dependency target.
idf_build_set_property(DEPENDENCIES_LOCK dependencies.lock.${IDF_TARGET})
project(SmartSpin2k)
include_directories(
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
)
