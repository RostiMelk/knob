idf_component_register(
    SRCS
        "app_events.cpp"
        "ui.cpp"
        "ui_pages.cpp"
        "ui_timer.cpp"
        "ui_voice.cpp"
        "display_idf.cpp"
        "ui_bridge.cpp"
        "../../main/fonts/geist_regular_14.c"
        "../../main/fonts/geist_regular_16.c"
        "../../main/fonts/geist_regular_20.c"
        "../../main/fonts/geist_medium_24.c"
        "../../main/fonts/geist_mono_medium_48.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_event
        esp_lcd
        esp_timer
        nvs_flash
        radio_net
        radio_input
    PRIV_REQUIRES
        esp_psram
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++20
    -fno-exceptions
    -fno-rtti
)
