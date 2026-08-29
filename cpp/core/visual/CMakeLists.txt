cmake_minimum_required(VERSION 3.19)
project(core_visual_module LANGUAGES CXX)

include("${CMAKE_SOURCE_DIR}/cmake/AetherKrkrzIntegration.cmake")

set(AETHER_FONT_VARIATIONS_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/FontVariations.cpp")
aether_krkrz_track_core_source(
    "${AETHER_FONT_VARIATIONS_SOURCE}"
    "common/visual/FontVariations.cpp")

# FontServiceIntf's declaration is consumed directly from krkrz_dev.  The
# implementation below is the Aether/FreeType ABI adapter; tracking the
# upstream header makes a submodule update trigger a reconfigure without
# copying the API into this repository.
aether_krkrz_require_core_source("common/visual/FontServiceIntf.h")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/FontServiceIntf.cpp"
    APPEND PROPERTY OBJECT_DEPENDS
    "${AETHER_KRKRZ_CORE_ROOT}/common/visual/FontServiceIntf.h")

set(AETHER_WEIGHT_FUNCTOR_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/WeightFunctor.cpp")
aether_krkrz_track_core_source(
    "${AETHER_WEIGHT_FUNCTOR_SOURCE}"
    "common/visual/gl/WeightFunctor.cpp")

set(AETHER_TLG_SIMD_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/TLGSIMD.cpp")
set(AETHER_TLG_SIMD_DISPATCH_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/TLGSIMDDispatch.cpp")
set(AETHER_RESAMPLE_SIMD_DISPATCH_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/ResampleImageDispatch.cpp")
set(AETHER_VISUAL_SIMD_LEAF_DISPATCH_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/VisualSIMDLeavesDispatch.cpp")
set(AETHER_VISUAL_SIMD_LEAF_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/ColorMapSSE2.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/ColorMapAVX2.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/ColorMapNEON.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/PixelFormatSSE2.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/PixelFormatNEON.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/UnivTransSSE2.cpp")
set(AETHER_VISUAL_SIMD_LEAF_INCLUDE_DIRS
    "${AETHER_KRKRZ_CORE_ROOT}/common/visual/IA32"
    "${AETHER_KRKRZ_CORE_ROOT}/common/visual/gl")
foreach(_aether_visual_leaf_source IN LISTS AETHER_VISUAL_SIMD_LEAF_SOURCES)
    if(_aether_visual_leaf_source MATCHES "ColorMapSSE2")
        aether_krkrz_track_core_source(
            "${_aether_visual_leaf_source}"
            "common/visual/gl/colormap_sse2.cpp")
    elseif(_aether_visual_leaf_source MATCHES "ColorMapAVX2")
        aether_krkrz_track_core_source(
            "${_aether_visual_leaf_source}"
            "common/visual/gl/colormap_avx2.cpp")
    elseif(_aether_visual_leaf_source MATCHES "ColorMapNEON")
        aether_krkrz_track_core_source(
            "${_aether_visual_leaf_source}"
            "common/visual/gl/colormap_neon.cpp")
    elseif(_aether_visual_leaf_source MATCHES "PixelFormatSSE2")
        aether_krkrz_track_core_source(
            "${_aether_visual_leaf_source}"
            "common/visual/gl/pixelformat_sse2.cpp")
    elseif(_aether_visual_leaf_source MATCHES "PixelFormatNEON")
        aether_krkrz_track_core_source(
            "${_aether_visual_leaf_source}"
            "common/visual/gl/pixelformat_neon.cpp")
    elseif(_aether_visual_leaf_source MATCHES "UnivTransSSE2")
        aether_krkrz_track_core_source(
            "${_aether_visual_leaf_source}"
            "common/visual/gl/univtrans_sse2.cpp")
    endif()
endforeach()
aether_krkrz_track_core_source(
    "${AETHER_VISUAL_SIMD_LEAF_DISPATCH_SOURCE}"
    "common/visual/gl/colormap_sse2.cpp")
set(_aether_visual_arch
    "${CMAKE_SYSTEM_PROCESSOR};${CMAKE_OSX_ARCHITECTURES};${ANDROID_ABI};${CMAKE_CXX_COMPILER_TARGET}")
string(TOLOWER "${_aether_visual_arch}" _aether_visual_arch)
# A universal macOS build is compiled once per architecture by Xcode; adding
# -mavx2 to that target would also be passed to the arm64 slice.  Restrict the
# isolated x86 leaves to single-architecture x86 builds and keep scalar/other
# SIMD paths for universal/mobile targets.
set(AETHER_RESAMPLE_SIMD_SOURCES)
set(AETHER_RESAMPLE_SIMD_INCLUDE_DIRS)
set(_aether_visual_is_single_x86 FALSE)
# Prefer an explicit target architecture over the host processor.  This is
# important when an arm64 Mac cross-compiles the x86_64 slice: CMake may leave
# CMAKE_SYSTEM_PROCESSOR as arm64 even though the compiler emits x86 code.
set(_aether_visual_explicit_arch
    "${CMAKE_OSX_ARCHITECTURES};${ANDROID_ABI};${CMAKE_CXX_COMPILER_TARGET}")
string(TOLOWER "${_aether_visual_explicit_arch}" _aether_visual_explicit_arch)
if("${CMAKE_OSX_ARCHITECTURES}" MATCHES ";")
    # A multi-architecture list is compiled by Xcode one slice at a time;
    # source-level -mavx2 flags would otherwise leak into the arm64 slice.
    set(_aether_visual_is_single_x86 FALSE)
elseif("${_aether_visual_explicit_arch}" MATCHES "x86_64|amd64|i[3-6]86" AND
       NOT "${_aether_visual_explicit_arch}" MATCHES "arm64|aarch64")
    set(_aether_visual_is_single_x86 TRUE)
elseif("${_aether_visual_arch}" MATCHES "x86_64|amd64|i[3-6]86" AND
       NOT "${_aether_visual_arch}" MATCHES "arm64|aarch64")
    set(_aether_visual_is_single_x86 TRUE)
endif()
if(_aether_visual_is_single_x86)
    aether_krkrz_track_core_source(
        "${AETHER_TLG_SIMD_SOURCE}"
        "common/visual/gl/tlg_sse2.cpp")
    set(AETHER_TLG_SIMD_INCLUDE_DIRS
        "${AETHER_KRKRZ_CORE_ROOT}/common/visual/IA32"
        "${AETHER_KRKRZ_CORE_ROOT}/common/visual/gl")
    set(AETHER_RESAMPLE_SIMD_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/ResampleImageSSE2.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/ResampleImageAVX2.cpp")
    set(AETHER_RESAMPLE_SIMD_INCLUDE_DIRS
        "${AETHER_KRKRZ_CORE_ROOT}/common/visual/IA32"
        "${AETHER_KRKRZ_CORE_ROOT}/common/visual/gl")
    aether_krkrz_track_core_source(
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/ResampleImageSSE2.cpp"
        "common/visual/gl/ResampleImageSSE2.cpp")
    aether_krkrz_track_core_source(
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/ResampleImageAVX2.cpp"
        "common/visual/gl/ResampleImageAVX2.cpp")
endif()
if(NOT _aether_visual_is_single_x86)
    set(AETHER_TLG_SIMD_INCLUDE_DIRS)
endif()

set(VISUAL_PATH ${CMAKE_CURRENT_LIST_DIR})
set(VISUAL_SOURCE_FILES
    ${VISUAL_PATH}/LoadWEBP.cpp
    ${VISUAL_PATH}/TransIntf.cpp
    ${VISUAL_PATH}/MenuItemIntf.cpp
    ${VISUAL_PATH}/SaveTLG5.cpp
    ${VISUAL_PATH}/FontSystem.cpp
    ${VISUAL_PATH}/FontStream.cpp
    ${VISUAL_PATH}/FontServiceIntf.cpp
    ${AETHER_FONT_VARIATIONS_SOURCE}
    ${VISUAL_PATH}/tvpps.inc
    ${VISUAL_PATH}/LoadPVRv3.cpp
    ${VISUAL_PATH}/RectItf.cpp
    ${VISUAL_PATH}/VideoOvlIntf.cpp
    ${VISUAL_PATH}/LayerBitmapIntf.cpp
    ${VISUAL_PATH}/FontImpl.cpp
    ${VISUAL_PATH}/RenderManager.cpp
    ${VISUAL_PATH}/LoadJPEG.cpp
    ${VISUAL_PATH}/GraphicsLoaderIntf.cpp
    ${VISUAL_PATH}/SaveTLG6.cpp
    ${VISUAL_PATH}/FreeType.cpp
    ${VISUAL_PATH}/LoadJXR.cpp
    ${VISUAL_PATH}/ImageFunction.cpp
    ${VISUAL_PATH}/tvpgl.cpp
    ${VISUAL_PATH}/LoadTLG.cpp
    ${AETHER_TLG_SIMD_SOURCE}
    ${AETHER_TLG_SIMD_DISPATCH_SOURCE}
    ${VISUAL_PATH}/LayerTreeOwnerImpl.cpp
    ${VISUAL_PATH}/GraphicsLoadThread.cpp
    ${VISUAL_PATH}/LayerManager.cpp
    ${VISUAL_PATH}/PrerenderedFont.cpp
    ${VISUAL_PATH}/LoadBPG.cpp
    ${VISUAL_PATH}/WindowIntf.cpp
    ${VISUAL_PATH}/FreeTypeFontRasterizer.cpp
    ${VISUAL_PATH}/LoadPNG.cpp
    ${VISUAL_PATH}/LayerIntf.cpp
    ${VISUAL_PATH}/ComplexRect.cpp
    ${VISUAL_PATH}/CharacterData.cpp
    ${VISUAL_PATH}/BitmapLayerTreeOwner.cpp
    ${VISUAL_PATH}/BitmapIntf.cpp
    ${VISUAL_PATH}/argb.cpp
    ${VISUAL_PATH}/LoadAMV.cpp

    ${VISUAL_PATH}/gl/blend_function.cpp
    ${VISUAL_PATH}/gl/ResampleImage.cpp
    ${AETHER_WEIGHT_FUNCTOR_SOURCE}
    ${AETHER_RESAMPLE_SIMD_SOURCES}
    ${AETHER_RESAMPLE_SIMD_DISPATCH_SOURCE}
    ${AETHER_VISUAL_SIMD_LEAF_DISPATCH_SOURCE}
    ${AETHER_VISUAL_SIMD_LEAF_SOURCES}

    ${VISUAL_PATH}/ogl/etcpak.cpp
    ${VISUAL_PATH}/ogl/PVRTDecompress.cpp

    ${VISUAL_PATH}/godot/GodotGpuBridge.cpp
    ${VISUAL_PATH}/godot/GodotRenderManager.cpp

    ${VISUAL_PATH}/impl/BasicDrawDevice.cpp
    ${VISUAL_PATH}/impl/BitmapBitsAlloc.cpp
    ${VISUAL_PATH}/impl/BitmapInfomation.cpp
    ${VISUAL_PATH}/impl/DInputMgn.cpp
    ${VISUAL_PATH}/impl/DrawDevice.cpp
#    ${VISUAL_PATH}/impl/GDIFontRasterizer.cpp
    ${VISUAL_PATH}/impl/GraphicsLoaderImpl.cpp
    ${VISUAL_PATH}/impl/LayerBitmapImpl.cpp
    ${VISUAL_PATH}/impl/LayerImpl.cpp
    ${VISUAL_PATH}/impl/MenuItemImpl.cpp
#    ${VISUAL_PATH}/impl/NativeFreeTypeFace.cpp
    ${VISUAL_PATH}/impl/PassThroughDrawDevice.cpp
    ${VISUAL_PATH}/impl/TVPScreen.cpp
#    ${VISUAL_PATH}/impl/TVPSysFont.cpp
    ${VISUAL_PATH}/impl/VideoOvlImpl.cpp
#    ${VISUAL_PATH}/impl/VSyncTimingThread.cpp
    ${VISUAL_PATH}/impl/WindowImpl.cpp
)

set(VISUAL_HEADERS_DIR
    ${VISUAL_PATH}/
    ${VISUAL_PATH}/gl
    ${VISUAL_PATH}/ogl
    ${VISUAL_PATH}/godot
    ${VISUAL_PATH}/impl
)

add_library(${PROJECT_NAME} STATIC ${VISUAL_SOURCE_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ${VISUAL_HEADERS_DIR})
if(AETHER_TLG_SIMD_INCLUDE_DIRS)
    target_include_directories(${PROJECT_NAME} PRIVATE ${AETHER_TLG_SIMD_INCLUDE_DIRS})
endif()
if(AETHER_RESAMPLE_SIMD_INCLUDE_DIRS)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${AETHER_RESAMPLE_SIMD_INCLUDE_DIRS})
endif()
target_include_directories(${PROJECT_NAME} PRIVATE
    ${AETHER_VISUAL_SIMD_LEAF_INCLUDE_DIRS})

if(_aether_visual_is_single_x86)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        AETHER_KRKRZ_VISUAL_AVX2_COMPILED=1
        AETHER_KRKRZ_TLG_SIMD_COMPILED=1)
    if(MSVC AND "${_aether_visual_arch}" MATCHES "i[3-6]86" AND
       NOT "${_aether_visual_arch}" MATCHES "x86_64|amd64")
        set(_aether_visual_sse2_flags /arch:SSE2)
    elseif(MSVC)
        set(_aether_visual_sse2_flags)
    else()
        set(_aether_visual_sse2_flags -msse2)
    endif()
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/ColorMapSSE2.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/PixelFormatSSE2.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/UnivTransSSE2.cpp"
        PROPERTIES COMPILE_OPTIONS "${_aether_visual_sse2_flags}")
    if(NOT MSVC)
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/ColorMapAVX2.cpp"
            PROPERTIES COMPILE_OPTIONS -mavx2)
    else()
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/ColorMapAVX2.cpp"
            PROPERTIES COMPILE_OPTIONS /arch:AVX2)
    endif()
endif()

# The dispatch translation unit is present in every target so the public
# initialization symbol remains stable.  Only single-architecture x86 builds
# compile the upstream SSE2/AVX2 resampler leaves; guard its declarations and
# calls with a definition so universal ARM/x86 builds do not leave unresolved
# symbols in the x86 slice.
if(AETHER_RESAMPLE_SIMD_SOURCES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        AETHER_KRKRZ_RESAMPLE_SIMD_COMPILED=1)
endif()

if(AETHER_RESAMPLE_SIMD_SOURCES)
    if(MSVC)
        # SSE2 is mandatory on x64 and /arch:SSE2 is an x86-only switch on
        # recent MSVC toolsets.  Leave it empty for x64 while retaining the
        # explicit switch for 32-bit builds.
        if("${_aether_visual_arch}" MATCHES "i[3-6]86" AND
           NOT "${_aether_visual_arch}" MATCHES "x86_64|amd64")
            set(_aether_resample_sse2_flags /arch:SSE2)
        else()
            set(_aether_resample_sse2_flags)
        endif()
        set(_aether_resample_avx2_flags /arch:AVX2)
    else()
        set(_aether_resample_sse2_flags -msse2)
        set(_aether_resample_avx2_flags -mavx2)
    endif()
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/ResampleImageSSE2.cpp"
        PROPERTIES COMPILE_OPTIONS "${_aether_resample_sse2_flags}")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/ResampleImageAVX2.cpp"
        PROPERTIES COMPILE_OPTIONS "${_aether_resample_avx2_flags}")
endif()
target_link_libraries(${PROJECT_NAME} PUBLIC tjs2 PRIVATE core_base_module core_environ_module core_plugin_module core_sound_module core_utils_module)

target_link_libraries(${PROJECT_NAME} PRIVATE libbpg)

if(ANDROID AND NOT BUILD_GPU_BRIDGE)
    target_sources(${PROJECT_NAME} PRIVATE
        ${VISUAL_PATH}/ogl/krkr_gl.cpp
        ${VISUAL_PATH}/ogl/krkr_egl_context.cpp
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE EGL GLESv2)
endif()

if(BUILD_GPU_BRIDGE)
    target_sources(${PROJECT_NAME} PRIVATE
        ${VISUAL_PATH}/ogl/astcrt.cpp
        ${VISUAL_PATH}/ogl/imagepacker.cpp
        ${VISUAL_PATH}/ogl/pvrtc.cpp
        ${VISUAL_PATH}/ogl/RenderManager_ogl.cpp
        ${VISUAL_PATH}/ogl/krkr_gl.cpp
        ${VISUAL_PATH}/ogl/krkr_egl_context.cpp
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE KRKR_ENABLE_GPU_BRIDGE=1)
    if(ANDROID)
        target_link_libraries(${PROJECT_NAME} PRIVATE EGL GLESv2)
    endif()
endif()

find_package(WebP CONFIG REQUIRED)
# Keep the engine debuggable without paying the full cost of an unoptimized
# third-party WebP decoder.  The vcpkg Debug archive is built without decoder
# optimizations and makes multi-megapixel PSD layers take hundreds of ms to
# materialize.  Use the ABI-compatible Release WebP archives for Debug builds;
# this only affects the third-party codec, not the engine's own debug symbols.
if(NOT WIN32)
    foreach(_webp_target WebP::webp WebP::webpdecoder WebP::webpdemux WebP::sharpyuv)
        if(TARGET ${_webp_target})
            set_property(TARGET ${_webp_target} PROPERTY
                         MAP_IMPORTED_CONFIG_DEBUG Release)
        endif()
    endforeach()
endif()
target_link_libraries(${PROJECT_NAME} INTERFACE WebP::webp WebP::webpdecoder WebP::webpdemux)


find_package(JXR REQUIRED)
target_compile_definitions(${PROJECT_NAME} PRIVATE -D__ANSI__)
target_include_directories(${PROJECT_NAME} PRIVATE ${JXR_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${JXR_LIBRARIES})

find_package(libjpeg-turbo CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC
    $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>
)

# An explicit OpenCV_DIR/OpenCV_ROOT is a supported host-provider override.
# This matters on developer machines that use Homebrew while CI keeps using
# the pinned vcpkg package.  Do not overwrite the caller's choice with a
# non-existent vcpkg path: doing so makes a perfectly usable host OpenCV look
# missing and can also trigger an unnecessary manifest rebuild.
if(NOT OpenCV_DIR AND NOT OpenCV_ROOT AND
   DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET AND
   EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/opencv4")
    set(OpenCV_ROOT
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/opencv4")
endif()
find_package(OpenCV REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC
    opencv_imgproc
    opencv_core
)

if(NOT TARGET lz4::lz4)
    include("${CMAKE_SOURCE_DIR}/cmake/AetherKiriDependencyAdapters.cmake")
    aetherkiri_prepare_lz4()
endif()
target_link_libraries(${PROJECT_NAME} PUBLIC
    lz4::lz4
)

find_package(Freetype REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Freetype::Freetype)

# FontService has a deliberately useful FreeType-only fallback, but complex
# scripts need the same HarfBuzz + FriBidi pair used by krkrz glyphware.  Keep
# discovery optional so mobile/web ports without these libraries still build
# and retain the scalar/fallback path; desktop CI and the vcpkg manifest carry
# both packages and therefore enable the full shaping contract.
option(AETHERKIRI_ENABLE_FONT_SHAPING
       "Enable HarfBuzz/FriBidi shaping in the shared FontService adapter" ON)
set(AETHERKIRI_FONT_SHAPING_ENABLED OFF)
if(AETHERKIRI_ENABLE_FONT_SHAPING AND NOT EMSCRIPTEN AND NOT WEB)
    set(_aether_harfbuzz_target "")
    find_package(harfbuzz CONFIG QUIET)
    if(TARGET harfbuzz::harfbuzz)
        set(_aether_harfbuzz_target harfbuzz::harfbuzz)
    elseif(TARGET harfbuzz)
        set(_aether_harfbuzz_target harfbuzz)
    endif()
    find_path(AETHER_HARFBUZZ_INCLUDE_DIR
        NAMES hb.h
        PATH_SUFFIXES harfbuzz)
    find_library(AETHER_HARFBUZZ_LIBRARY NAMES harfbuzz)
    if(NOT _aether_harfbuzz_target AND AETHER_HARFBUZZ_LIBRARY)
        set(_aether_harfbuzz_target ${AETHER_HARFBUZZ_LIBRARY})
    endif()

    find_path(AETHER_FRIBIDI_INCLUDE_DIR
        NAMES fribidi.h
        PATH_SUFFIXES fribidi)
    find_library(AETHER_FRIBIDI_LIBRARY NAMES fribidi)

    if(_aether_harfbuzz_target AND AETHER_FRIBIDI_INCLUDE_DIR AND
       AETHER_FRIBIDI_LIBRARY)
        set(AETHERKIRI_FONT_SHAPING_ENABLED ON)
        if(AETHER_HARFBUZZ_INCLUDE_DIR)
            target_include_directories(${PROJECT_NAME} PRIVATE
                ${AETHER_HARFBUZZ_INCLUDE_DIR})
        endif()
        target_include_directories(${PROJECT_NAME} PRIVATE
            ${AETHER_FRIBIDI_INCLUDE_DIR})
        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${_aether_harfbuzz_target}
            ${AETHER_FRIBIDI_LIBRARY})
        target_compile_definitions(${PROJECT_NAME} PRIVATE
            AETHERKIRI_FONT_SHAPING_ENABLED=1)
        message(STATUS "FontService complex shaping: HarfBuzz + FriBidi")
    else()
        message(STATUS
            "FontService complex shaping: unavailable; using FreeType fallback")
    endif()
elseif(AETHERKIRI_ENABLE_FONT_SHAPING)
    message(STATUS
        "FontService complex shaping: disabled for web/emscripten target")
endif()

find_package(PNG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE PNG::PNG)

find_package(ZLIB REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE ZLIB::ZLIB)

# Highway SIMD optimization module
add_subdirectory(simd)
target_link_libraries(${PROJECT_NAME} PRIVATE tvpgl_simd)

# The animation compositor spends most of its frame time in the templated
# stretch/affine raster loops in this module. Keep assertions and debug symbols,
# but allow those hot loops to inline and vectorize in Debug builds as well;
# -Og makes layered 1080p SD motions too slow to coexist with typewriter text.
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE -O2 -g)
endif()
