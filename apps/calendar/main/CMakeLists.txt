set(SRCS
    "main.cpp"
    "cal_fetch.cpp"
    "ical_parser.cpp"
    "weather.cpp"
    "ui/ui.cpp"
    "fonts/geist_regular_16.c"
    "fonts/geist_regular_18.c"
    "fonts/geist_regular_20.c"
    "fonts/geist_medium_28.c"
    "fonts/geist_medium_52.c"
    "fonts/lucide_weather_20.c"
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
        esp_http_client
        json
        nvs_flash
    PRIV_REQUIRES
        esp_netif
        esp-tls
)

target_compile_options(${COMPONENT_LIB} PUBLIC
    -std=gnu++20
    -fno-exceptions
    -fno-rtti
)
