idf_component_register(
    SRCS
        "src/sonos.cpp"
        "src/discovery.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_event
        esp_http_client
        knob_storage
        knob_net
    PRIV_REQUIRES
        esp-tls
)

target_compile_options(${COMPONENT_LIB} PUBLIC
    -std=gnu++20
    -fno-exceptions
    -fno-rtti
)
