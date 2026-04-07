include(FetchContent)

# ──────────────────────────────────────────────
# SDL3
# ──────────────────────────────────────────────
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# ──────────────────────────────────────────────
# glad (OpenGL 3.3 Core loader — glad2)
# Requires Python 3 + jinja2 on the build machine
# ──────────────────────────────────────────────
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v2.0.8
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake
)
FetchContent_MakeAvailable(glad)

glad_add_library(glad STATIC REPRODUCIBLE LOADER API gl:core=3.3)

# ──────────────────────────────────────────────
# PortAudio
# ──────────────────────────────────────────────
FetchContent_Declare(
    portaudio
    GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
    GIT_TAG        v19.7.0
    GIT_SHALLOW    TRUE
)
set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(portaudio)

# PortAudio target name varies; create an alias
if(TARGET portaudio_static)
    add_library(PortAudio ALIAS portaudio_static)
elseif(TARGET portaudio)
    add_library(PortAudio ALIAS portaudio)
endif()

# ──────────────────────────────────────────────
# Ogg, Vorbis, FLAC (for direct FLAC/OGG export, not through libsndfile)
# ──────────────────────────────────────────────
FetchContent_Declare(
    Ogg
    GIT_REPOSITORY https://github.com/xiph/ogg.git
    GIT_TAG        v1.3.5
    GIT_SHALLOW    TRUE
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(Ogg)
if(NOT TARGET Ogg::ogg)
    add_library(Ogg::ogg ALIAS ogg)
endif()

FetchContent_Declare(
    Vorbis
    GIT_REPOSITORY https://github.com/xiph/vorbis.git
    GIT_TAG        v1.3.7
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Vorbis)

set(BUILD_CXXLIBS OFF CACHE BOOL "" FORCE)
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(INSTALL_MANPAGES OFF CACHE BOOL "" FORCE)
set(WITH_OGG ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    FLAC
    GIT_REPOSITORY https://github.com/xiph/flac.git
    GIT_TAG        1.4.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(FLAC)

# ──────────────────────────────────────────────
# libsndfile (audio file I/O: WAV reading/writing, other format reading)
# ──────────────────────────────────────────────
FetchContent_Declare(
    libsndfile
    GIT_REPOSITORY https://github.com/libsndfile/libsndfile.git
    GIT_TAG        1.2.2
    GIT_SHALLOW    TRUE
)
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_EXTERNAL_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_REGTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libsndfile)

# ──────────────────────────────────────────────
# stb (stb_truetype for font rendering)
# ──────────────────────────────────────────────
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

# ──────────────────────────────────────────────
# RtMidi (cross-platform MIDI I/O)
# ──────────────────────────────────────────────
FetchContent_Declare(
    rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG        6.0.0
    GIT_SHALLOW    TRUE
)
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RTMIDI_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(RTMIDI_TARGETNAME_UNINSTALL "rtmidi_uninstall" CACHE STRING "" FORCE)
FetchContent_MakeAvailable(rtmidi)

# ──────────────────────────────────────────────
# Google Test (for unit testing)
# ──────────────────────────────────────────────
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ──────────────────────────────────────────────
# minimp3 (header-only MP3 decoder)
# ──────────────────────────────────────────────
FetchContent_Declare(
    minimp3
    GIT_REPOSITORY https://github.com/lieff/minimp3.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(minimp3)

# ──────────────────────────────────────────────
# nlohmann/json (header-only JSON library)
# ──────────────────────────────────────────────
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# ──────────────────────────────────────────────
# VST3 SDK (plugin hosting — optional)
# ──────────────────────────────────────────────
option(YAWN_VST3 "Enable VST3 plugin hosting support" ON)
if(YAWN_VST3)
    include(${CMAKE_CURRENT_LIST_DIR}/VST3Hosting.cmake)
endif()
