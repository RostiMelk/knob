set(SRCS
    "main.cpp"
)

set(INCLUDE_DIRS
    "."
)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    REQUIRES
        knob_hal
        knob_net
        knob_storage
        knob_ui
        esp_event
        esp_timer
        nvs_flash
)

target_compile_options(${COMPONENT_LIB} PUBLIC
    -std=gnu++20
    -fno-exceptions
    -fno-rtti
)
