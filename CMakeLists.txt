cmake_minimum_required(VERSION 3.20)
project(YAWN
    VERSION 0.37.0
    DESCRIPTION "Yet Another Audio Workstation New"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Workaround for older dependencies with outdated cmake_minimum_required
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version")

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Fetch dependencies
include(cmake/Dependencies.cmake)

# Generate Version.h at build time (embeds git hash)
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")
add_custom_target(generate_version ALL
    COMMAND ${CMAKE_COMMAND}
        -DVERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        -DVERSION_MINOR=${PROJECT_VERSION_MINOR}
        -DVERSION_PATCH=${PROJECT_VERSION_PATCH}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_DIR=${GENERATED_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/GenerateVersion.cmake
    COMMENT "Generating Version.h"
)

# SQLite3 amalgamation (vendored)
add_library(sqlite3 STATIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3)
if(MSVC)
    target_compile_definitions(sqlite3 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ── FFmpeg (video playback) ──────────────────────────────────────────────
# Optional dep. Two detection paths:
#
#   1. pkg-config — the normal route on Linux/macOS and inside envs that
#      ship .pc files (e.g. vcpkg, MSYS2). Produces PkgConfig::FFMPEG
#      imported targets with all flags pre-wired.
#
#   2. Manual find — fallback for Windows where pkg-config typically
#      isn't present. Consumes an FFMPEG_ROOT hint (either -D or env)
#      pointing at a prebuilt FFmpeg tree shaped like:
#          <root>/include/libavcodec/avcodec.h
#          <root>/lib/avcodec.lib     (or libavcodec.a on unix)
#          <root>/bin/avcodec-*.dll   (shared builds on Windows)
#      This matches the BtbN prebuilt layout used in CI.
#
# YAWN_HAS_VIDEO / YAWN_HAS_AVDEVICE gate the video code paths so builds
# without ffmpeg still work (menu items hidden).
set(YAWN_HAS_VIDEO 0)
set(YAWN_HAS_AVDEVICE 0)
set(YAWN_FFMPEG_TARGET "")
set(YAWN_FFMPEG_AVDEVICE_TARGET "")

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET
        libavcodec libavformat libavutil libswscale)
    if(FFMPEG_FOUND)
        set(YAWN_HAS_VIDEO 1)
        set(YAWN_FFMPEG_TARGET PkgConfig::FFMPEG)
        message(STATUS "FFmpeg found via pkg-config — video track support enabled")
    endif()
    # libavdevice is optional — without it, network URLs (rtsp/http) still
    # work but OS capture URLs (v4l2, avfoundation, dshow) don't resolve.
    pkg_check_modules(FFMPEG_AVDEVICE QUIET IMPORTED_TARGET libavdevice)
    if(FFMPEG_AVDEVICE_FOUND)
        set(YAWN_HAS_AVDEVICE 1)
        set(YAWN_FFMPEG_AVDEVICE_TARGET PkgConfig::FFMPEG_AVDEVICE)
        message(STATUS "libavdevice found via pkg-config — live device capture enabled")
    endif()
endif()

# Manual detection fallback — picks up prebuilt FFmpeg via FFMPEG_ROOT.
if(NOT YAWN_HAS_VIDEO)
    if(NOT DEFINED FFMPEG_ROOT AND DEFINED ENV{FFMPEG_ROOT})
        set(FFMPEG_ROOT "$ENV{FFMPEG_ROOT}" CACHE PATH "FFmpeg install root")
    endif()
    if(FFMPEG_ROOT)
        find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
            HINTS ${FFMPEG_ROOT}
            PATH_SUFFIXES include
            NO_DEFAULT_PATH)
        find_library(FFMPEG_avcodec  NAMES avcodec  HINTS ${FFMPEG_ROOT} PATH_SUFFIXES lib NO_DEFAULT_PATH)
        find_library(FFMPEG_avformat NAMES avformat HINTS ${FFMPEG_ROOT} PATH_SUFFIXES lib NO_DEFAULT_PATH)
        find_library(FFMPEG_avutil   NAMES avutil   HINTS ${FFMPEG_ROOT} PATH_SUFFIXES lib NO_DEFAULT_PATH)
        find_library(FFMPEG_swscale  NAMES swscale  HINTS ${FFMPEG_ROOT} PATH_SUFFIXES lib NO_DEFAULT_PATH)
        if(FFMPEG_INCLUDE_DIR AND FFMPEG_avcodec AND FFMPEG_avformat
           AND FFMPEG_avutil AND FFMPEG_swscale)
            add_library(yawn_ffmpeg INTERFACE)
            target_include_directories(yawn_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIR})
            target_link_libraries(yawn_ffmpeg INTERFACE
                ${FFMPEG_avcodec} ${FFMPEG_avformat}
                ${FFMPEG_avutil}  ${FFMPEG_swscale})
            set(YAWN_HAS_VIDEO 1)
            set(YAWN_FFMPEG_TARGET yawn_ffmpeg)
            message(STATUS "FFmpeg found via FFMPEG_ROOT=${FFMPEG_ROOT} — video track support enabled")

            # libavdevice is a separate component.
            find_library(FFMPEG_avdevice NAMES avdevice
                HINTS ${FFMPEG_ROOT} PATH_SUFFIXES lib NO_DEFAULT_PATH)
            if(FFMPEG_avdevice)
                add_library(yawn_ffmpeg_avdevice INTERFACE)
                target_link_libraries(yawn_ffmpeg_avdevice INTERFACE ${FFMPEG_avdevice})
                set(YAWN_HAS_AVDEVICE 1)
                set(YAWN_FFMPEG_AVDEVICE_TARGET yawn_ffmpeg_avdevice)
                message(STATUS "libavdevice found via FFMPEG_ROOT — live device capture enabled")
            endif()
        else()
            message(WARNING "FFMPEG_ROOT=${FFMPEG_ROOT} set but headers/libs not found")
        endif()
    endif()
endif()

if(NOT YAWN_HAS_VIDEO)
    message(STATUS "FFmpeg NOT found — video track support disabled")
endif()
if(NOT YAWN_HAS_AVDEVICE)
    message(STATUS "libavdevice NOT found — live capture disabled "
                   "(network URLs still work)")
endif()

# Lua 5.4 (vendored)
file(GLOB LUA54_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua54/*.c)
list(FILTER LUA54_SOURCES EXCLUDE REGEX "(lua|luac)\\.c$")
add_library(lua54 STATIC ${LUA54_SOURCES})
target_include_directories(lua54 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua54)
if(MSVC)
    target_compile_definitions(lua54 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Collect sources
file(GLOB_RECURSE YAWN_SOURCES
    src/*.cpp
    src/*.c
)

# Core library — non-UI code (audio, midi, instruments, effects, util)
# Linked by both main executable and test executable
set(YAWN_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/AudioEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/ArrangementPlayback.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/ClipEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/Mixer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/MidiClipEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/OfflineRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util/FileIO.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util/ProjectSerializer.cpp
    # stb_image single-TU implementation. Lives in yawn_core so every
    # downstream binary (main exe, tests, tinygltf impl) resolves the
    # stbi_* symbols without needing its own dedicated TU.
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util/stb_image_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/presets/PresetManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/library/LibraryDatabase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/library/LibraryScanner.cpp
    # Slim-API stub — delegates to the main exe's VisualEngine when
    # linked into YAWN, safely no-ops in the test binary.
    ${CMAKE_CURRENT_SOURCE_DIR}/src/visual/VisualEngineAPI.cpp
    # Video import logic (POSIX fork+exec ffmpeg). Windows gets a stub
    # inside the .cpp. No SDL/GL deps, safe for yawn_core.
    ${CMAKE_CURRENT_SOURCE_DIR}/src/visual/VideoImporter.cpp
    # UI framework v2 — design-doc-driven rewrite; coexists with v1.
    # No GL deps in these TUs — pure layout/measure/event/cache logic.
    # Widgets that do paint link against the renderer in the main exe
    # (see Fw2Painters.cpp, below, added to YAWN only).
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/UIContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/Theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/Widget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/Fader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/FlexBox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/Label.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/Button.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/Painter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/LayerStack.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/DropDown.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/Tooltip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/ContextMenu.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/Dialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/Toggle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/Checkbox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/Knob.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/Pan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/StepSelector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/Crossfader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/ScrollBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/Meter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/NumberInput.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/TextInput.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/MenuBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/ProgressBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/RadioGroup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/Stack.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/FwGrid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/ScrollView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/TabView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/framework/v2/SplitView.cpp
)
# VST3 hosting sources (conditional)
if(YAWN_VST3)
    list(APPEND YAWN_CORE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vst3/VST3Host.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vst3/VST3Scanner.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vst3/VST3Instrument.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vst3/VST3Effect.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vst3/VST3EditorWindow.cpp
    )
endif()
# 3D model loader (tinygltf single-TU impl). No GL deps — safe for
# yawn_core; M3DRenderer (G.3) lives in the main exe with the GL code.
if(YAWN_HAS_MODEL3D)
    list(APPEND YAWN_CORE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/visual/gltf/tinygltf_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/visual/gltf/M3DModel.cpp
        # Lua scene-script glue. No GL deps; just Lua + stdlib.
        ${CMAKE_CURRENT_SOURCE_DIR}/src/visual/gltf/M3DSceneScript.cpp
    )
endif()
add_library(yawn_core STATIC ${YAWN_CORE_SOURCES})
target_include_directories(yawn_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${minimp3_SOURCE_DIR}
    # stb lives in yawn_core now so stb_image_impl.cpp (and any
    # consumer) can find stb_image.h without platform-specific paths.
    ${stb_SOURCE_DIR}
)
target_compile_definitions(yawn_core PUBLIC YAWN_CORE_BUILD)
target_link_libraries(yawn_core PUBLIC
    PortAudio
    sndfile
    rtmidi
    nlohmann_json::nlohmann_json
    sqlite3
    FLAC
    ogg
    vorbis
    vorbisenc
)

# VST3 hosting support (conditional)
if(YAWN_VST3)
    target_link_libraries(yawn_core PUBLIC vst3_hosting)
    target_compile_definitions(yawn_core PUBLIC YAWN_HAS_VST3=1)
endif()

# 3D model loader (tinygltf). Header-only: include path + feature flag.
# Lua is also needed here for the scene-script layer (M3DSceneScript).
if(YAWN_HAS_MODEL3D)
    target_include_directories(yawn_core PUBLIC
        ${tinygltf_SOURCE_DIR}
    )
    target_link_libraries(yawn_core PUBLIC lua54)
    target_compile_definitions(yawn_core PUBLIC YAWN_HAS_MODEL3D=1)
endif()

# Remove core sources from main exe (they're in yawn_core now)
list(REMOVE_ITEM YAWN_SOURCES ${YAWN_CORE_SOURCES})

# Remove standalone editor host (it's its own executable)
list(FILTER YAWN_SOURCES EXCLUDE REGEX "VST3EditorHost\\.cpp$")

add_executable(${PROJECT_NAME} ${YAWN_SOURCES})

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${stb_SOURCE_DIR}
    ${GENERATED_DIR}
)

add_dependencies(${PROJECT_NAME} generate_version)

# Link dependencies — yawn_core provides audio/midi/json libs
target_link_libraries(${PROJECT_NAME} PRIVATE
    yawn_core
    SDL3::SDL3
    glad
    lua54
)

if(YAWN_HAS_VIDEO)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${YAWN_FFMPEG_TARGET})
    target_compile_definitions(${PROJECT_NAME} PRIVATE YAWN_HAS_VIDEO=1)
endif()
if(YAWN_HAS_AVDEVICE)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${YAWN_FFMPEG_AVDEVICE_TARGET})
    target_compile_definitions(${PROJECT_NAME} PRIVATE YAWN_HAS_AVDEVICE=1)
endif()

# Platform-specific
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE opengl32)
elseif(UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GL)
endif()

# Copy assets to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
)

# Copy controller scripts to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts
)

# VST3 editor host — separate process to isolate plugin UI from SDL
if(YAWN_VST3 AND (WIN32 OR (UNIX AND NOT APPLE)))
    add_executable(yawn_vst3_host src/vst3/VST3EditorHost.cpp)
    target_link_libraries(yawn_vst3_host PRIVATE vst3_hosting)
    target_compile_definitions(yawn_vst3_host PRIVATE YAWN_HAS_VST3=1)
    set_target_properties(yawn_vst3_host PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    if(WIN32)
        target_link_libraries(yawn_vst3_host PRIVATE ole32 oleaut32)
    elseif(UNIX AND NOT APPLE)
        find_package(X11 REQUIRED)
        target_link_libraries(yawn_vst3_host PRIVATE X11::X11)
    endif()
    if(MSVC)
        target_compile_options(yawn_vst3_host PRIVATE /bigobj)
    endif()
endif()

# Testing
enable_testing()
add_subdirectory(tests)
