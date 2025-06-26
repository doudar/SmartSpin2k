# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources *.cpp)
FILE(GLOB_RECURSE ss2k_sources ${CMAKE_SOURCE_DIR}/lib/SS2K/src/sensors)
FILE(GLOB_RECURSE aws_sources ${CMAKE_SOURCE_DIR}/lib/ArduinoWebsockets/src)
FILE(GLOB_RECURSE tmcs_sources ${CMAKE_SOURCE_DIR}/lib/TMCStepper/src/source)
set(srcs ${app_sources} ${ss2k_sources} ${aws_sources} ${tmcs_sources})

set(include_dirs
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/SS2K/include
    ${CMAKE_SOURCE_DIR}/lib/SS2K/include/sensors
    ${CMAKE_SOURCE_DIR}/lib/ArduinoWebsockets
    ${CMAKE_SOURCE_DIR}/lib/TMCStepper/src
    ${CMAKE_SOURCE_DIR}/lib/TMCStepper/src/source
)

idf_component_register(SRCS "${srcs}" INCLUDE_DIRS "${include_dirs}")
