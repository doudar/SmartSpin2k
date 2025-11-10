# ArduinoCompat component CMakeLists.txt

# Gather all source files
file(GLOB_RECURSE COMPONENT_SRCS 
    "src/*.cpp" 
    "src/*.c"
)

# Register the component
idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES arduino bt
)

# Add build flags
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++17
)
