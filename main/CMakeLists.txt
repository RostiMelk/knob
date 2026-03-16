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
        spiffs
)

target_compile_options(${COMPONENT_LIB} PUBLIC
    -std=gnu++20
    -fno-exceptions
    -fno-rtti
)

# ── Image conversion: PNG → LVGL .bin at build time ──────────────────────────────
# Logos: ARGB8888 (preserves alpha). Backgrounds: RGB565 + dithering (fixes banding).
# Output goes to build/spiffs_data/ which gets packed into the SPIFFS partition.

set(SPIFFS_DIR "${CMAKE_BINARY_DIR}/spiffs_data")
set(LVGL_IMAGE "${CMAKE_SOURCE_DIR}/scripts/LVGLImage.py")
set(LOGO_SRC "${CMAKE_SOURCE_DIR}/assets/logos")

add_custom_command(
    OUTPUT ${SPIFFS_DIR}/.stamp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SPIFFS_DIR}/bg
    COMMAND python3 ${LVGL_IMAGE} --cf ARGB8888 --ofmt BIN -o ${SPIFFS_DIR} ${LOGO_SRC}/
    COMMAND ${CMAKE_COMMAND} -E remove -f ${SPIFFS_DIR}/*_bg.bin
    COMMAND python3 ${LVGL_IMAGE} --cf RGB565 --rgb565dither --ofmt BIN -o ${SPIFFS_DIR}/bg ${LOGO_SRC}/bg/
    COMMAND ${CMAKE_COMMAND} -E touch ${SPIFFS_DIR}/.stamp
    DEPENDS ${LOGO_SRC}
    COMMENT "Converting PNG assets to LVGL binary format"
)
add_custom_target(convert_images DEPENDS ${SPIFFS_DIR}/.stamp)
add_dependencies(${COMPONENT_LIB} convert_images)

spiffs_create_partition_image(storage ${SPIFFS_DIR} FLASH_IN_PROJECT)
