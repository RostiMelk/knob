idf_component_register(
    SRCS
        "src/voice_audio.cpp"
        "src/voice_mic.cpp"
        "src/voice_protocol.cpp"
        "src/voice_session.cpp"
        "src/voice_task.cpp"
        "src/voice_tools.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_event
        esp_websocket_client
        knob_storage
        knob_net
    PRIV_REQUIRES
        driver
        esp_psram
        esp-tls
)

target_compile_options(${COMPONENT_LIB} PUBLIC
    -std=gnu++20
    -fno-exceptions
    -fno-rtti
)
