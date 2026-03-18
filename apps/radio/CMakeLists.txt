cmake_minimum_required(VERSION 3.16)

# Shared components live two levels up
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

# Load sdkconfig defaults in order (last wins):
#   1. sdkconfig.defaults        — base config (committed)
#   2. sdkconfig.defaults.local  — local secrets: WiFi, API keys (gitignored)
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
    list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(sonos_radio)
