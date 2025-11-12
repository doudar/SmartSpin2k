# Main component CMakeLists.txt

# Gather all source files
file(GLOB_RECURSE SRCS "*.cpp" "*.c")

# Register the component
idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "." "../include"
    REQUIRES
        arduino-esp32
        SS2K
        esp_http_server
        esp_https_server
        esp_http_client
        esp_https_ota
        nvs_flash
        spiffs
        littlefs
        esp_wifi
        mdns
        bt
        esp_timer
        driver
        spi_flash
        esp_netif
        esp_eth
        app_update
        esp-nimble-cpp
)

# Add build flags
# Use C++17 for C++ sources only
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
