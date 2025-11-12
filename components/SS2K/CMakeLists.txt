# SS2K component CMakeLists.txt

# Gather all source files
file(GLOB_RECURSE COMPONENT_SRCS 
    "src/*.cpp" 
    "src/*.c"
    "src/**/*.cpp"
    "src/**/*.c"
)

# Register the component
idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS "include" "include/sensors" "${CMAKE_SOURCE_DIR}/include"
    REQUIRES arduino-esp32
)

# Add build flags
# Use C++17 for C++ sources only (avoid passing C++ flags to C files)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
