cmake_minimum_required(VERSION 3.19)
project(krkr2plugin)

include("${CMAKE_SOURCE_DIR}/cmake/AetherKrkrzIntegration.cmake")

# layerExSave keeps Aether's Layer/TJS wrapper and storage boundary, but its
# portable PNG and TLG5 codecs are compiled directly from the pinned upstream
# sources.  Do not copy these files into the parent repository.
aether_krkrz_require_source("layerExSave/LodePNG/lodepng.cpp")
aether_krkrz_require_source("layerExSave/LodePNG/lodepng.h")
aether_krkrz_require_source("layerExSave/tlg5/slide.cpp")
aether_krkrz_require_source("layerExSave/tlg5/slide.h")

# These low-risk leaf plugins have one implementation path: a small Aether
# ABI/module-name bridge that includes the business translation unit from the
# pinned krkrz_dev submodule. Historical local copies were deliberately
# removed so a stale build option cannot select a second implementation.
aether_krkrz_require_source("layerExAreaAverage/main.cpp")
aether_krkrz_require_source("layerExRaster/main.cpp")
aether_krkrz_require_source("layerExRaster/layerExBase.hpp")
aether_krkrz_require_source("layerExImage/layerExBase.hpp")
aether_krkrz_require_source("layerExImage/LayerExImage.cpp")
aether_krkrz_require_source("layerExImage/Main.cpp")
aether_krkrz_require_source("layerExLongExposure/Main.cpp")
aether_krkrz_require_source("getSample/main.cpp")
aether_krkrz_require_source("layerExBTOA/Main.cpp")
aether_krkrz_require_source("shrinkCopy/main.cpp")

# The extNagano sources are self-contained transition algorithms. The two
# rule-provider variants (3duniversal/imagewipe) require texture methods that
# are not present in Aether's scan-line ABI, so they remain on the Aether
# fallback provider; the other algorithms are detached and wrapped by
# upstream_bridge/extNaganoProviders.cpp.
set(AETHER_EXTNAGANO_UPSTREAM_SOURCES
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/blurfade.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/book.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/flutter.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/honeyturn.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/morphing.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/multiripple.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/rgbfade.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/scanline.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/spin.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/zoomfade.cpp")
foreach(_aether_extnagano_source IN LISTS AETHER_EXTNAGANO_UPSTREAM_SOURCES)
    if(NOT EXISTS "${_aether_extnagano_source}")
        message(FATAL_ERROR
            "Selected extNagano upstream source is missing: "
            "${_aether_extnagano_source}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_aether_extnagano_source}")
endforeach()
set(AETHER_LAYEREX_AREA_AVERAGE_SOURCE upstream_bridge/layerExAreaAverage.cpp)
set(AETHER_LAYEREX_RASTER_SOURCE upstream_bridge/layerExRaster.cpp)
set(AETHER_LAYEREX_LONG_EXPOSURE_SOURCE
    upstream_bridge/layerExLongExposure.cpp)
set(AETHER_GET_SAMPLE_SOURCE upstream_bridge/getSample.cpp)
set(AETHER_LAYEREX_BTOA_SOURCE upstream_bridge/layerExBTOA.cpp)
set(AETHER_LAYEREX_IMAGE_SOURCE upstream_bridge/layerExImage.cpp)
set(AETHER_SHRINK_COPY_SOURCE upstream_bridge/shrinkCopy.cpp)

if(AETHER_USE_KRKRZ_OPTIONAL_PLUGINS)
    if(NOT EXISTS "${AETHER_KRKRZ_DEV_ROOT}/.git" AND
       NOT EXISTS "${AETHER_KRKRZ_DEV_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR
            "AETHER_USE_KRKRZ_OPTIONAL_PLUGINS requires an initialized "
            "krkrz_dev checkout at ${AETHER_KRKRZ_DEV_ROOT}")
    endif()
    # These plugins pull in platform/rendering SDKs and old registration
    # contracts.  The layerExVector contract is consumed by the lightweight
    # Aether bridge in krkrzLayerExVectorCompat.cpp; the remaining projects are
    # still source-validated only until dedicated adapters and tests exist.
    foreach(_aether_krkrz_optional_source
            "layerExVector/main.cpp"
            "krkr_richtext/src/main.cpp"
            "krkreffekseer/src/Effect.cpp"
            "krkrthreepp/src/main.cpp")
        aether_krkrz_require_source("${_aether_krkrz_optional_source}")
    endforeach()
    message(STATUS
        "krkrz_dev optional plugin sources validated; layerExVector uses the "
        "Aether LayerExDraw compatibility bridge")
endif()

add_library(${PROJECT_NAME} STATIC)

# Keep the include scanner honest when a pinned upstream translation unit
# changes in place. The bridge remains the only product source; the object
# dependency merely triggers a rebuild.
aether_krkrz_track_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/${AETHER_LAYEREX_AREA_AVERAGE_SOURCE}"
    "layerExAreaAverage/main.cpp")
aether_krkrz_track_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/${AETHER_LAYEREX_RASTER_SOURCE}"
    "layerExRaster/main.cpp")
aether_krkrz_track_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/${AETHER_LAYEREX_LONG_EXPOSURE_SOURCE}"
    "layerExLongExposure/Main.cpp")
aether_krkrz_track_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/${AETHER_GET_SAMPLE_SOURCE}"
    "getSample/main.cpp")
aether_krkrz_track_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/${AETHER_LAYEREX_BTOA_SOURCE}"
    "layerExBTOA/Main.cpp")
aether_krkrz_track_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/${AETHER_LAYEREX_IMAGE_SOURCE}"
    "layerExImage/Main.cpp")
aether_krkrz_track_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/${AETHER_LAYEREX_IMAGE_SOURCE}"
    "layerExImage/LayerExImage.cpp")
aether_krkrz_track_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/${AETHER_SHRINK_COPY_SOURCE}"
    "shrinkCopy/main.cpp")

target_sources(${PROJECT_NAME} PRIVATE
    scriptsEx.cpp
    kbadDataPack.cpp
    binaryStream.cpp
    base64.cpp
    encode.cpp
    expat.cpp
    imagesaver.cpp
    lineParser.cpp
    memfile.cpp
    json/jsonPlugin.cpp
    tomlPlugin.cpp
    minizip.cpp
    qrcode.cpp
    qrcode/QR_Encode.cpp
    sqlitePlugin.cpp
    sqliteXp3Vfs.cpp
    sqlite/sqlite3.c
    win32dialog.cpp
    dirlist.cpp
    csvParser.cpp
    layerExMovie.cpp
    varfile.cpp
    saveStruct.cpp
    getabout.cpp
    addFont.cpp
    wutcwf.cpp
    windowEx.cpp
    menuExt.cpp
    ${AETHER_GET_SAMPLE_SOURCE}
    kirikiroid2.cpp
    layerExPerspective.cpp
    fftgraph.cpp
    LayerExBase.cpp
    layerExSaveCompat.cpp
    upstream_bridge/layerExSaveCodecs.cpp
    "${AETHER_KRKRZ_PLUGIN_ROOT}/layerExSave/LodePNG/lodepng.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/layerExSave/tlg5/slide.cpp"
    xp3filter.cpp
    ${AETHER_SHRINK_COPY_SOURCE}
    extrans.cpp
    extrans_precise/mosaic.cpp
    extrans_precise/ripple.cpp
    extrans_precise/rotatebase.cpp
    extrans_precise/rotatetrans.cpp
    extrans_precise/turn.cpp
    extrans_precise/turntrans_table.cpp
    extrans_precise/wave.cpp
    extNagano.cpp
    multiimage.cpp
    alphamovie.cpp
    ${AETHER_LAYEREX_BTOA_SOURCE}
    ${AETHER_LAYEREX_IMAGE_SOURCE}
    ${AETHER_LAYEREX_RASTER_SOURCE}
    ${AETHER_LAYEREX_AREA_AVERAGE_SOURCE}
    ${AETHER_LAYEREX_LONG_EXPOSURE_SOURCE}
    wfBasicEffectCompat.cpp
    wfTypicalDSPCompat.cpp
    packinone.cpp
    clipboardEx.cpp
    compatLegacyPlugins.cpp
    compatMediaLayerPlugins.cpp
    krkrzLayerExVectorCompat.cpp
    compatSystemPlugins.cpp
    stubs/dummy_plugin_stubs.cpp
    pluginAnchors.cpp
    fstat/main.cpp
)

target_sources(${PROJECT_NAME} PRIVATE
    upstream_bridge/extNaganoProviders.cpp
    ${AETHER_EXTNAGANO_UPSTREAM_SOURCES})
target_compile_definitions(${PROJECT_NAME} PRIVATE
    AETHER_KRKRZ_EXTNAGANO_UPSTREAM=1)
# Aether's local tp_stub intentionally omits the generated calling-convention
# macro. The upstream files expect it to expand to an empty token on
# non-Windows hosts; apply that definition only to these selected sources.
set_source_files_properties(${AETHER_EXTNAGANO_UPSTREAM_SOURCES}
    PROPERTIES COMPILE_DEFINITIONS "TJS_INTF_METHOD=")

if(BUILD_GPU_BRIDGE)
    target_sources(${PROJECT_NAME} PRIVATE krkrgles.cpp)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    ${KRKR2CORE_PATH}/
    ${KRKR2CORE_PATH}/base
    ${KRKR2CORE_PATH}/base/win32
    ${KRKR2CORE_PATH}/environ
    ${KRKR2CORE_PATH}/environ/win32
    ${KRKR2CORE_PATH}/environ/android
    ${KRKR2CORE_PATH}/environ/sdl
    ${KRKR2CORE_PATH}/msg
    ${KRKR2CORE_PATH}/msg/win32
    ${KRKR2CORE_PATH}/extension
    ${KRKR2CORE_PATH}/sound
    ${KRKR2CORE_PATH}/sound/win32
    ${KRKR2CORE_PATH}/tjs2
    ${KRKR2CORE_PATH}/utils
    ${KRKR2CORE_PATH}/utils/win32
    ${KRKR2CORE_PATH}/visual
    ${KRKR2CORE_PATH}/visual/ARM
    ${KRKR2CORE_PATH}/visual/win32
    ${KRKR2CORE_PATH}/visual/RenderScript/rs
)

add_definitions(-D__STDC_CONSTANT_MACROS -DTJS_TEXT_OUT_CRLF)

if(ANDROID)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SQLITE_DISABLE_UMASK)
endif()

if(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SQLITE_WITHOUT_ZONEMALLOC)
endif()

# The public target only owns the script-compatible fallback.  The optional
# AetherInternal package adds the Cubism SDK and the native Live2D loaders and
# renderers to this target without replacing the rest of krkr2plugin.
if(AETHERKIRI_INTERNAL_ENABLED AND COMMAND aetherinternal_extend_live2d)
    aetherinternal_extend_live2d(${PROJECT_NAME})
else()
    target_sources(${PROJECT_NAME} PRIVATE krkrlive2d_stub.cpp)
endif()
if(AETHERKIRI_INTERNAL_ENABLED AND
   COMMAND aetherinternal_extend_legacy_plugins)
    aetherinternal_extend_legacy_plugins(${PROJECT_NAME})
endif()

if(AETHERKIRI_INTERNAL_ENABLED AND COMMAND aetherinternal_extend_krkr2plugin)
    aetherinternal_extend_krkr2plugin(${PROJECT_NAME})
else()
    target_sources(${PROJECT_NAME} PRIVATE
        tjsNs0DataPack.cpp
        textrender.cpp
        drawDeviceD2DCompat.cpp
    )
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${KRKR2PLUGIN_PATH} ${KRKR2PLUGIN_PATH}/simplebinder)
target_include_directories(${PROJECT_NAME} PRIVATE
    "${AETHER_KRKRZ_PLUGIN_ROOT}/layerExSave")
# Wrappers include upstream translation units by plugin-relative path so an
# alternate pinned checkout can be supplied with -DAETHER_KRKRZ_DEV_ROOT=...
# without editing source files.
target_include_directories(${PROJECT_NAME} PRIVATE
    "${AETHER_KRKRZ_PLUGIN_ROOT}")
target_link_libraries(${PROJECT_NAME} PUBLIC krkr2core)

add_subdirectory(psdfile)
add_subdirectory(psbfile)
if(NOT WEB AND NOT EMSCRIPTEN)
    add_subdirectory(layerex_draw)
endif()
add_subdirectory(motionplayer)
add_subdirectory(kagparserex)
add_subdirectory(extkagparser)

#add_subdirectory(json)
#add_subdirectory(steam)
#add_subdirectory(DrawDeviceForSteam)
target_link_libraries(${PROJECT_NAME} PUBLIC
        psdfile
        psbfile
        motionplayer
        kagparserex
        extkagparser
#        json steam DrawDeviceForSteam
)
if(NOT WEB AND NOT EMSCRIPTEN)
    target_link_libraries(${PROJECT_NAME} PUBLIC layerExDraw)
endif()

find_package(fmt CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt)

find_package(spdlog CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)

find_package(expat CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE expat::expat)

find_package(ZLIB REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE ZLIB::ZLIB)
