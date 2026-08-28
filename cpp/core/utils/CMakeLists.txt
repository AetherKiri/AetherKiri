cmake_minimum_required(VERSION 3.19)
project(core_utils_module LANGUAGES CXX)

include("${CMAKE_SOURCE_DIR}/cmake/AetherKrkrzIntegration.cmake")

set(AETHER_REAL_FFT_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/RealFFT.cpp")
aether_krkrz_track_core_source(
    "${AETHER_REAL_FFT_SOURCE}"
    "common/sound/RealFFT.cpp")

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
target_link_libraries(${PROJECT_NAME} PUBLIC tjs2 PRIVATE core_base_module core_environ_module)
