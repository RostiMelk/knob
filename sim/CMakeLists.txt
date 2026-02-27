cmake_minimum_required(VERSION 3.16)
project(sonos_radio_sim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# ─── SDL2 ────────────────────────────────────────────────────────────────────

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# ─── LVGL v9 via FetchContent ────────────────────────────────────────────────

include(FetchContent)

FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.2.2
    GIT_SHALLOW    TRUE
)

# LVGL build config — set before FetchContent_MakeAvailable
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(lvgl)

# Tell LVGL where our lv_conf.h lives
target_include_directories(lvgl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE=1)

# LVGL's SDL driver needs SDL2 headers.
# pkg-config gives .../include/SDL2 — add it so <SDL.h> works,
# and add its parent so <SDL2/SDL.h> also works.
target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
foreach(dir ${SDL2_INCLUDE_DIRS})
    get_filename_component(parent "${dir}" DIRECTORY)
    target_include_directories(lvgl PUBLIC "${parent}")
endforeach()
target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})

# ─── Simulator executable ────────────────────────────────────────────────────

add_executable(sim
    sim_main.cpp
    display_sdl.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../main/ui/ui.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../main/ui/ui_voice.cpp"
)

# Include order matters: shims FIRST so they override ESP-IDF headers,
# then main/ for app_config.h and module headers, then SDL2.
target_include_directories(sim PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/shims"
    "${CMAKE_CURRENT_SOURCE_DIR}/../main"
    "${CMAKE_CURRENT_SOURCE_DIR}/../main/ui"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    ${SDL2_INCLUDE_DIRS}
)

target_link_directories(sim PRIVATE ${SDL2_LIBRARY_DIRS})
target_link_libraries(sim PRIVATE
    lvgl
    ${SDL2_LIBRARIES}
)

target_compile_definitions(sim PRIVATE
    SIMULATOR=1
    SDL_MAIN_HANDLED
)

target_compile_options(sim PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-unused-variable
)
