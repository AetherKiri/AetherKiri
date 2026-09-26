cmake_minimum_required(VERSION 3.28)

find_program(CCACHE_PROGRAM ccache)

if (CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Ccache found: ${CCACHE_PROGRAM}")
endif ()

add_compile_definitions(
        $<$<CONFIG:Debug>:DEBUG>
        $<$<CONFIG:Debug>:_DEBUG>   # 对于 MSVC 兼容性
        # Artemis is a supported runtime in every product configuration.
        AETHERKIRI_ENABLE_ARTEMIS_RUNTIME
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

# Detect Android build: Gradle may set CMAKE_SYSTEM_NAME=Android or VCPKG_TARGET_ANDROID=ON
# before project() is called; the ANDROID variable is only set after project().
if(VCPKG_TARGET_ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(VCPKG_TARGET_ANDROID ON)
    # Gradle passes ANDROID_NDK as a CMake -D variable, propagate it to env
    if(DEFINED ANDROID_NDK AND NOT DEFINED ENV{ANDROID_NDK_HOME})
        set(ENV{ANDROID_NDK_HOME} "${ANDROID_NDK}")
    elseif(DEFINED CMAKE_ANDROID_NDK AND NOT DEFINED ENV{ANDROID_NDK_HOME})
        set(ENV{ANDROID_NDK_HOME} "${CMAKE_ANDROID_NDK}")
    elseif(DEFINED ENV{ANDROID_NDK})
        set(ENV{ANDROID_NDK_HOME} "$ENV{ANDROID_NDK}")
    endif()
    # If Gradle already set CMAKE_TOOLCHAIN_FILE to vcpkg and VCPKG_CHAINLOAD_TOOLCHAIN_FILE
    # to the Android NDK toolchain, we only need to set VCPKG_TARGET_TRIPLET here.
    # Otherwise, fall back to the vcpkg_android.cmake helper.
    if(DEFINED VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
        # Gradle already configured the toolchain chain; just set the triplet
        if(DEFINED VCPKG_ROOT AND NOT DEFINED ENV{VCPKG_ROOT})
            set(ENV{VCPKG_ROOT} "${VCPKG_ROOT}")
        endif()
        if(ANDROID_ABI MATCHES "arm64-v8a")
            set(VCPKG_TARGET_TRIPLET "arm64-android" CACHE STRING "" FORCE)
        elseif(ANDROID_ABI MATCHES "armeabi-v7a")
            set(VCPKG_TARGET_TRIPLET "arm-android" CACHE STRING "" FORCE)
        elseif(ANDROID_ABI MATCHES "x86_64")
            set(VCPKG_TARGET_TRIPLET "x64-android" CACHE STRING "" FORCE)
        elseif(ANDROID_ABI MATCHES "x86")
            set(VCPKG_TARGET_TRIPLET "x86-android" CACHE STRING "" FORCE)
        endif()
        message(STATUS "Android build with Gradle-managed toolchain: triplet=${VCPKG_TARGET_TRIPLET}")
    else()
        include(cmake/vcpkg_android.cmake)
    endif()
else()
    set(CMAKE_TOOLCHAIN_FILE $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
endif()

project(krkr2)

# vcpkg's OpenAL target carries raw "-Wl,-framework,..." items in its
# interface. Imported targets are directory-scoped: every scope that calls
# find_package(OpenAL) gets a fresh copy with the raw flags, and the mixed
# C++/Swift Godot extension is linked by swiftc, which rejects the -Wl
# spelling. Each scope that creates an OpenAL::OpenAL copy must sanitize it
# to the driver-neutral LINKER: form (see aetherkiri_sanitize_openal_link).
function(aetherkiri_sanitize_openal_link)
    if(NOT TARGET OpenAL::OpenAL)
        return()
    endif()
    get_target_property(_aetherkiri_openal_interface OpenAL::OpenAL
        INTERFACE_LINK_LIBRARIES)
    if(NOT _aetherkiri_openal_interface)
        return()
    endif()
    list(FIND _aetherkiri_openal_interface "-Wl,-framework,AudioToolbox"
        _aetherkiri_openal_raw_index)
    if(_aetherkiri_openal_raw_index EQUAL -1)
        return()
    endif()
    list(TRANSFORM _aetherkiri_openal_interface REPLACE
        "^-Wl,-framework,(.+)$" "LINKER:-framework,\\1")
    # macOS pthreads live in libSystem; the bare -pthread driver flag would
    # reach swiftc-driven links (which reject it) through this interface.
    list(REMOVE_ITEM _aetherkiri_openal_interface "$<LINK_ONLY:-pthread>")
    set_target_properties(OpenAL::OpenAL PROPERTIES
        INTERFACE_LINK_LIBRARIES "${_aetherkiri_openal_interface}")
endfunction()

if(ANDROID)
    add_compile_options(-Wno-inconsistent-missing-override)
endif()

if(WEB OR EMSCRIPTEN)
    add_compile_options(
        -fPIC
        -pthread
        -msimd128
        -sSUPPORT_LONGJMP=wasm
        -sWASM_LEGACY_EXCEPTIONS=1
        $<$<COMPILE_LANGUAGE:CXX>:-fwasm-exceptions>
    )
    add_link_options(
        -pthread
        -msimd128
        -sSUPPORT_LONGJMP=wasm
        -sWASM_LEGACY_EXCEPTIONS=1
        -fwasm-exceptions
    )
endif()

option(ENABLE_TESTS "enable tests execute build(exclude android)" ON)
option(BUILD_TOOLS "build tools execute build(exclude android ios)" ON)
option(BUILD_ENGINE_API "build engine_api bridge shared library" ON)
option(BUILD_GODOT_EXTENSION "build Godot GDExtension host library" ON)
option(AETHERKIRI_ENABLE_RFVP "build the experimental native FVP runtime (requires Rust)" OFF)
option(BUILD_GPU_BRIDGE "build legacy external GPU bridge renderer" OFF)
option(AETHERKIRI_ENABLE_ONSCRIPTER
       "enable the bundled OnscripterYuri runtime" ON)
option(AETHERKIRI_ENABLE_SIGLUS
       "enable the bundled siglus_rs runtime (requires a Rust toolchain)" ON)
option(AETHERKIRI_ENABLE_MINORI
       "enable the independent Rust Minori runtime" ON)
option(AETHERKIRI_ENABLE_CODE_OBFUSCATION
       "obfuscate private Internal translation units in release builds" OFF)
set(AETHERKIRI_OBFUSCATOR_PLUGIN "" CACHE FILEPATH
    "path to a Kagura LLVM pass plugin built for the active Clang")
set(AETHERKIRI_OBFUSCATION_BUILD_ID "local" CACHE STRING
    "non-secret identifier mixed into deterministic obfuscation output")
option(AETHERKIRI_ENABLE_INTERNAL
       "enable optional private AetherInternal packages when available" ON)
set(AETHERKIRI_MOTIONPLAYER_BACKEND_API_VERSION 4)
set(AETHERKIRI_LIVE2D_HOST_API_VERSION 1)
set(AETHERKIRI_FRAME_EFFECT_HOST_API_VERSION 2)
set(AETHERKIRI_INTERNAL_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/packages/AetherInternal"
    CACHE PATH "path to an AetherInternal package checkout")
# Local checkouts for the runtime submodules. Point these at a working copy
# to iterate on a runtime without committing a gitlink bump (same pattern as
# AETHERKIRI_INTERNAL_DIR).
set(AETHERKIRI_ONSCRIPTERYURI_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/packages/OnscripterYuri"
    CACHE PATH "path to an OnscripterYuri runtime checkout")
set(AETHERKIRI_SIGLUS_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/packages/AetherSiglus"
    CACHE PATH "path to a siglus_rs runtime checkout")
set(AETHERKIRI_MINORI_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/packages/AetherMinori"
    CACHE PATH "path to an AetherMinori runtime checkout")
set(AETHERKIRI_RFVP_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/packages/rfvp"
    CACHE PATH "path to an rfvp runtime checkout")

include(cmake/AetherKiriBinaryProtection.cmake)

set(AETHERKIRI_INTERNAL_ENABLED OFF)
if(AETHERKIRI_ENABLE_INTERNAL)
    # Use a normal (non-cache) package hint so a previous configured checkout
    # cannot keep a removed/private submodule enabled through stale cache.
    set(AetherInternal_DIR "${AETHERKIRI_INTERNAL_DIR}/cmake")
    unset(AetherInternal_FOUND)
    find_package(AetherInternal CONFIG QUIET
        NO_DEFAULT_PATH)
    if(AetherInternal_FOUND)
        if(NOT AETHERINTERNAL_PACKAGE_API_VERSION EQUAL 8 OR
           NOT AETHERINTERNAL_REQUIRED_MOTIONPLAYER_API_VERSION EQUAL
               AETHERKIRI_MOTIONPLAYER_BACKEND_API_VERSION OR
           NOT AETHERINTERNAL_REQUIRED_LIVE2D_HOST_API_VERSION EQUAL
               AETHERKIRI_LIVE2D_HOST_API_VERSION OR
           NOT AETHERINTERNAL_REQUIRED_FRAME_EFFECT_HOST_API_VERSION EQUAL
               AETHERKIRI_FRAME_EFFECT_HOST_API_VERSION OR
           NOT COMMAND aetherinternal_extend_motionplayer OR
           NOT COMMAND aetherinternal_extend_psbfile OR
           NOT COMMAND aetherinternal_extend_legacy_plugins OR
           NOT COMMAND aetherinternal_extend_live2d OR
           NOT COMMAND aetherinternal_extend_catsystem2 OR
           NOT COMMAND aetherinternal_extend_catsystem2_tests OR
           NOT COMMAND aetherinternal_add_catsystem2_probe OR
           NOT COMMAND aetherinternal_add_artemis_tests OR
           NOT COMMAND aetherinternal_extend_artemis OR
           NOT COMMAND aetherinternal_extend_text_translation OR
           NOT COMMAND aetherinternal_extend_godot_extension OR
           NOT COMMAND aetherinternal_extend_krkr2plugin OR
           NOT COMMAND aetherinternal_extend_krkr2plugin_tests)
            message(FATAL_ERROR
                "AetherInternal is present but is incompatible with the "
                "motionplayer backend contract required by this checkout")
        endif()
        set(AETHERKIRI_INTERNAL_ENABLED ON)
    endif()
endif()

if(AETHERKIRI_INTERNAL_ENABLED)
    message(STATUS "AetherInternal E-mote/Live2D package: enabled")
else()
    message(STATUS "AetherInternal package: unavailable; using public fallbacks")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The krkr2 engine runtime lives in its own repository (issue #241 Phase 2)
# and is embedded as the packages/krkr2 submodule. This consuming build
# injects the two paths the engine repository needs: the engine_api ABI
# headers (engine_gpu_bridge.h / TextTransform.h consumed by the core
# visual/utils modules) and the psdfile adapter package.
set(AETHERKIRI_KRKR2_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/packages/krkr2"
    CACHE PATH "path to a krkr2 engine checkout")
set(KRKR2_ENGINE_ABI_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/bridge/engine_api/include")
set(KRKR2_PSDFILE_PACKAGE_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/packages/psdfile")

if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/EHsc>" "/MP" "/utf-8")
    add_link_options(
        "/ignore:4099"
        "$<$<CONFIG:Debug>:/INCREMENTAL>"
        "$<$<CONFIG:Debug>:/DEBUG:FASTLINK>"
        "$<$<CONFIG:Release>:/INCREMENTAL:NO>"
        "$<$<CONFIG:Release>:/OPT:REF>"
        "$<$<CONFIG:Release>:/OPT:ICF>"
    )
    # set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Godot is the product app shell on the wired platforms. This native build
# produces engine libraries and the GDExtension host, not standalone launchers.

# The krkr2 engine repository defines the engine targets (external/, core/,
# plugins/); its integration glue stays in bridge/krkr2_runtime.
add_subdirectory(${AETHERKIRI_KRKR2_DIR})

# AetherInternal contributes sources to public targets but does not own the
# obfuscation policy. Register only package-owned source units for protected
# iOS Release builds.
if(IOS AND AETHERKIRI_INTERNAL_ENABLED AND AETHERKIRI_ENABLE_CODE_OBFUSCATION)
    file(GLOB_RECURSE AETHERKIRI_INTERNAL_EMOTE_SOURCES CONFIGURE_DEPENDS
        "${AETHERKIRI_INTERNAL_DIR}/src/emote/*.cpp")
    file(GLOB_RECURSE AETHERKIRI_INTERNAL_PSB_SOURCES CONFIGURE_DEPENDS
        "${AETHERKIRI_INTERNAL_DIR}/src/psbfile/*.cpp")
    file(GLOB_RECURSE AETHERKIRI_INTERNAL_PLUGIN_SOURCES CONFIGURE_DEPENDS
        "${AETHERKIRI_INTERNAL_DIR}/src/plugins/*.cpp"
        "${AETHERKIRI_INTERNAL_DIR}/src/krkr2_plugins/*.cpp"
        "${AETHERKIRI_INTERNAL_DIR}/src/live2d/*.cpp")
    if(TARGET motionplayer AND AETHERKIRI_INTERNAL_EMOTE_SOURCES)
        aetherkiri_protect_internal_sources(motionplayer
            ${AETHERKIRI_INTERNAL_EMOTE_SOURCES})
    endif()
    if(TARGET psbfile AND AETHERKIRI_INTERNAL_PSB_SOURCES)
        aetherkiri_protect_internal_sources(psbfile
            ${AETHERKIRI_INTERNAL_PSB_SOURCES})
    endif()
    if(TARGET krkr2plugin AND AETHERKIRI_INTERNAL_PLUGIN_SOURCES)
        aetherkiri_protect_internal_sources(krkr2plugin
            ${AETHERKIRI_INTERNAL_PLUGIN_SOURCES})
    endif()
endif()

set(AETHERKIRI_BUILD_NATIVE_ONSCRIPTER_TESTS OFF)
if(ENABLE_TESTS AND AETHERKIRI_ENABLE_ONSCRIPTER AND
   NOT (IOS OR ANDROID OR WEB OR EMSCRIPTEN))
    # The provider contract links the real ONS runtime and runtime registry.
    # Keep those targets available in the native test-only configuration used
    # by CI even when product bridge targets are otherwise disabled.
    set(AETHERKIRI_BUILD_NATIVE_ONSCRIPTER_TESTS ON)
endif()

# The KiriKiri runtime glue owns the legacy engine implementation and the
# krkr2core/krkr2plugin linkage; the Godot extension host links the glue and
# installs its engine_legacy_* surface into engine_api through the legacy
# services table (Phase 2d link flip — engine_api itself never links it).
if(BUILD_ENGINE_API OR AETHERKIRI_BUILD_NATIVE_ONSCRIPTER_TESTS OR AETHERKIRI_ENABLE_RFVP)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bridge/krkr2_runtime)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bridge/engine_api)
endif()
if(AETHERKIRI_ENABLE_ONSCRIPTER AND
   (BUILD_GODOT_EXTENSION OR AETHERKIRI_BUILD_NATIVE_ONSCRIPTER_TESTS))
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bridge/onscripter_runtime)
endif()
if(AETHERKIRI_ENABLE_SIGLUS AND BUILD_GODOT_EXTENSION AND
   NOT (WEB OR EMSCRIPTEN))
    # Web stays excluded until the wasm host path is designed (phase 5).
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bridge/siglus_runtime)
endif()
# The Rust provider has targets for Linux, Apple, Android, and Emscripten.
# Keep it out of unsupported native targets, but do not make Linux the only
# product platform that can register Minori.
if(AETHERKIRI_ENABLE_MINORI AND BUILD_GODOT_EXTENSION AND
    (LINUX OR APPLE OR ANDROID OR EMSCRIPTEN))
    add_subdirectory(${AETHERKIRI_MINORI_DIR})
endif()
if(AETHERKIRI_ENABLE_RFVP)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bridge/rfvp_runtime)
endif()
if(BUILD_GODOT_EXTENSION)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bridge/godot_extension)
endif()

if(ENABLE_TESTS AND NOT (IOS OR ANDROID OR WEB OR EMSCRIPTEN))
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

if(BUILD_TOOLS AND NOT (IOS OR ANDROID OR WEB OR EMSCRIPTEN))
    add_subdirectory(tools)
endif()
