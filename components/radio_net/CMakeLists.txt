idf_component_register(
    SRCS
        "wifi_manager.cpp"
        "sonos.cpp"
        "discovery.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_event
        esp_http_client
        esp_wifi
        nvs_flash
    PRIV_REQUIRES
        esp_netif
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++20
    -fno-exceptions
    -fno-rtti
)
