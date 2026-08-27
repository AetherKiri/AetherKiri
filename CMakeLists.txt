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
        # Artemis is compiled into Apple Store products as a gated preview.
        # Debug builds bypass the product gate; Release requires a verified
        # beta-access grant before the runtime can open a game.
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
option(BUILD_GPU_BRIDGE "build legacy external GPU bridge renderer" OFF)
option(AETHERKIRI_ENABLE_ONSCRIPTER
       "enable the bundled OnscripterYuri runtime" ON)
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
        if(NOT AETHERINTERNAL_PACKAGE_API_VERSION EQUAL 6 OR
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
           NOT COMMAND aetherinternal_extend_artemis OR
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
elseif(NOT AETHERKIRI_ENABLE_INTERNAL)
    message(STATUS
        "AetherInternal package: explicitly disabled; building public "
        "fallbacks only (not the full compatibility product)")
else()
    message(STATUS "AetherInternal package: unavailable; using public fallbacks")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(KRKR2CORE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cpp/core)
set(KRKR2PLUGIN_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cpp/plugins)

# Selected krkrz_dev business sources are consumed through Aether adapters.
# The upstream checkout is a required, pinned submodule for the default
# integration build; its standalone ABI and CMake registry are never linked.
set(AETHER_KRKRZ_DEV_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/krkrz_dev"
    CACHE PATH "Pinned krkrz_dev checkout used for source-level reuse")
# Keep the plugin-root path in the parent scope so unit-test and adapter
# subdirectories can refer to the same checked-out headers.  The integration
# helper repeats this assignment in its local scope for standalone consumers.
set(AETHER_KRKRZ_PLUGIN_ROOT
    "${AETHER_KRKRZ_DEV_ROOT}/src/plugins")
option(AETHER_USE_KRKRZ_LEAF_PLUGINS
       "Use pinned krkrz_dev sources for the low-risk leaf plugins" ON)
option(AETHER_USE_KRKRZ_OPTIONAL_PLUGINS
       "Validate experimental krkrz_dev plugin sources without linking them" OFF)
option(AETHER_BUILD_KRKRZ_CORE_PARITY
       "Build isolated krkrz_dev visual/sound SIMD parity executables" OFF)

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

# external lib
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cpp/external)

# build library
add_subdirectory(${KRKR2CORE_PATH})
add_subdirectory(${KRKR2PLUGIN_PATH})

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

if(BUILD_ENGINE_API OR AETHERKIRI_BUILD_NATIVE_ONSCRIPTER_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bridge/engine_api)
endif()
if(AETHERKIRI_ENABLE_ONSCRIPTER AND
   (BUILD_GODOT_EXTENSION OR AETHERKIRI_BUILD_NATIVE_ONSCRIPTER_TESTS))
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bridge/onscripter_runtime)
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
