cmake_minimum_required(VERSION 3.19)

project(aether_onscripter_runtime LANGUAGES CXX)

find_package(BZip2 REQUIRED)
find_package(JPEG REQUIRED)
find_package(Lua REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)

set(ONSCRIPTER_YURI_ROOT
    "${CMAKE_SOURCE_DIR}/packages/OnscripterYuri")
set(ONSCRIPTER_YURI_SOURCE_DIR
    "${ONSCRIPTER_YURI_ROOT}/src/onsyuri")

if(NOT EXISTS "${ONSCRIPTER_YURI_SOURCE_DIR}/ONScripter.cpp")
    message(FATAL_ERROR
        "OnscripterYuri submodule is unavailable. Run "
        "`git submodule update --init packages/OnscripterYuri`.")
endif()

include(cmake/PrepareOnscripterYuriSources.cmake)
aetherkiri_prepare_onscripter_yuri_sources(
    "${ONSCRIPTER_YURI_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/onscripter_yuri"
    ONSCRIPTER_YURI_BASE_SOURCE
    ONSCRIPTER_YURI_SOUND_SOURCE
    ONSCRIPTER_YURI_COMMAND_SOURCE
    ONSCRIPTER_YURI_EVENT_SOURCE
    ONSCRIPTER_YURI_PARSER_SOURCE
    ONSCRIPTER_YURI_PARALLEL_SOURCE)

set(ONSCRIPTER_YURI_SOURCES
    "${ONSCRIPTER_YURI_SOURCE_DIR}/AnimationInfo.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/coding2utf16.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/DirectReader.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/DirtyRect.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/FontInfo.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/gbk2utf16.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/LUAHandler.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/NsaReader.cpp"
    "${ONSCRIPTER_YURI_BASE_SOURCE}"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/ONScripter_animation.cpp"
    "${ONSCRIPTER_YURI_COMMAND_SOURCE}"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/ONScripter_effect.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/ONScripter_effect_breakup.cpp"
    "${ONSCRIPTER_YURI_EVENT_SOURCE}"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/ONScripter_file.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/ONScripter_file2.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/ONScripter_image.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/ONScripter_lut.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/ONScripter_rmenu.cpp"
    "${ONSCRIPTER_YURI_SOUND_SOURCE}"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/ONScripter_text.cpp"
    "${ONSCRIPTER_YURI_PARALLEL_SOURCE}"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/resize_image.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/SarReader.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/ScriptHandler.cpp"
    "${ONSCRIPTER_YURI_PARSER_SOURCE}"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/ScriptParser_command.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/sjis2utf16.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/builtin_dll/layer_oldmovie.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/builtin_dll/layer_snow.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/builtin_dll/ONScripter_effect_cascade.cpp"
    "${ONSCRIPTER_YURI_SOURCE_DIR}/builtin_dll/ONScripter_effect_trig.cpp"
)

add_library(aether_onscripter_runtime STATIC
    src/onscripter_runtime.cpp
    src/onscripter_runtime_provider.cpp
    src/onscripter_save_storage.cpp
    ${ONSCRIPTER_YURI_SOURCES}
)

target_compile_features(aether_onscripter_runtime PUBLIC cxx_std_17)
set_target_properties(aether_onscripter_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    # Keep manifest headers ahead of host-global /usr/local headers. This is
    # especially important for Lua, whose header and library ABIs must match.
    NO_SYSTEM_FROM_IMPORTED ON)

target_compile_definitions(aether_onscripter_runtime PRIVATE
    AETHERKIRI_EMBEDDED_HOST
    ENABLE_1BYTE_CHAR
    USE_BUILTIN_LAYER_EFFECTS
    USE_LUA
    USE_PARALLEL
    $<$<PLATFORM_ID:Darwin>:MACOSX>
    $<$<PLATFORM_ID:iOS>:MACOSX>
    $<$<PLATFORM_ID:iOS>:AETHERKIRI_IOS>
    $<$<PLATFORM_ID:Android>:ANDROID>
    $<$<PLATFORM_ID:Android>:UTF8_FILESYSTEM>
    $<$<PLATFORM_ID:Emscripten>:WEB>
    $<$<PLATFORM_ID:Emscripten>:LINUX>
    $<$<PLATFORM_ID:Linux>:LINUX>
)

target_include_directories(aether_onscripter_runtime
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${ONSCRIPTER_YURI_SOURCE_DIR}"
        "${LUA_INCLUDE_DIR}"
)

if(MSVC)
    target_compile_options(aether_onscripter_runtime PRIVATE
        /FI"${CMAKE_CURRENT_SOURCE_DIR}/src/onscripter_host_shim.h"
        /W0)
else()
    target_compile_options(aether_onscripter_runtime PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/src/onscripter_host_shim.h"
        -Wno-deprecated-declarations
        -Wno-invalid-source-encoding
        -Wno-unused-result)
endif()

target_link_libraries(aether_onscripter_runtime
    # This is a static implementation library. CMake still forwards PRIVATE
    # link dependencies to final consumers through LINK_ONLY, while keeping
    # their usage requirements out of the mixed C++/Swift Godot target. In
    # particular, vcpkg's uuid/uuid.h must not shadow the Apple SDK header
    # while Swift imports Darwin on iOS and macOS.
    PRIVATE
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
        $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
        $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
        $<IF:$<TARGET_EXISTS:SDL2_ttf::SDL2_ttf>,SDL2_ttf::SDL2_ttf,SDL2_ttf::SDL2_ttf-static>
        BZip2::BZip2
        JPEG::JPEG
        ${LUA_LIBRARIES}
)

if(NOT TARGET engine_api)
    message(FATAL_ERROR
        "The embedded OnscripterYuri media bridge requires engine_api.")
endif()
target_link_libraries(aether_onscripter_runtime PRIVATE engine_api)

if(UNIX)
    target_link_libraries(aether_onscripter_runtime PUBLIC m)
endif()

if(ANDROID)
    target_link_libraries(aether_onscripter_runtime PUBLIC log)
endif()
