set(SRCS
    "main.cpp"
    "fonts/geist_regular_18.c"
    "fonts/geist_regular_16.c"
    "fonts/geist_regular_22.c"
    "fonts/geist_medium_28.c"
    "fonts/geist_medium_52.c"
    "ui/images/nrj.c"
    "ui/images/nrk_jazz.c"
    "ui/images/nrk_mp3.c"
    "ui/images/nrk_nyheter.c"
    "ui/images/nrk_p1.c"
    "ui/images/nrk_p2.c"
    "ui/images/nrk_p3.c"
    "ui/images/p4.c"
    "ui/images/p5.c"
    "ui/images/p9.c"
    "ui/images/radio_norge.c"
    "ui/images/radio_rock.c"
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
    "voice/voice_task.cpp"
    "voice/voice_mic.cpp"
    "voice/voice_audio.cpp"
    "ui/ui.cpp"
    "ui/ui_pages.cpp"
    "ui/ui_timer.cpp"
    "ui/ui_voice.cpp"
    "ui/display_idf.cpp"
    "ui/tjpgd/tjpgd.c"
    "ui/art_decoder.cpp"
    "ui/squircle.cpp"
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
        esp_websocket_client
        nvs_flash
    PRIV_REQUIRES
        esp_netif
        esp_psram
        esp-tls
        fatfs
        sdmmc
)

target_compile_options(${COMPONENT_LIB} PUBLIC
    -std=gnu++20
    -fno-exceptions
    -fno-rtti
)

# Generated LVGL image C files use #ifdef LV_LVGL_H_INCLUDE_SIMPLE to pick
# the right include path. ESP-IDF's LVGL port expects "lvgl.h" (simple).
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
