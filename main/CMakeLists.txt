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

# ── Image conversion: PNG → LVGL .bin → memory-mapped flash assets ───────────
# Logos: ARGB8888 (preserves alpha). Backgrounds: RGB565 + dithering (fixes banding).
# Output goes to build/mmap_data/ which gets packed into the 'assets' flash partition.
# At runtime, images are memory-mapped (zero-copy from flash via MMU).

set(MMAP_DIR "${CMAKE_BINARY_DIR}/mmap_data")
# LVGLImage.py ships with LVGL — use the managed component copy
# (downloaded automatically by idf.py build via idf_component.yml)
set(LVGL_IMAGE "${CMAKE_SOURCE_DIR}/managed_components/lvgl__lvgl/scripts/LVGLImage.py")
set(LOGO_SRC "${CMAKE_SOURCE_DIR}/assets/logos")

add_custom_command(
    OUTPUT ${MMAP_DIR}/.stamp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MMAP_DIR}
    COMMAND python3 ${LVGL_IMAGE} --cf ARGB8888 --ofmt BIN -o ${MMAP_DIR} ${LOGO_SRC}/
    COMMAND ${CMAKE_COMMAND} -E remove -f ${MMAP_DIR}/*_bg.bin
    COMMAND python3 ${LVGL_IMAGE} --cf RGB565 --rgb565dither --ofmt BIN -o ${MMAP_DIR} ${LOGO_SRC}/bg/
    COMMAND ${CMAKE_COMMAND} -E touch ${MMAP_DIR}/.stamp
    DEPENDS ${LOGO_SRC}
    COMMENT "Converting PNG assets to LVGL binary format"
)
add_custom_target(convert_images DEPENDS ${MMAP_DIR}/.stamp)
add_dependencies(${COMPONENT_LIB} convert_images)

# spiffs_create_partition_assets packs files into a binary blob with an index table.
# Despite the name, this has nothing to do with SPIFFS filesystem — it creates a
# flat binary that can be memory-mapped at runtime via esp_mmap_assets.
spiffs_create_partition_assets(
    assets
    ${MMAP_DIR}
    FLASH_IN_PROJECT
    MMAP_FILE_SUPPORT_FORMAT ".bin"
)
