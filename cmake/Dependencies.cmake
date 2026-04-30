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
    # v19.7.0 (Feb 2020) predates WASAPI-loopback support in the
    # upstream WASAPI host. This commit (post-loopback-PR master) ships
    # synthetic loopback capture devices alongside regular devices —
    # Pa_GetDeviceCount enumerates them with maxInputChannels > 0 and
    # PaWasapi_IsLoopback(idx) flags which entries are loopback render
    # endpoints. Lets YAWN capture system playback on Windows for the
    # Auto-Sampler with no third-party drivers (VB-CABLE, Stereo Mix).
    # Pinned to a specific hash for reproducible builds — bump
    # explicitly if upstream regressions need to be picked up.
    GIT_TAG        375345a752822e795bf6daeaa8e9ba981389c0ca
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
# tinygltf (glTF 2.0 loader — optional, 3D models)
# Header-only. We provide a single translation unit (tinygltf_impl.cpp)
# that defines TINYGLTF_IMPLEMENTATION and configures it to reuse our
# existing stb_image + nlohmann/json rather than the bundled copies.
# ──────────────────────────────────────────────
option(YAWN_HAS_MODEL3D "Enable 3D model loading via tinygltf" ON)
if(YAWN_HAS_MODEL3D)
    FetchContent_Declare(
        tinygltf
        GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
        GIT_TAG        v2.9.3
        GIT_SHALLOW    TRUE
    )
    # Avoid tinygltf building its loader_example / tests / installs.
    set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(TINYGLTF_BUILD_GL_EXAMPLES    OFF CACHE BOOL "" FORCE)
    set(TINYGLTF_BUILD_VALIDATOR_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(TINYGLTF_BUILD_BUILDER_EXAMPLE   OFF CACHE BOOL "" FORCE)
    set(TINYGLTF_HEADER_ONLY ON CACHE BOOL "" FORCE)
    set(TINYGLTF_INSTALL     OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tinygltf)
endif()

# ──────────────────────────────────────────────
# VST3 SDK (plugin hosting — optional)
# ──────────────────────────────────────────────
option(YAWN_VST3 "Enable VST3 plugin hosting support" ON)
if(YAWN_VST3)
    include(${CMAKE_CURRENT_LIST_DIR}/VST3Hosting.cmake)
endif()

# ──────────────────────────────────────────────
# Ableton Link (network beat/tempo sync — optional)
# ──────────────────────────────────────────────
option(YAWN_HAS_LINK "Enable Ableton Link support (network beat/tempo sync)" ON)

# ──────────────────────────────────────────────
# Neural Amp Modeler Core (.nam model inference) — optional
# ──────────────────────────────────────────────
# NAM bundles Eigen as a git submodule; FetchContent doesn't pull
# submodules, so we fetch Eigen separately and tell NAM where to
# find it via a custom include path on the static-library target
# we build below. Same trick for nlohmann::json — NAM expects a
# bundled "json.hpp" but we already have nlohmann::json fetched
# (line above), so a one-line shim in third_party/nam_shim/json.hpp
# pulls in the version YAWN uses everywhere else.
#
# NAM requires C++20. We ONLY apply the C++20 standard to the nam
# library target itself; the rest of YAWN stays on C++17. Public
# headers of nam are NOT exposed to YAWN code — the NeuralAmp
# effect uses a PIMPL so its .cpp (compiled as C++20) is the only
# translation unit that sees NAM's headers. The .h stays clean
# C++17 so it can be included from anywhere in YAWN.
option(YAWN_HAS_NAM "Enable Neural Amp Modeler (.nam) inference" ON)
if(YAWN_HAS_NAM)
    FetchContent_Declare(
        eigen
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        # Eigen 3.4.0 (2021) lacks placeholders::lastN which NAM
        # uses in lstm.h. NAM bundles Eigen at a newer commit on
        # master via submodule; we mirror that with a fresh master
        # pull. Pinned to a specific commit would be cleaner long-
        # term but Eigen has no recent tagged release to anchor on.
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(eigen)
    if(NOT eigen_POPULATED)
        # Eigen's own CMake is heavy and triggers a full Doxygen
        # build by default. We only need the headers, so use
        # FetchContent_Populate (no add_subdirectory) and reference
        # the populated source directory directly via include path.
        FetchContent_Populate(eigen)
    endif()

    FetchContent_Declare(
        neural_amp_modeler_core
        GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(neural_amp_modeler_core)
    if(NOT neural_amp_modeler_core_POPULATED)
        FetchContent_Populate(neural_amp_modeler_core)
    endif()
endif()

if(YAWN_HAS_LINK)
    # ASIO standalone (header-only networking, needed by Link)
    FetchContent_Declare(
        asio_headers
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG asio-1-28-0
    )
    FetchContent_GetProperties(asio_headers)
    if(NOT asio_headers_POPULATED)
        FetchContent_Populate(asio_headers)
    endif()

    # Ableton Link (header-only C++ library)
    FetchContent_Declare(
        ableton_link
        GIT_REPOSITORY https://github.com/Ableton/link.git
        GIT_TAG Link-3.1.5
    )
    FetchContent_GetProperties(ableton_link)
    if(NOT ableton_link_POPULATED)
        FetchContent_Populate(ableton_link)
    endif()
endif()
