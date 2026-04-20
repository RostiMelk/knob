idf_component_register(
    SRCS
        "src/ui_pages.cpp"
        "src/squircle.cpp"
        "src/art_decoder.cpp"
        "src/tjpgd/tjpgd.c"
        "src/lucide_22.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        knob_hal
    PRIV_REQUIRES
        esp_psram
)

target_compile_options(${COMPONENT_LIB} PUBLIC
    -std=gnu++20
    -fno-exceptions
    -fno-rtti
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
