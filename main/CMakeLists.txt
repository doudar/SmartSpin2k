# Main component CMakeLists.txt

# Gather all source files
file(GLOB_RECURSE SRCS "*.cpp" "*.c")

# Register the component
idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "." "../include"
    REQUIRES
        arduino
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
)

# Add build flags
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++17
)
