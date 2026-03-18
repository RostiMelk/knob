idf_component_register(
    SRCS
        "src/display_idf.cpp"
        "src/encoder.cpp"
        "src/haptic.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        esp_lcd
        esp_timer
        esp_event
    PRIV_REQUIRES
        esp_psram
)

target_compile_options(${COMPONENT_LIB} PUBLIC
    -std=gnu++20
    -fno-exceptions
    -fno-rtti
)
