cmake_minimum_required(VERSION 3.20)
project(YAWN
    VERSION 0.5.0
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
# Optional dep, detected via pkg-config. YAWN_HAS_VIDEO gates the video
# code paths so builds without ffmpeg still work (menu items hidden).
find_package(PkgConfig QUIET)
set(YAWN_HAS_VIDEO 0)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET
        libavcodec libavformat libavutil libswscale)
    if(FFMPEG_FOUND)
        set(YAWN_HAS_VIDEO 1)
        message(STATUS "FFmpeg found — video track support enabled")
    endif()
endif()
if(NOT YAWN_HAS_VIDEO)
    message(STATUS "FFmpeg NOT found — video track support disabled")
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/presets/PresetManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/library/LibraryDatabase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/library/LibraryScanner.cpp
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
add_library(yawn_core STATIC ${YAWN_CORE_SOURCES})
target_include_directories(yawn_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${minimp3_SOURCE_DIR}
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
    target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::FFMPEG)
    target_compile_definitions(${PROJECT_NAME} PRIVATE YAWN_HAS_VIDEO=1)
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
