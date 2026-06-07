cmake_minimum_required(VERSION 3.19)
project(krkr2plugin)

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PRIVATE
    scriptsEx.cpp
    binaryStream.cpp
    base64.cpp
    encode.cpp
    expat.cpp
    imagesaver.cpp
    lineParser.cpp
    memfile.cpp
    json/jsonPlugin.cpp
    minizip.cpp
    qrcode.cpp
    qrcode/QR_Encode.cpp
    sqlitePlugin.cpp
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
    layerExPerspective.cpp
    fftgraph.cpp
    LayerExBase.cpp
    layerExSaveCompat.cpp
    xp3filter.cpp
    extrans.cpp
    extNagano.cpp
    multiimage.cpp
    textrender.cpp
    alphamovie.cpp
    layerExBTOA.cpp
    layerExImage.cpp
    layerExRaster.cpp
    layerExAreaAverage.cpp
    layerExLongExposure.cpp
    drawDeviceD2DCompat.cpp
    wfBasicEffectCompat.cpp
    wfTypicalDSPCompat.cpp
    packinone.cpp
    clipboardEx.cpp
    dummy_plugin_stubs.cpp
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

# ---------------------------------------------------------------------------
# Live2D Cubism SDK
# ---------------------------------------------------------------------------
set(CUBISM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cubism)
set(CUBISM_CORE_DIR ${CUBISM_DIR}/Core)
set(CUBISM_FW_DIR ${CUBISM_DIR}/Framework)

file(GLOB_RECURSE CUBISM_FW_SOURCES
    ${CUBISM_FW_DIR}/*.cpp
)
# Exclude non-OpenGL renderers (already removed from tree, but safety net)
list(FILTER CUBISM_FW_SOURCES EXCLUDE REGEX ".*/D3D9/.*|.*/D3D11/.*|.*/Vulkan/.*|.*/Metal/.*|.*/Cocos2d/.*")
if(NOT BUILD_GPU_BRIDGE)
    list(FILTER CUBISM_FW_SOURCES EXCLUDE REGEX ".*/OpenGL/.*")
endif()

if(CUBISM_FW_SOURCES AND (IOS OR ANDROID OR APPLE))
    add_library(CubismFramework STATIC ${CUBISM_FW_SOURCES})
    target_include_directories(CubismFramework PUBLIC
        ${CUBISM_FW_DIR}
        ${CUBISM_FW_DIR}/src
        ${CUBISM_CORE_DIR}/include
    )
    target_compile_definitions(CubismFramework PUBLIC CSM_TARGET_GLES2)
    if(IOS)
        target_compile_definitions(CubismFramework PUBLIC CSM_TARGET_IPHONE_ES2 GLES_SILENCE_DEPRECATION)
    endif()

    if(ANDROID AND BUILD_GPU_BRIDGE)
        target_link_libraries(CubismFramework PRIVATE EGL GLESv2)
    endif()

    if(ANDROID)
        set(CUBISM_CORE_ARCH ${CMAKE_ANDROID_ARCH_ABI})
        set(CUBISM_CORE_LIB ${CUBISM_CORE_DIR}/lib/android/${CUBISM_CORE_ARCH}/libLive2DCubismCore.a)
    elseif(IOS)
        if(CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
            if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
                set(CUBISM_CORE_LIB ${CUBISM_CORE_DIR}/lib/ios/Release-iphonesimulator-x86_64/libLive2DCubismCore.a)
            else()
                set(CUBISM_CORE_LIB ${CUBISM_CORE_DIR}/lib/ios/Release-iphonesimulator-arm64/libLive2DCubismCore.a)
            endif()
        else()
            set(CUBISM_CORE_LIB ${CUBISM_CORE_DIR}/lib/ios/Release-iphoneos/libLive2DCubismCore.a)
        endif()
    elseif(APPLE)
        if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
            set(CUBISM_CORE_LIB ${CUBISM_CORE_DIR}/lib/macos/x86_64/libLive2DCubismCore.a)
        else()
            set(CUBISM_CORE_LIB ${CUBISM_CORE_DIR}/lib/macos/arm64/libLive2DCubismCore.a)
        endif()
    endif()

    target_link_libraries(CubismFramework PUBLIC ${CUBISM_CORE_LIB})

    target_include_directories(${PROJECT_NAME} PUBLIC
        ${CUBISM_FW_DIR}
        ${CUBISM_FW_DIR}/src
        ${CUBISM_CORE_DIR}/include
    )
    target_compile_definitions(${PROJECT_NAME} PUBLIC CSM_TARGET_GLES2)
    if(IOS)
        target_compile_definitions(${PROJECT_NAME} PUBLIC CSM_TARGET_IPHONE_ES2 GLES_SILENCE_DEPRECATION)
    endif()
    target_link_libraries(${PROJECT_NAME} PUBLIC CubismFramework)
    if(BUILD_GPU_BRIDGE)
        target_sources(${PROJECT_NAME} PRIVATE krkrlive2d.cpp)
    else()
        target_sources(${PROJECT_NAME} PRIVATE krkrlive2d_stub.cpp)
    endif()
else()
    message(WARNING "Live2D Cubism SDK Framework is missing or empty! krkrlive2d plugin will NOT be compiled. To fix, extract Live2D Cubism Native SDK into cpp/plugins/cubism/")
    target_sources(${PROJECT_NAME} PRIVATE krkrlive2d_stub.cpp)
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

#add_subdirectory(json)
#add_subdirectory(steam)
#add_subdirectory(DrawDeviceForSteam)
target_link_libraries(${PROJECT_NAME} PUBLIC
        psdfile
        psbfile
        motionplayer
        kagparserex
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
