idf_component_register(
    SRCS
        "src/settings.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        nvs_flash
)

target_compile_options(${COMPONENT_LIB} PUBLIC
    -std=gnu++20
    -fno-exceptions
    -fno-rtti
)
