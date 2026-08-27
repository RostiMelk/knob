idf_component_register(
    SRCS
        "src/wifi_setup.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_wifi
        esp_event
        esp_http_server
        knob_storage
    PRIV_REQUIRES
        esp_netif
        nvs_flash
        lwip
)

target_compile_options(${COMPONENT_LIB} PUBLIC
    -std=gnu++20
    -fno-exceptions
    -fno-rtti
)
