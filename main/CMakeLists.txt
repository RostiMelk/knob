set(SRCS
    "main.cpp"
    "net/wifi_manager.cpp"
    "sonos/sonos.cpp"
    "sonos/discovery.cpp"
    "input/encoder.cpp"
    "storage/settings.cpp"
    "ui/ui.cpp"
    "ui/display_idf.cpp"
)

set(INCLUDE_DIRS
    "."
)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    REQUIRES
        driver
        esp_event
        esp_http_client
        esp_lcd
        esp_timer
        esp_wifi
        nvs_flash
    PRIV_REQUIRES
        esp_netif
        esp_psram
        fatfs
        sdmmc
)

target_compile_options(${COMPONENT_LIB} PUBLIC
    -std=gnu++20
    -fno-exceptions
    -fno-rtti
)
