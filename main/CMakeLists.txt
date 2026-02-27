set(SRCS
    "main.cpp"
    "fonts/geist_regular_14.c"
    "fonts/geist_regular_16.c"
    "fonts/geist_regular_20.c"
    "fonts/geist_medium_24.c"
    "fonts/geist_mono_medium_48.c"
    "net/wifi_manager.cpp"
    "sonos/sonos.cpp"
    "sonos/discovery.cpp"
    "input/encoder.cpp"
    "input/haptic.cpp"
    "storage/settings.cpp"
    "timer/timer.cpp"
    "voice/voice_tools.cpp"
    "voice/voice_session.cpp"
    "voice/voice_protocol.cpp"
    "voice/tools_radio.cpp"
    "ui/ui.cpp"
    "ui/ui_pages.cpp"
    "ui/ui_timer.cpp"
    "ui/ui_voice.cpp"
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
