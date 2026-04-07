cmake_minimum_required(VERSION 3.24)

project(terminal_buddy VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)

option(TERMINAL_BUDDY_STATIC "Statically link SDL libraries into the executable." OFF)

if(TERMINAL_BUDDY_STATIC)
    set(BUILD_SHARED_LIBS OFF)
    set(SDL_STATIC ON)
    set(SDL_SHARED OFF)
    set(SDL_STATIC_PIC OFF)
endif()

# Disable SDL subsystems we don't use
set(SDL_HAPTIC OFF)
set(SDL_SENSOR OFF)
set(SDL_JOYSTICK OFF)
set(SDL_GAMEPAD OFF)
set(SDL_CAMERA OFF)
set(SDL_HIDAPI OFF)
set(SDL_POWER OFF)

option(TERMINAL_BUDDY_USE_SYSTEM_SDL "Prefer an installed SDL3 package before fetching." ON)

if(TERMINAL_BUDDY_USE_SYSTEM_SDL)
    find_package(SDL3 CONFIG QUIET)
    find_package(SDL3_ttf CONFIG QUIET)
    find_package(SDL3_image CONFIG QUIET)
endif()

if(NOT SDL3_FOUND)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.4
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(SDL3)
endif()

if(NOT SDL3_ttf_FOUND)
    FetchContent_Declare(
        SDL3_ttf
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
        GIT_TAG release-3.2.2
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(SDL3_ttf)
endif()

if(NOT SDL3_image_FOUND)
    FetchContent_Declare(
        SDL3_image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
        GIT_TAG release-3.2.0
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(SDL3_image)
endif()

add_executable(terminal-buddy WIN32
    src/main.c
    src/keyboard_state.c
    src/keyboard_state.h
    src/keyboard_layout.c
    src/keyboard_layout.h
    src/keyboard_emit_win32.c
    src/keyboard_emit_win32.h
    src/ui_clay.c
    src/ui_clay.h
    src/ui_clay_internal.h
    src/ui_bubble_widget.c
    src/ui_keyboard_widget.c
    src/ui_panel_widget.c
    src/ui_root.c
)

add_executable(text-engine-probe
    src/text_engine_probe.c
)

target_include_directories(terminal-buddy PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/clay
)

target_include_directories(text-engine-probe PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(TERMINAL_BUDDY_STATIC)
    set(_SDL3_TARGET SDL3::SDL3-static)
    set(_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf-static)
    set(_SDL3_IMAGE_TARGET SDL3_image::SDL3_image-static)
else()
    set(_SDL3_TARGET SDL3::SDL3)
    set(_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf)
    set(_SDL3_IMAGE_TARGET SDL3_image::SDL3_image)
endif()

target_link_libraries(terminal-buddy PRIVATE
    ${_SDL3_TARGET}
    ${_SDL3_TTF_TARGET}
    ${_SDL3_IMAGE_TARGET}
)
target_link_libraries(text-engine-probe PRIVATE
    ${_SDL3_TARGET}
    ${_SDL3_TTF_TARGET}
)
target_compile_definitions(terminal-buddy PRIVATE TERMINAL_BUDDY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_definitions(text-engine-probe PRIVATE TERMINAL_BUDDY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(WIN32)
    target_link_libraries(terminal-buddy PRIVATE winhttp)
    target_link_libraries(text-engine-probe PRIVATE)
    add_custom_command(
        TARGET terminal-buddy
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:terminal-buddy> $<TARGET_RUNTIME_DLLS:terminal-buddy>
        COMMAND_EXPAND_LISTS
    )
    add_custom_command(
        TARGET text-engine-probe
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:text-engine-probe> $<TARGET_RUNTIME_DLLS:text-engine-probe>
        COMMAND_EXPAND_LISTS
    )
endif()

if(MSVC)
    target_compile_options(terminal-buddy PRIVATE /W4 /permissive-)
else()
    target_compile_options(terminal-buddy PRIVATE -Wall -Wextra -Wpedantic)
endif()
