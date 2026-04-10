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
set(TERMINAL_BUDDY_NPU_VOICE_SOURCE_DIR
    "${CMAKE_SOURCE_DIR}/../npu-voice-c"
    CACHE PATH
    "Path to the npu-voice-c source checkout."
)
set(TERMINAL_BUDDY_NPU_STT_ARTIFACT_ROOT
    "${CMAKE_SOURCE_DIR}/../npu-voice-assets/stt"
    CACHE PATH
    "Install-root style STT asset folder used by the local NPU backend defaults."
)
set(
    TERMINAL_BUDDY_WINTERM_SOURCE_DIR
    "${CMAKE_SOURCE_DIR}/../winterm"
    CACHE PATH
    "Path to the sibling winterm checkout used for the interim Strata terminal UI."
)
set(
    TERMINAL_BUDDY_STRATA_SOURCE_DIR
    "${CMAKE_SOURCE_DIR}/../strata"
    CACHE PATH
    "Path to the sibling Strata checkout used by the interim terminal UI."
)

option(TERMINAL_BUDDY_STATIC "Statically link SDL libraries into the executable." OFF)
option(TERMINAL_BUDDY_BUILD_APP "Build the SDL UI app." ON)
option(
    TERMINAL_BUDDY_BUILD_TERMINAL_UI
    "Build the interim Strata tabbed terminal companion executable."
    OFF
)
option(
    TERMINAL_BUDDY_ENABLE_OWNED_TERMINAL_WINDOW
    "Build the in-process Buddy-owned terminal window using the sibling winterm runtime."
    OFF
)
option(TERMINAL_BUDDY_BUILD_TEXT_ENGINE_PROBE "Build the text engine probe." ON)
option(TERMINAL_BUDDY_BUILD_TRANSCRIPTION_PROBE "Build the transcription probe." ON)
option(
    TERMINAL_BUDDY_ENABLE_MANAGED_TERMINAL_BACKEND
    "Build the internal Ghostty/ConPTY managed terminal backend."
    OFF
)
option(
    TERMINAL_BUDDY_BUILD_MANAGED_TERMINAL_PROBE
    "Build the managed terminal probe."
    OFF
)

if(TERMINAL_BUDDY_BUILD_MANAGED_TERMINAL_PROBE)
    set(TERMINAL_BUDDY_ENABLE_MANAGED_TERMINAL_BACKEND ON)
endif()

if(TERMINAL_BUDDY_BUILD_TERMINAL_UI)
    set(TERMINAL_BUDDY_ENABLE_OWNED_TERMINAL_WINDOW ON)
endif()

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
    if(TERMINAL_BUDDY_BUILD_APP OR TERMINAL_BUDDY_BUILD_TEXT_ENGINE_PROBE OR TERMINAL_BUDDY_BUILD_TERMINAL_UI)
        find_package(SDL3_ttf CONFIG QUIET)
    endif()
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

if((TERMINAL_BUDDY_BUILD_APP OR TERMINAL_BUDDY_BUILD_TEXT_ENGINE_PROBE OR TERMINAL_BUDDY_BUILD_TERMINAL_UI) AND NOT SDL3_ttf_FOUND)
    FetchContent_Declare(
        SDL3_ttf
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
        GIT_TAG release-3.2.2
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(SDL3_ttf)
endif()

if(TERMINAL_BUDDY_STATIC)
    set(_SDL3_TARGET SDL3::SDL3-static)
    set(_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf-static)
else()
    set(_SDL3_TARGET SDL3::SDL3)
    set(_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf)
endif()

if(
    TERMINAL_BUDDY_ENABLE_MANAGED_TERMINAL_BACKEND
    OR TERMINAL_BUDDY_BUILD_TERMINAL_UI
    OR TERMINAL_BUDDY_ENABLE_OWNED_TERMINAL_WINDOW
)
    set(_tb_reused_ghostty OFF)
    set(_tb_ghostty_dll "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/build/ghostty-vt.dll")
    set(_tb_ghostty_implib "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/build/_deps/ghostty-src/zig-out/lib/ghostty-vt.lib")
    set(_tb_ghostty_include "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/build/_deps/ghostty-src/include")

    if(
        NOT TARGET ghostty-vt
        AND EXISTS "${_tb_ghostty_dll}"
        AND EXISTS "${_tb_ghostty_implib}"
        AND EXISTS "${_tb_ghostty_include}/ghostty/vt.h"
    )
        add_library(ghostty-vt SHARED IMPORTED GLOBAL)
        set_target_properties(ghostty-vt PROPERTIES
            IMPORTED_LOCATION "${_tb_ghostty_dll}"
            IMPORTED_IMPLIB "${_tb_ghostty_implib}"
            INTERFACE_INCLUDE_DIRECTORIES "${_tb_ghostty_include}"
        )
        set(_tb_reused_ghostty ON)
        message(STATUS "Reusing ghostty-vt from ${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/build")
    endif()

    if(NOT _tb_reused_ghostty)
        FetchContent_Declare(
            ghostty
            GIT_REPOSITORY https://github.com/ghostty-org/ghostty.git
            GIT_TAG fdb6e3d2c8543e2e756b7e07f44372efbc0fba4b
        )
        FetchContent_MakeAvailable(ghostty)
    endif()
endif()

set(_TB_STRATA_ADDED OFF)
if(TERMINAL_BUDDY_BUILD_TERMINAL_UI OR TERMINAL_BUDDY_ENABLE_OWNED_TERMINAL_WINDOW)
    if(NOT EXISTS "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "The owned/companion terminal path requires winterm at "
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}")
    endif()
endif()

if(TERMINAL_BUDDY_BUILD_TERMINAL_UI)
    if(NOT EXISTS "${TERMINAL_BUDDY_STRATA_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "TERMINAL_BUDDY_BUILD_TERMINAL_UI requires Strata at "
            "${TERMINAL_BUDDY_STRATA_SOURCE_DIR}")
    endif()

    set(_tb_dxc_hint_arches)
    foreach(_tb_dxc_arch_hint IN ITEMS
            "${CMAKE_HOST_SYSTEM_PROCESSOR}"
            "${CMAKE_SYSTEM_PROCESSOR}"
            "$ENV{PROCESSOR_ARCHITEW6432}"
            "$ENV{PROCESSOR_ARCHITECTURE}")
        string(TOLOWER "${_tb_dxc_arch_hint}" _tb_dxc_arch_hint)
        if(_tb_dxc_arch_hint STREQUAL "amd64" OR
           _tb_dxc_arch_hint STREQUAL "x86_64" OR
           _tb_dxc_arch_hint STREQUAL "x64")
            set(_tb_dxc_arch_hint "x64")
        elseif(_tb_dxc_arch_hint STREQUAL "arm64" OR
               _tb_dxc_arch_hint STREQUAL "aarch64")
            set(_tb_dxc_arch_hint "arm64")
        elseif(_tb_dxc_arch_hint STREQUAL "x86" OR
               _tb_dxc_arch_hint MATCHES "^i[3-6]86$")
            set(_tb_dxc_arch_hint "x86")
        else()
            set(_tb_dxc_arch_hint "")
        endif()

        if(_tb_dxc_arch_hint)
            list(APPEND _tb_dxc_hint_arches "${_tb_dxc_arch_hint}")
        endif()
    endforeach()
    foreach(_tb_dxc_arch_hint IN ITEMS x64 arm64 x86)
        list(APPEND _tb_dxc_hint_arches "${_tb_dxc_arch_hint}")
    endforeach()
    list(REMOVE_DUPLICATES _tb_dxc_hint_arches)

    set(_tb_dxc_hints)
    foreach(_tb_dxc_arch_hint IN LISTS _tb_dxc_hint_arches)
        list(APPEND _tb_dxc_hints
            "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/${_tb_dxc_arch_hint}"
            "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/${_tb_dxc_arch_hint}")
    endforeach()

    find_program(
        TERMINAL_BUDDY_DXC_EXECUTABLE
        NAMES dxc
        HINTS ${_tb_dxc_hints}
    )

    if(NOT TERMINAL_BUDDY_DXC_EXECUTABLE)
        message(FATAL_ERROR
            "TERMINAL_BUDDY_BUILD_TERMINAL_UI requires dxc.exe for the borrowed Strata terminal shaders")
    endif()

    set(STRATA_BUILD_EXAMPLES OFF CACHE BOOL "Build Strata example applications" FORCE)
    set(STRATA_BUILD_TOOLS OFF CACHE BOOL "Build Strata development tools" FORCE)
    set(STRATA_BUILD_TESTS OFF CACHE BOOL "Build Strata regression tests" FORCE)
    set(STRATA_BUILD_BACKEND_NULL ON CACHE BOOL "Build the null renderer backend" FORCE)
    set(STRATA_BUILD_BACKEND_OPENGL OFF CACHE BOOL "Build the OpenGL renderer backend seam" FORCE)
    set(STRATA_BUILD_BACKEND_WEBGPU OFF CACHE BOOL "Build the WebGPU renderer backend seam" FORCE)
    set(STRATA_BUILD_BACKEND_SDLGPU ON CACHE BOOL "Build the SDL GPU renderer backend seam" FORCE)
    set(STRATA_DXC_EXECUTABLE "${TERMINAL_BUDDY_DXC_EXECUTABLE}" CACHE FILEPATH
        "Path to the dxc executable used to compile SDL GPU shader blobs"
        FORCE)
    add_subdirectory("${TERMINAL_BUDDY_STRATA_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/strata")
    set(_TB_STRATA_ADDED ON)
endif()

if(TERMINAL_BUDDY_BUILD_TERMINAL_UI OR TERMINAL_BUDDY_ENABLE_OWNED_TERMINAL_WINDOW)
    set(_tb_winterm_font_input "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/fonts/JetBrainsMono-Regular.ttf")
    set(_tb_winterm_font_header "${CMAKE_BINARY_DIR}/font_jetbrains_mono.h")
    add_custom_command(
        OUTPUT "${_tb_winterm_font_header}"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT=${_tb_winterm_font_input}
            -DOUTPUT=${_tb_winterm_font_header}
            -DARRAY_NAME=font_jetbrains_mono
            -P ${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/bin2header.cmake
        DEPENDS "${_tb_winterm_font_input}" "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/bin2header.cmake"
        COMMENT "Generating tb_winterm_font_jetbrains_mono.h"
    )

    set(_tb_terminal_vert_shader "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/shaders/terminal.vert.hlsl")
    set(_tb_terminal_frag_shader "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/shaders/terminal.frag.hlsl")
    set(_tb_terminal_vert_dxil "${CMAKE_BINARY_DIR}/tb_terminal.vert.dxil")
    set(_tb_terminal_frag_dxil "${CMAKE_BINARY_DIR}/tb_terminal.frag.dxil")
    set(_tb_terminal_vert_header "${CMAKE_BINARY_DIR}/terminal_vert_dxil.h")
    set(_tb_terminal_frag_header "${CMAKE_BINARY_DIR}/terminal_frag_dxil.h")

    add_custom_command(
        OUTPUT "${_tb_terminal_vert_dxil}"
        COMMAND "${TERMINAL_BUDDY_DXC_EXECUTABLE}" -E main -T vs_6_0 -Fo "${_tb_terminal_vert_dxil}" "${_tb_terminal_vert_shader}"
        DEPENDS "${_tb_terminal_vert_shader}"
        COMMENT "Compiling borrowed terminal.vert.hlsl"
    )

    add_custom_command(
        OUTPUT "${_tb_terminal_frag_dxil}"
        COMMAND "${TERMINAL_BUDDY_DXC_EXECUTABLE}" -E main -T ps_6_0 -Fo "${_tb_terminal_frag_dxil}" "${_tb_terminal_frag_shader}"
        DEPENDS "${_tb_terminal_frag_shader}"
        COMMENT "Compiling borrowed terminal.frag.hlsl"
    )

    add_custom_command(
        OUTPUT "${_tb_terminal_vert_header}"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT=${_tb_terminal_vert_dxil}
            -DOUTPUT=${_tb_terminal_vert_header}
            -DARRAY_NAME=terminal_vert_dxil
            -P ${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/bin2header.cmake
        DEPENDS "${_tb_terminal_vert_dxil}" "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/bin2header.cmake"
        COMMENT "Generating tb_terminal_vert_dxil.h"
    )

    add_custom_command(
        OUTPUT "${_tb_terminal_frag_header}"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT=${_tb_terminal_frag_dxil}
            -DOUTPUT=${_tb_terminal_frag_header}
            -DARRAY_NAME=terminal_frag_dxil
            -P ${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/bin2header.cmake
        DEPENDS "${_tb_terminal_frag_dxil}" "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/bin2header.cmake"
        COMMENT "Generating tb_terminal_frag_dxil.h"
    )
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
if(EXISTS "${TERMINAL_BUDDY_NPU_VOICE_SOURCE_DIR}/CMakeLists.txt")
    set(NPU_VOICE_BUILD_COMMON ON CACHE BOOL "Build the shared npu-voice common layer." FORCE)
    set(NPU_VOICE_BUILD_STT ON CACHE BOOL "Build the npu-voice STT library." FORCE)
    set(NPU_VOICE_BUILD_TTS OFF CACHE BOOL "Disable the npu-voice TTS library in terminal-buddy builds." FORCE)
    set(NPU_STT_BUILD_EXAMPLES OFF CACHE BOOL "Disable umbrella STT examples in terminal-buddy builds." FORCE)
    set(NPU_STT_BUILD_BENCHMARKS OFF CACHE BOOL "Disable umbrella STT benchmarks in terminal-buddy builds." FORCE)
    set(NPU_STT_BUILD_TESTS OFF CACHE BOOL "Disable umbrella STT tests in terminal-buddy builds." FORCE)
    set(NPU_STT_BUILD_NO_HEADERS_SMOKE OFF CACHE BOOL "Disable umbrella STT no-header smoke in terminal-buddy builds." FORCE)
    set(NPU_STT_INSTALL OFF CACHE BOOL "Disable umbrella STT install rules in terminal-buddy builds." FORCE)

    if(EXISTS "${TB_ONNXRUNTIME_QNN_INCLUDE_DIR}/onnxruntime_c_api.h")
        set(
            NPU_STT_ONNXRUNTIME_QNN_INCLUDE_DIR
            "${TB_ONNXRUNTIME_QNN_INCLUDE_DIR}"
            CACHE PATH
            "Path to the ONNX Runtime QNN C headers for the umbrella STT library."
            FORCE
        )
    endif()

    add_subdirectory("${TERMINAL_BUDDY_NPU_VOICE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/npu-voice-c")
    set(TERMINAL_BUDDY_HAVE_NPU_STT ON)
else()
    message(WARNING "npu-voice-c not found at ${TERMINAL_BUDDY_NPU_VOICE_SOURCE_DIR}; the NPU backend will build in stub mode.")
endif()

if(TERMINAL_BUDDY_BUILD_APP)
    add_executable(terminal-buddy WIN32
        src/main.c
        src/owned_terminal_window.c
        src/owned_terminal_window.h
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
        src/sidecar_client.c
        src/sidecar_client.h
    )

    if(TERMINAL_BUDDY_ENABLE_MANAGED_TERMINAL_BACKEND)
        target_sources(terminal-buddy PRIVATE
            src/managed_terminal.h
            src/managed_terminal_win32.c
        )
    endif()

    if(TERMINAL_BUDDY_ENABLE_OWNED_TERMINAL_WINDOW)
        target_sources(terminal-buddy PRIVATE
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/config.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/config.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/conpty.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/conpty.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/input.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/input.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/launch_spec.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/launch_spec.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/renderer_gpu.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/renderer_gpu.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/shell_input.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/shell_layout.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/shell_layout.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/shell_root.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/shell_root.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/speech_policy.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/speech_policy.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/tab_manager.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/tab_manager.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_controller.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_controller.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_host.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_host.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_input.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_layout.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_layout.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_session.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_session.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_surface.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_surface.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_view.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_view.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/utf8.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/winterm_embed.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/winterm_embed.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/winterm_input.h"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/winterm_runtime.c"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/winterm_runtime.h"
            "${_tb_winterm_font_header}"
            "${_tb_terminal_vert_header}"
            "${_tb_terminal_frag_header}"
        )
    endif()

    target_include_directories(terminal-buddy PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party/clay
    )

    if(TERMINAL_BUDDY_ENABLE_OWNED_TERMINAL_WINDOW)
        target_include_directories(terminal-buddy PRIVATE
            "${CMAKE_BINARY_DIR}"
            "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src"
        )
        target_compile_definitions(terminal-buddy PRIVATE
            TB_HAVE_OWNED_TERMINAL_WINDOW=1
            WINVER=0x0A00
            _WIN32_WINNT=0x0A00
        )
    endif()
endif()

if(TERMINAL_BUDDY_BUILD_TEXT_ENGINE_PROBE)
    add_executable(text-engine-probe
        src/text_engine_probe.c
    )

    target_include_directories(text-engine-probe PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
endif()

if(TERMINAL_BUDDY_BUILD_TRANSCRIPTION_PROBE)
    add_executable(transcription-probe
        src/transcription_probe.c
        src/transcription_backend.c
        src/transcription_backend.h
        src/transcription_backend_openai_win32.c
        src/transcription_backend_npu_win32.c
    )

    target_include_directories(transcription-probe PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
endif()

if(TERMINAL_BUDDY_BUILD_MANAGED_TERMINAL_PROBE)
    add_executable(managed-terminal-probe
        src/managed_terminal_probe.c
        src/managed_terminal.h
        src/managed_terminal_win32.c
    )

    target_include_directories(managed-terminal-probe PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
endif()

if(TERMINAL_BUDDY_BUILD_TERMINAL_UI)
    add_executable(terminal-buddy-terminal
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/config.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/config.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/conpty.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/conpty.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/input.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/input.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/launch_spec.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/launch_spec.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/renderer_gpu.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/renderer_gpu.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/shell_input.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/shell_layout.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/shell_layout.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/shell_root.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/shell_root.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/speech_policy.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/speech_policy.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/tab_manager.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/tab_manager.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_controller.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_controller.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_host.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_host.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_input.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_layout.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_layout.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_session.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_session.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_surface.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_surface.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_view.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/terminal_view.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/utf8.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/winterm_embed.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/winterm_embed.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/winterm_input.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/winterm_runtime.c"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/winterm_runtime.h"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src/winterm_strata_app.c"
        "${_tb_winterm_font_header}"
        "${_tb_terminal_vert_header}"
        "${_tb_terminal_frag_header}"
    )

    target_compile_features(terminal-buddy-terminal PRIVATE c_std_11)
    target_include_directories(terminal-buddy-terminal PRIVATE
        "${CMAKE_BINARY_DIR}"
        "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/src"
    )
    target_link_libraries(terminal-buddy-terminal PRIVATE
        ${_SDL3_TARGET}
        ${_SDL3_TTF_TARGET}
        ghostty-vt
        strata_host
        strata_render
    )
    target_compile_definitions(terminal-buddy-terminal PRIVATE
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
    )
    strata_configure_app_manifest(terminal-buddy-terminal
        MANIFEST "${TERMINAL_BUDDY_WINTERM_SOURCE_DIR}/strata.json"
        PREFIX WINTERM_STRATA
    )
endif()

if(TERMINAL_BUDDY_BUILD_APP)
    target_link_libraries(terminal-buddy PRIVATE
        ${_SDL3_TARGET}
        ${_SDL3_TTF_TARGET}
    )
    if(TERMINAL_BUDDY_ENABLE_MANAGED_TERMINAL_BACKEND OR TERMINAL_BUDDY_ENABLE_OWNED_TERMINAL_WINDOW)
        target_link_libraries(terminal-buddy PRIVATE ghostty-vt)
    endif()
endif()
if(TERMINAL_BUDDY_BUILD_TEXT_ENGINE_PROBE)
    target_link_libraries(text-engine-probe PRIVATE
        ${_SDL3_TARGET}
        ${_SDL3_TTF_TARGET}
    )
endif()
if(TERMINAL_BUDDY_BUILD_TRANSCRIPTION_PROBE)
    target_link_libraries(transcription-probe PRIVATE
        ${_SDL3_TARGET}
    )
endif()
if(TERMINAL_BUDDY_BUILD_MANAGED_TERMINAL_PROBE)
    target_link_libraries(managed-terminal-probe PRIVATE ghostty-vt)
endif()

if(TERMINAL_BUDDY_HAVE_NPU_STT)
    if(TERMINAL_BUDDY_BUILD_APP)
        target_link_libraries(terminal-buddy PRIVATE npu_stt::npu_stt)
        target_compile_definitions(
            terminal-buddy
            PRIVATE
                TB_HAVE_NPU_STT=1
                TERMINAL_BUDDY_NPU_STT_INSTALL_ROOT="${TERMINAL_BUDDY_NPU_STT_ARTIFACT_ROOT}"
        )
    endif()
    if(TERMINAL_BUDDY_BUILD_TRANSCRIPTION_PROBE)
        target_link_libraries(transcription-probe PRIVATE npu_stt::npu_stt)
        target_compile_definitions(
            transcription-probe
            PRIVATE
                TB_HAVE_NPU_STT=1
                TERMINAL_BUDDY_NPU_STT_INSTALL_ROOT="${TERMINAL_BUDDY_NPU_STT_ARTIFACT_ROOT}"
        )
    endif()
endif()
if(TERMINAL_BUDDY_BUILD_APP)
    target_compile_definitions(terminal-buddy PRIVATE TERMINAL_BUDDY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()
if(TERMINAL_BUDDY_BUILD_TEXT_ENGINE_PROBE)
    target_compile_definitions(text-engine-probe PRIVATE TERMINAL_BUDDY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()
if(TERMINAL_BUDDY_BUILD_TRANSCRIPTION_PROBE)
    target_compile_definitions(transcription-probe PRIVATE TERMINAL_BUDDY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()

if(WIN32)
    if(TERMINAL_BUDDY_BUILD_APP)
        target_link_libraries(terminal-buddy PRIVATE winhttp)
        add_custom_command(
            TARGET terminal-buddy
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:terminal-buddy> $<TARGET_RUNTIME_DLLS:terminal-buddy>
            COMMAND_EXPAND_LISTS
        )
    endif()
    if(TERMINAL_BUDDY_BUILD_TEXT_ENGINE_PROBE)
        target_link_libraries(text-engine-probe PRIVATE)
        add_custom_command(
            TARGET text-engine-probe
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:text-engine-probe> $<TARGET_RUNTIME_DLLS:text-engine-probe>
            COMMAND_EXPAND_LISTS
        )
    endif()
    if(TERMINAL_BUDDY_BUILD_TRANSCRIPTION_PROBE)
        target_link_libraries(transcription-probe PRIVATE winhttp)
        add_custom_command(
            TARGET transcription-probe
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:transcription-probe> $<TARGET_RUNTIME_DLLS:transcription-probe>
            COMMAND_EXPAND_LISTS
        )
    endif()
    if(TERMINAL_BUDDY_BUILD_MANAGED_TERMINAL_PROBE)
        add_custom_command(
            TARGET managed-terminal-probe
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:managed-terminal-probe> $<TARGET_RUNTIME_DLLS:managed-terminal-probe>
            COMMAND_EXPAND_LISTS
        )
    endif()
    if(TERMINAL_BUDDY_BUILD_TERMINAL_UI)
        add_custom_command(
            TARGET terminal-buddy-terminal
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:terminal-buddy-terminal> $<TARGET_RUNTIME_DLLS:terminal-buddy-terminal>
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()

if(MSVC)
    if(TERMINAL_BUDDY_BUILD_APP)
        target_compile_options(terminal-buddy PRIVATE /W4 /permissive-)
    endif()
    if(TERMINAL_BUDDY_BUILD_MANAGED_TERMINAL_PROBE)
        target_compile_options(managed-terminal-probe PRIVATE /W4 /permissive-)
    endif()
    if(TERMINAL_BUDDY_BUILD_TERMINAL_UI)
        target_compile_options(terminal-buddy-terminal PRIVATE /W4 /permissive-)
    endif()
else()
    if(TERMINAL_BUDDY_BUILD_APP)
        target_compile_options(terminal-buddy PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(TERMINAL_BUDDY_BUILD_MANAGED_TERMINAL_PROBE)
        target_compile_options(managed-terminal-probe PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(TERMINAL_BUDDY_BUILD_TERMINAL_UI)
        target_compile_options(terminal-buddy-terminal PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
