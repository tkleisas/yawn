cmake_minimum_required(VERSION 3.20)
project(YAWN
    VERSION 0.4.3
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
)

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

# VST3 editor host — separate process to isolate plugin UI from SDL
if(YAWN_VST3)
    add_executable(yawn_vst3_host src/vst3/VST3EditorHost.cpp)
    target_link_libraries(yawn_vst3_host PRIVATE vst3_hosting ole32 oleaut32)
    target_compile_definitions(yawn_vst3_host PRIVATE YAWN_HAS_VST3=1)
    set_target_properties(yawn_vst3_host PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    if(MSVC)
        target_compile_options(yawn_vst3_host PRIVATE /bigobj)
    endif()
endif()

# Testing
enable_testing()
add_subdirectory(tests)
