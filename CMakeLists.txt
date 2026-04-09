cmake_minimum_required(VERSION 3.24)

project(terminal_buddy VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)

set(TB_ONNXRUNTIME_QNN_VERSION "1.24.4")
set(TB_ONNXRUNTIME_QNN_INCLUDE_DIR
    "${CMAKE_SOURCE_DIR}/build/nuget-cache/Microsoft.ML.OnnxRuntime.QNN.${TB_ONNXRUNTIME_QNN_VERSION}/build/native/include"
)
set(TERMINAL_BUDDY_NPU_STT_SOURCE_DIR
    "${CMAKE_SOURCE_DIR}/../npu-stt-c"
    CACHE PATH
    "Path to the npu-stt-c source checkout."
)

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

find_package(kissfft CONFIG QUIET)

if(NOT TARGET kissfft::kissfft)
    set(_TB_BUILD_SHARED_LIBS_SAVED "${BUILD_SHARED_LIBS}")
    set(BUILD_SHARED_LIBS OFF)
    set(KISSFFT_DATATYPE "float" CACHE STRING "Principal datatype of kissfft." FORCE)
    set(KISSFFT_STATIC ON CACHE BOOL "Build kissfft as a static library." FORCE)
    set(KISSFFT_TEST OFF CACHE BOOL "Build kissfft tests." FORCE)
    set(KISSFFT_TOOLS OFF CACHE BOOL "Build kissfft command-line tools." FORCE)
    set(KISSFFT_OPENMP OFF CACHE BOOL "Build kissfft with OpenMP support." FORCE)
    set(KISSFFT_PKGCONFIG OFF CACHE BOOL "Build kissfft pkg-config files." FORCE)
    FetchContent_Declare(
        kissfft
        GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
        GIT_TAG 131.2.0
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(kissfft)
    set(BUILD_SHARED_LIBS "${_TB_BUILD_SHARED_LIBS_SAVED}")
endif()

set(TERMINAL_BUDDY_HAVE_NPU_STT OFF)
if(EXISTS "${TERMINAL_BUDDY_NPU_STT_SOURCE_DIR}/CMakeLists.txt")
    set(NPU_STT_BUILD_EXAMPLES OFF CACHE BOOL "Disable npu-stt-c examples in terminal-buddy builds." FORCE)
    set(NPU_STT_BUILD_BENCHMARKS OFF CACHE BOOL "Disable npu-stt-c benchmarks in terminal-buddy builds." FORCE)
    set(NPU_STT_BUILD_TESTS OFF CACHE BOOL "Disable npu-stt-c tests in terminal-buddy builds." FORCE)
    set(NPU_STT_BUILD_NO_HEADERS_SMOKE OFF CACHE BOOL "Disable npu-stt-c no-header smoke in terminal-buddy builds." FORCE)
    set(NPU_STT_INSTALL OFF CACHE BOOL "Disable npu-stt-c install rules in terminal-buddy builds." FORCE)

    if(EXISTS "${TB_ONNXRUNTIME_QNN_INCLUDE_DIR}/onnxruntime_c_api.h")
        set(
            NPU_STT_ONNXRUNTIME_QNN_INCLUDE_DIR
            "${TB_ONNXRUNTIME_QNN_INCLUDE_DIR}"
            CACHE PATH
            "Path to the ONNX Runtime QNN C headers for npu-stt-c."
            FORCE
        )
    endif()

    add_subdirectory("${TERMINAL_BUDDY_NPU_STT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/npu-stt-c")
    set(TERMINAL_BUDDY_HAVE_NPU_STT ON)
else()
    message(WARNING "npu-stt-c not found at ${TERMINAL_BUDDY_NPU_STT_SOURCE_DIR}; the NPU backend will build in stub mode.")
endif()

add_executable(terminal-buddy WIN32
    src/main.c
    src/ui_clay.c
    src/ui_clay.h
    src/ui_clay_internal.h
    src/ui_bubble_widget.c
    src/ui_panel_widget.c
    src/ui_root.c
    src/transcription_backend.c
    src/transcription_backend.h
    src/transcription_backend_openai_win32.c
    src/transcription_backend_npu_win32.c
)

add_executable(text-engine-probe
    src/text_engine_probe.c
)

add_executable(transcription-probe
    src/transcription_probe.c
    src/transcription_backend.c
    src/transcription_backend.h
    src/transcription_backend_openai_win32.c
    src/transcription_backend_npu_win32.c
)

target_include_directories(terminal-buddy PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/clay
)

target_include_directories(text-engine-probe PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_include_directories(transcription-probe PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(TERMINAL_BUDDY_STATIC)
    set(_SDL3_TARGET SDL3::SDL3-static)
    set(_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf-static)
else()
    set(_SDL3_TARGET SDL3::SDL3)
    set(_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf)
endif()

target_link_libraries(terminal-buddy PRIVATE
    ${_SDL3_TARGET}
    ${_SDL3_TTF_TARGET}
)
target_link_libraries(text-engine-probe PRIVATE
    ${_SDL3_TARGET}
    ${_SDL3_TTF_TARGET}
)
target_link_libraries(transcription-probe PRIVATE
    ${_SDL3_TARGET}
)

if(TERMINAL_BUDDY_HAVE_NPU_STT)
    target_link_libraries(terminal-buddy PRIVATE npu_stt::npu_stt)
    target_link_libraries(transcription-probe PRIVATE npu_stt::npu_stt)
    target_compile_definitions(
        terminal-buddy
        PRIVATE
            TB_HAVE_NPU_STT=1
            TERMINAL_BUDDY_NPU_STT_SOURCE_DIR="${TERMINAL_BUDDY_NPU_STT_SOURCE_DIR}"
    )
    target_compile_definitions(
        transcription-probe
        PRIVATE
            TB_HAVE_NPU_STT=1
            TERMINAL_BUDDY_NPU_STT_SOURCE_DIR="${TERMINAL_BUDDY_NPU_STT_SOURCE_DIR}"
    )
endif()
target_compile_definitions(terminal-buddy PRIVATE TERMINAL_BUDDY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_definitions(text-engine-probe PRIVATE TERMINAL_BUDDY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_definitions(transcription-probe PRIVATE TERMINAL_BUDDY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(WIN32)
    target_link_libraries(terminal-buddy PRIVATE winhttp)
    target_link_libraries(transcription-probe PRIVATE winhttp)
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
    add_custom_command(
        TARGET transcription-probe
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:transcription-probe> $<TARGET_RUNTIME_DLLS:transcription-probe>
        COMMAND_EXPAND_LISTS
    )
endif()

if(MSVC)
    target_compile_options(terminal-buddy PRIVATE /W4 /permissive-)
else()
    target_compile_options(terminal-buddy PRIVATE -Wall -Wextra -Wpedantic)
endif()
