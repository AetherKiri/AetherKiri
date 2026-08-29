cmake_minimum_required(VERSION 3.19)
project(core_sound_module LANGUAGES CXX)

include("${CMAKE_SOURCE_DIR}/cmake/AetherKrkrzIntegration.cmake")

# WaveSegmentQueue and WaveLoopManager share the same data/layout contract with
# the pinned core.  Compile both implementations directly from the submodule
# while retaining Aether-owned headers and the public sound ABI.  The loop
# manager's extra DesiredFormat virtual is supplied by its small adapter.
set(AETHER_WAVE_SEGMENT_QUEUE_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/WaveSegmentQueue.cpp")
aether_krkrz_track_core_source(
    "${AETHER_WAVE_SEGMENT_QUEUE_SOURCE}"
    "common/sound/WaveSegmentQueue.cpp")
set(AETHER_WAVE_LOOP_MANAGER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/WaveLoopManager.cpp")
aether_krkrz_track_core_source(
    "${AETHER_WAVE_LOOP_MANAGER_SOURCE}"
    "common/sound/WaveLoopManager.cpp")

set(AETHER_SOUND_SIMD_SOURCES)
set(_aether_sound_arch
    "${CMAKE_SYSTEM_PROCESSOR};${CMAKE_OSX_ARCHITECTURES};${ANDROID_ABI};${CMAKE_CXX_COMPILER_TARGET}")
string(TOLOWER "${_aether_sound_arch}" _aether_sound_arch)
if("${_aether_sound_arch}" MATCHES "x86_64|amd64|i[3-6]86")
    set(AETHER_SOUND_SIMD_SOURCES
        ${AETHER_SOUND_SIMD_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/MathAlgorithms_SSE.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/xmmlib.cpp)
    aether_krkrz_track_core_source(
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/MathAlgorithms_SSE.cpp"
        "common/sound/MathAlgorithms_SSE.cpp")
    aether_krkrz_track_core_source(
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/xmmlib.cpp"
        "common/sound/xmmlib.cpp")
endif()
if("${_aether_sound_arch}" MATCHES "aarch64|arm64|armv7|armeabi|arm")
    set(AETHER_SOUND_SIMD_SOURCES
        ${AETHER_SOUND_SIMD_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/MathAlgorithms_NEON.cpp)
    aether_krkrz_track_core_source(
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/MathAlgorithms_NEON.cpp"
        "common/sound/MathAlgorithms_NEON.cpp")
endif()

set(SOUND_PATH ${CMAKE_CURRENT_LIST_DIR})
set(SOUND_SOURCE_FILES
    ${SOUND_PATH}/CDDAIntf.cpp
    ${SOUND_PATH}/FFWaveDecoder.cpp
    # MathAlgorithms.cpp removed: same functions already defined in
    # core_utils_module/MathAlgorithms_Default.cpp (linked via core_utils_module dep)
    ${SOUND_PATH}/MIDIIntf.cpp
    ${SOUND_PATH}/PhaseVocoderDSP.cpp
    ${SOUND_PATH}/PhaseVocoderFilter.cpp
    ${SOUND_PATH}/SoundBufferBaseIntf.cpp
    ${SOUND_PATH}/VorbisWaveDecoder.cpp
    ${SOUND_PATH}/WaveFormatConverter.cpp
#    ${SOUND_PATH}/WaveFormatConverter_SSE.cpp
    ${SOUND_PATH}/WaveIntf.cpp
    ${AETHER_WAVE_LOOP_MANAGER_SOURCE}
    ${AETHER_WAVE_SEGMENT_QUEUE_SOURCE}
    ${AETHER_SOUND_SIMD_SOURCES}
#    ${SOUND_PATH}/xmmlib.cpp

    ${SOUND_PATH}/win32/CDDAImpl.cpp
    ${SOUND_PATH}/win32/MIDIImpl.cpp
    ${SOUND_PATH}/win32/SoundBufferBaseImpl.cpp
#    ${SOUND_PATH}/win32/tvpsnd.c
    ${SOUND_PATH}/win32/tvpsnd.cpp
#    ${SOUND_PATH}/win32/tvpsnd.idl
    ${SOUND_PATH}/win32/WaveImpl.cpp
    ${SOUND_PATH}/win32/WaveMixer.cpp
)

set(SOUND_HEADERS_DIR
    ${SOUND_PATH}/
    ${SOUND_PATH}/win32
)

add_library(${PROJECT_NAME} STATIC ${SOUND_SOURCE_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ${SOUND_HEADERS_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge")
target_link_libraries(${PROJECT_NAME} PUBLIC tjs2 PRIVATE core_base_module core_movie_module core_utils_module core_environ_module)

if(NOT TARGET Vorbis::vorbis OR NOT TARGET Opus::opus OR
   NOT TARGET OpusFile::opusfile OR NOT TARGET OpenAL::OpenAL)
    include("${CMAKE_SOURCE_DIR}/cmake/AetherKiriDependencyAdapters.cmake")
    aetherkiri_prepare_sound_codecs()
endif()

if(ANDROID)
    # vcpkg's oboe port does not ship CMake config files; find manually.
    find_library(OBOE_LIBRARY NAMES oboe PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib" NO_DEFAULT_PATH)
    find_path(OBOE_INCLUDE_DIR NAMES oboe/Oboe.h PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include" NO_DEFAULT_PATH)
    if(NOT OBOE_LIBRARY OR NOT OBOE_INCLUDE_DIR)
        message(FATAL_ERROR "Could not find oboe library. OBOE_LIBRARY=${OBOE_LIBRARY} OBOE_INCLUDE_DIR=${OBOE_INCLUDE_DIR}")
    endif()
    add_library(oboe::oboe STATIC IMPORTED)
    set_target_properties(oboe::oboe PROPERTIES
        IMPORTED_LOCATION "${OBOE_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OBOE_INCLUDE_DIR}"
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
        oboe::oboe
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vorbis::vorbis
    Vorbis::vorbisfile
    Vorbis::vorbisenc

    Opus::opus
    OpusFile::opusfile
    OpenAL::OpenAL
)
