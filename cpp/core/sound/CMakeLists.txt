cmake_minimum_required(VERSION 3.19)
project(core_sound_module LANGUAGES CXX)

include("${CMAKE_SOURCE_DIR}/cmake/AetherKrkrzIntegration.cmake")

# WaveSegmentQueue has no Aether-specific implementation delta.  Compile the
# implementation directly from the pinned core submodule while retaining the
# Aether-owned header and public sound ABI.  WaveLoopManager remains local
# because Aether added DesiredFormat and host-specific lifecycle behavior.
set(AETHER_WAVE_SEGMENT_QUEUE_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/WaveSegmentQueue.cpp")
aether_krkrz_track_core_source(
    "${AETHER_WAVE_SEGMENT_QUEUE_SOURCE}"
    "common/sound/WaveSegmentQueue.cpp")

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
    ${SOUND_PATH}/WaveLoopManager.cpp
    ${AETHER_WAVE_SEGMENT_QUEUE_SOURCE}
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
target_link_libraries(${PROJECT_NAME} PUBLIC tjs2 PRIVATE core_base_module core_movie_module core_utils_module core_environ_module)

find_package(Vorbis CONFIG REQUIRED)
find_package(Opus CONFIG REQUIRED)
find_package(OpusFile CONFIG REQUIRED)

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

find_package(OpenAL CONFIG REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vorbis::vorbis
    Vorbis::vorbisfile
    Vorbis::vorbisenc

    Opus::opus
    OpusFile::opusfile
    OpenAL::OpenAL
)
