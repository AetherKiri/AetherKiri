cmake_minimum_required(VERSION 3.19)
project(core_sound_module LANGUAGES CXX)

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
    ${SOUND_PATH}/WaveIntf.cpp
    ${SOUND_PATH}/WaveLoopManager.cpp
    ${SOUND_PATH}/WaveSegmentQueue.cpp

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

# Keep the upstream DSP kernels optional and architecture-specific.  The
# wrappers pre-include AetherKiri's headers so the symbols use this module's
# ABI, while runtime dispatch in WaveIntf/PhaseVocoderDSP preserves scalar
# behavior on CPUs without the corresponding ISA.
string(TOLOWER
       "${CMAKE_SYSTEM_PROCESSOR};${CMAKE_OSX_ARCHITECTURES};${ANDROID_ABI};${CMAKE_ANDROID_ARCH_ABI}"
       _aether_sound_arch)
if(NOT WEB AND NOT EMSCRIPTEN AND
   _aether_sound_arch MATCHES "x86_64|amd64|x64|i[3-6]86|x86")
    target_sources(${PROJECT_NAME} PRIVATE
        ${SOUND_PATH}/xmmlib.cpp
        ${SOUND_PATH}/WaveFormatConverter_SSE.cpp
        ${SOUND_PATH}/MathAlgorithms_SSE_upstream.cpp
        ${SOUND_PATH}/RealFFT_SSE_upstream.cpp
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE AETHER_ENABLE_SOUND_X86_SIMD=1)
    if(NOT MSVC)
        set_source_files_properties(
            ${SOUND_PATH}/WaveFormatConverter_SSE.cpp
            ${SOUND_PATH}/MathAlgorithms_SSE_upstream.cpp
            ${SOUND_PATH}/RealFFT_SSE_upstream.cpp
            PROPERTIES COMPILE_OPTIONS "-msse2")
    endif()
elseif(NOT WEB AND NOT EMSCRIPTEN AND
       _aether_sound_arch MATCHES "aarch64|arm64|armv7|arm")
    target_sources(${PROJECT_NAME} PRIVATE
        ${SOUND_PATH}/MathAlgorithms_NEON_upstream.cpp
        ${SOUND_PATH}/RealFFT_NEON_upstream.cpp
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE AETHER_ENABLE_SOUND_ARM_SIMD=1)
endif()

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
