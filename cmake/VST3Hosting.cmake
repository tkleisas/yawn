# ──────────────────────────────────────────────
# VST3 SDK — Hosting-only build
#
# Fetches the individual VST3 SDK repos (pluginterfaces, base, public.sdk)
# and builds a single static library containing only the hosting utilities.
# Does NOT use the SDK's own CMake module system — avoids pulling in
# samples, validators, VSTGUI, and other unneeded targets.
# ──────────────────────────────────────────────

include(FetchContent)

set(VST3_SDK_VERSION "v3.7.12_build_20")

# --- Fetch individual SDK repos (no submodules needed) ---

FetchContent_Declare(
    vst3_pluginterfaces
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3_pluginterfaces.git
    GIT_TAG        ${VST3_SDK_VERSION}
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    vst3_base
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3_base.git
    GIT_TAG        ${VST3_SDK_VERSION}
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    vst3_public_sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3_public_sdk.git
    GIT_TAG        ${VST3_SDK_VERSION}
    GIT_SHALLOW    TRUE
)

# Populate only (download sources without running their CMakeLists.txt)
FetchContent_GetProperties(vst3_pluginterfaces)
if(NOT vst3_pluginterfaces_POPULATED)
    FetchContent_Populate(vst3_pluginterfaces)
endif()

FetchContent_GetProperties(vst3_base)
if(NOT vst3_base_POPULATED)
    FetchContent_Populate(vst3_base)
endif()

FetchContent_GetProperties(vst3_public_sdk)
if(NOT vst3_public_sdk_POPULATED)
    FetchContent_Populate(vst3_public_sdk)
endif()

# The SDK headers expect an include root where:
#   #include "pluginterfaces/vst/ivstcomponent.h"
#   #include "base/source/fobject.h"
#   #include "public.sdk/source/vst/hosting/module.h"
# Create a virtual SDK root with junctions (Windows) or symlinks (Unix).

set(VST3_SDK_ROOT "${CMAKE_BINARY_DIR}/_vst3sdk_root")
file(MAKE_DIRECTORY "${VST3_SDK_ROOT}")

if(WIN32)
    # Junctions don't require admin on Windows (unlike symlinks)
    file(TO_NATIVE_PATH "${VST3_SDK_ROOT}/pluginterfaces" _PI_LINK)
    file(TO_NATIVE_PATH "${vst3_pluginterfaces_SOURCE_DIR}" _PI_TARGET)
    file(TO_NATIVE_PATH "${VST3_SDK_ROOT}/base" _BASE_LINK)
    file(TO_NATIVE_PATH "${vst3_base_SOURCE_DIR}" _BASE_TARGET)
    file(TO_NATIVE_PATH "${VST3_SDK_ROOT}/public.sdk" _PUB_LINK)
    file(TO_NATIVE_PATH "${vst3_public_sdk_SOURCE_DIR}" _PUB_TARGET)

    if(NOT EXISTS "${VST3_SDK_ROOT}/pluginterfaces")
        execute_process(COMMAND cmd /c mklink /J "${_PI_LINK}" "${_PI_TARGET}")
    endif()
    if(NOT EXISTS "${VST3_SDK_ROOT}/base")
        execute_process(COMMAND cmd /c mklink /J "${_BASE_LINK}" "${_BASE_TARGET}")
    endif()
    if(NOT EXISTS "${VST3_SDK_ROOT}/public.sdk")
        execute_process(COMMAND cmd /c mklink /J "${_PUB_LINK}" "${_PUB_TARGET}")
    endif()
else()
    # Unix: symlinks work without privileges
    if(NOT EXISTS "${VST3_SDK_ROOT}/pluginterfaces")
        execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${vst3_pluginterfaces_SOURCE_DIR}" "${VST3_SDK_ROOT}/pluginterfaces")
    endif()
    if(NOT EXISTS "${VST3_SDK_ROOT}/base")
        execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${vst3_base_SOURCE_DIR}" "${VST3_SDK_ROOT}/base")
    endif()
    if(NOT EXISTS "${VST3_SDK_ROOT}/public.sdk")
        execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${vst3_public_sdk_SOURCE_DIR}" "${VST3_SDK_ROOT}/public.sdk")
    endif()
endif()

# ── pluginterfaces (COM interfaces) ──
set(VST3_PLUGINTERFACES_SOURCES
    ${VST3_SDK_ROOT}/pluginterfaces/base/conststringtable.cpp
    ${VST3_SDK_ROOT}/pluginterfaces/base/coreiids.cpp
    ${VST3_SDK_ROOT}/pluginterfaces/base/funknown.cpp
    ${VST3_SDK_ROOT}/pluginterfaces/base/ustring.cpp
)

# ── base (fundamental utilities) ──
set(VST3_BASE_SOURCES
    ${VST3_SDK_ROOT}/base/source/baseiids.cpp
    ${VST3_SDK_ROOT}/base/source/fbuffer.cpp
    ${VST3_SDK_ROOT}/base/source/fdebug.cpp
    ${VST3_SDK_ROOT}/base/source/fdynlib.cpp
    ${VST3_SDK_ROOT}/base/source/fobject.cpp
    ${VST3_SDK_ROOT}/base/source/fstreamer.cpp
    ${VST3_SDK_ROOT}/base/source/fstring.cpp
    ${VST3_SDK_ROOT}/base/source/timer.cpp
    ${VST3_SDK_ROOT}/base/source/updatehandler.cpp
    ${VST3_SDK_ROOT}/base/thread/source/fcondition.cpp
    ${VST3_SDK_ROOT}/base/thread/source/flock.cpp
)

# ── sdk_common (shared utilities) ──
set(VST3_COMMON_SOURCES
    ${VST3_SDK_ROOT}/public.sdk/source/common/commoniids.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/common/commonstringconvert.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/common/openurl.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/common/readfile.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/vst/vstpresetfile.cpp
)
# Platform-specific common sources
if(WIN32)
    list(APPEND VST3_COMMON_SOURCES
        ${VST3_SDK_ROOT}/public.sdk/source/common/threadchecker_win32.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/systemclipboard_win32.cpp
    )
elseif(APPLE)
    list(APPEND VST3_COMMON_SOURCES
        ${VST3_SDK_ROOT}/public.sdk/source/common/threadchecker_mac.mm
        ${VST3_SDK_ROOT}/public.sdk/source/common/systemclipboard_mac.mm
    )
elseif(UNIX)
    list(APPEND VST3_COMMON_SOURCES
        ${VST3_SDK_ROOT}/public.sdk/source/common/threadchecker_linux.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/common/systemclipboard_linux.cpp
    )
endif()

# ── sdk_hosting (hosting utilities — EventList, ProcessData, Module, etc.) ──
set(VST3_HOSTING_SOURCES
    ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/connectionproxy.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/eventlist.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/parameterchanges.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/processdata.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/vst/utility/stringconvert.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/vst/vstinitiids.cpp
)
# Platform-specific module loader
if(WIN32)
    list(APPEND VST3_HOSTING_SOURCES
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp
    )
elseif(APPLE)
    list(APPEND VST3_HOSTING_SOURCES
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_mac.mm
    )
elseif(UNIX)
    list(APPEND VST3_HOSTING_SOURCES
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_linux.cpp
    )
endif()

# ── Create the unified hosting library ──
add_library(vst3_hosting STATIC
    ${VST3_PLUGINTERFACES_SOURCES}
    ${VST3_BASE_SOURCES}
    ${VST3_COMMON_SOURCES}
    ${VST3_HOSTING_SOURCES}
)

target_include_directories(vst3_hosting
    PUBLIC
        ${VST3_SDK_ROOT}
)

target_compile_features(vst3_hosting PUBLIC cxx_std_17)

target_compile_definitions(vst3_hosting
    PUBLIC
        "$<$<CONFIG:Debug>:DEVELOPMENT=1>"
        "$<$<CONFIG:Release>:RELEASE=1>"
        "$<$<CONFIG:RelWithDebInfo>:RELEASE=1>"
)

if(MSVC)
    target_compile_options(vst3_hosting PRIVATE
        /W3
        /wd4244   # conversion warnings (SDK has many)
        /wd4267   # size_t to int
        /wd4996   # deprecated functions
    )
endif()

# Platform libraries needed by the SDK
if(WIN32)
    target_link_libraries(vst3_hosting PUBLIC ole32 oleaut32)
elseif(APPLE)
    find_library(COREFOUNDATION_LIB CoreFoundation)
    find_library(FOUNDATION_LIB Foundation)
    target_link_libraries(vst3_hosting PUBLIC ${COREFOUNDATION_LIB} ${FOUNDATION_LIB})
elseif(UNIX)
    target_link_libraries(vst3_hosting PUBLIC dl)
endif()

message(STATUS "VST3 hosting library configured (SDK ${VST3_SDK_VERSION})")
