idf_component_register(
    SRCS
        "src/wifi_manager.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_wifi
        esp_event
        esp_timer
        knob_storage
    PRIV_REQUIRES
        esp_netif
)

target_compile_options(${COMPONENT_LIB} PUBLIC
    -std=gnu++20
    -fno-exceptions
    -fno-rtti
)
