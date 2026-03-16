idf_component_register(
    SRCS
        "encoder.cpp"
        "haptic.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_event
        esp_timer
        radio_ui
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++20
    -fno-exceptions
    -fno-rtti
)
