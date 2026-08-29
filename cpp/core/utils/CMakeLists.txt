cmake_minimum_required(VERSION 3.19)
project(core_utils_module LANGUAGES CXX)

include("${CMAKE_SOURCE_DIR}/cmake/AetherKrkrzIntegration.cmake")

set(AETHER_REAL_FFT_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/RealFFT.cpp")
aether_krkrz_track_core_source(
    "${AETHER_REAL_FFT_SOURCE}"
    "common/sound/RealFFT.cpp")

# Compile the upstream FFT SIMD leaves through source bridges.  Keep the
# scalar bridge above in every build; a universal macOS target may contain
# both leaves, with each translation unit's preprocessor guard selecting the
# matching architecture slice.
set(AETHER_REAL_FFT_SIMD_SOURCES)
set(_aether_fft_arch
    "${CMAKE_SYSTEM_PROCESSOR};${CMAKE_OSX_ARCHITECTURES};${ANDROID_ABI};${CMAKE_CXX_COMPILER_TARGET}")
string(TOLOWER "${_aether_fft_arch}" _aether_fft_arch)
if("${_aether_fft_arch}" MATCHES "x86_64|amd64|i[3-6]86")
    set(AETHER_REAL_FFT_SIMD_SOURCES
        ${AETHER_REAL_FFT_SIMD_SOURCES}
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/RealFFT_SSE.cpp")
    aether_krkrz_track_core_source(
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/RealFFT_SSE.cpp"
        "common/sound/RealFFT_SSE.cpp")
endif()
if("${_aether_fft_arch}" MATCHES "aarch64|arm64|armv7|armeabi|arm")
    set(AETHER_REAL_FFT_SIMD_SOURCES
        ${AETHER_REAL_FFT_SIMD_SOURCES}
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/RealFFT_NEON.cpp")
    aether_krkrz_track_core_source(
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/RealFFT_NEON.cpp"
        "common/sound/RealFFT_NEON.cpp")
endif()

set(AETHER_RANDOM_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/Random.cpp")
aether_krkrz_track_core_source(
    "${AETHER_RANDOM_SOURCE}"
    "common/utils/Random.cpp")

set(AETHER_CLIPBOARD_INTF_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/ClipboardIntf.cpp")
aether_krkrz_track_core_source(
    "${AETHER_CLIPBOARD_INTF_SOURCE}"
    "common/utils/ClipboardIntf.cpp")

set(AETHER_DAP_SERVER_SOURCE)
if(AETHERKIRI_DAP_ENABLED)
    set(AETHER_DAP_SERVER_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/DAPServer.cpp")
    aether_krkrz_track_core_source(
        "${AETHER_DAP_SERVER_SOURCE}"
        "common/utils/DAPServer.cpp")
endif()

set(AETHER_MISC_UTILITY_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/MiscUtility.cpp")
aether_krkrz_track_core_source(
    "${AETHER_MISC_UTILITY_SOURCE}"
    "common/utils/MiscUtility.cpp")
set(AETHER_MD5_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/md5.c")
aether_krkrz_track_core_source(
    "${AETHER_MD5_SOURCE}"
    "common/utils/md5.c")

set(UTILS_PATH ${CMAKE_CURRENT_LIST_DIR})
set(UTILS_SOURCE_FILES
    ${AETHER_CLIPBOARD_INTF_SOURCE}
    ${UTILS_PATH}/DebugIntf.cpp
    ${UTILS_PATH}/MathAlgorithms_Default.cpp
    ${AETHER_MD5_SOURCE}
    ${AETHER_MISC_UTILITY_SOURCE}
    ${UTILS_PATH}/PadIntf.cpp
    ${AETHER_RANDOM_SOURCE}
    ${AETHER_REAL_FFT_SOURCE}
    ${AETHER_REAL_FFT_SIMD_SOURCES}
    ${AETHER_DAP_SERVER_SOURCE}
    ${UTILS_PATH}/ReplFileChannel.cpp
    ${UTILS_PATH}/ThreadIntf.cpp
    ${UTILS_PATH}/TickCount.cpp
    ${UTILS_PATH}/TimerIntf.cpp
    ${UTILS_PATH}/VelocityTracker.cpp

    ${UTILS_PATH}/encoding/gbk2unicode.c
    ${UTILS_PATH}/encoding/jis2unicode.c

    ${UTILS_PATH}/win32/ClipboardImpl.cpp
    ${UTILS_PATH}/win32/DebugImpl.cpp
    ${UTILS_PATH}/win32/PadImpl.cpp
    ${UTILS_PATH}/win32/ThreadImpl.cpp
    ${UTILS_PATH}/win32/TimerImpl.cpp
    ${UTILS_PATH}/win32/TVPTimer.cpp
)

set(UTILS_HEADERS_DIR
    ${UTILS_PATH}/
    ${UTILS_PATH}/iconv/
    ${UTILS_PATH}/win32/
)

add_library(${PROJECT_NAME} STATIC ${UTILS_SOURCE_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ${UTILS_HEADERS_DIR})
if(AETHERKIRI_DAP_ENABLED)
    # picojson is part of the public DAP header contract; upstream source
    # directories are implementation-only and must not leak a second set of
    # headers into downstream Aether targets.
    target_include_directories(${PROJECT_NAME} PUBLIC
        "${AETHERKIRI_PICOJSON_INCLUDE_DIR}")
    target_include_directories(${PROJECT_NAME} PRIVATE
        # Several ordinary utils sources include the historical unqualified
        # tjsCommHead.h spelling.  Resolve that name to Aether's ABI before
        # exposing the upstream leaf declaration directories below.
        "${CMAKE_SOURCE_DIR}/cpp/core/tjs2"
        "${CMAKE_SOURCE_DIR}/cpp/core/base"
        "${CMAKE_SOURCE_DIR}/cpp/core/base/impl"
        "${CMAKE_SOURCE_DIR}/cpp/core/environ"
        "${CMAKE_SOURCE_DIR}/cpp/core/environ/win32"
        "${CMAKE_SOURCE_DIR}/cpp/core/environ/sdl"
        "${AETHER_KRKRZ_CORE_ROOT}/common/tjs2"
        "${AETHER_KRKRZ_CORE_ROOT}/common/utils"
        "${AETHER_KRKRZ_CORE_ROOT}/common/base")
endif()
target_link_libraries(${PROJECT_NAME} PUBLIC tjs2 PRIVATE core_base_module core_environ_module)
