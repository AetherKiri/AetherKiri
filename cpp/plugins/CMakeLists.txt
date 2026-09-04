cmake_minimum_required(VERSION 3.19)
project(krkr2plugin)

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PRIVATE
    scriptsEx.cpp
    kbadDataPack.cpp
    binaryStream.cpp
    base64.cpp
    encode.cpp
    expat.cpp
    mdkparser/LexicalAnalyzer.cpp
    mdkparser/MDKMessages.cpp
    mdkparser/MDKParser.cpp
    mdkparser/Parser.cpp
    mdkparser/ReservedWord.cpp
    mdkparser/plugin.cpp
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
    getSample.cpp
    kirikiroid2.cpp
    layerExPerspective.cpp
    fftgraph.cpp
    LayerExBase.cpp
    layerExSaveCompat.cpp
    xp3filter.cpp
    shrinkCopy.cpp
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
    layerExBTOA.cpp
    layerExImage.cpp
    layerExRaster.cpp
    layerExAreaAverage.cpp
    layerExLongExposure.cpp
    wfBasicEffectCompat.cpp
    wfTypicalDSPCompat.cpp
    packinone.cpp
    clipboardEx.cpp
    compatLegacyPlugins.cpp
    compatMediaLayerPlugins.cpp
    compatSystemPlugins.cpp
    dummy_plugin_stubs.cpp
    windows_compat_stubs.cpp
    pluginAnchors.cpp
    fstat/main.cpp
)

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

target_include_directories(${PROJECT_NAME} PUBLIC ${KRKR2PLUGIN_PATH} ${KRKR2PLUGIN_PATH}/simplebinder})
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
